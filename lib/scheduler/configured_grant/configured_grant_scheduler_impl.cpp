// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "configured_grant_scheduler_impl.h"
#include "../cell/resource_grid.h"
#include "../support/bwp_helpers.h"
#include "../support/dmrs_helpers.h"
#include "../support/mcs_tbs_calculator.h"
#include "../support/sch_pdu_builder.h"
#include "../uci_scheduling/uci_allocator_impl.h"
#include "ocudu/ran/csi_report/csi_report_config_helpers.h"
#include "ocudu/ran/direct_current_offset.h"
#include <optional>

using namespace ocudu;

static unsigned rep_to_rv(const cg_configuration::repetitions_t& reps, unsigned rep_idx)
{
  constexpr std::array<unsigned, 4> rv_0303 = {0, 3, 0, 3};

  // TODO review if last rep idx 8 is correct.

  // RV index for the first repetition is always 0.
  if (reps.rep_k == cg_configuration::rep_k_t::n1 or reps.rv_seq == cg_configuration::rep_k_rv::s3_0000) {
    return 0U;
  }
  if (reps.rv_seq == cg_configuration::rep_k_rv::s1_0231) {
    constexpr std::array<unsigned, 4> rv_0231 = {0, 2, 3, 1};
    return rv_0231[rep_idx % static_cast<unsigned>(reps.rep_k)];
  }
  return rv_0303[rep_idx % static_cast<unsigned>(reps.rep_k)];
}

static harq_id_t get_harq_id(slot_point                      pusch_slot,
                             unsigned                        symbol,
                             cg_configuration::periodicity_t periodicity,
                             uint8_t                         nof_harq_processes)
{
  // As per TS 38.321, Section 5.4.1.
  const unsigned current_symbol =
      static_cast<unsigned>(pusch_slot.system_slot()) * NOF_OFDM_SYM_PER_SLOT_NORMAL_CP + symbol;
  const unsigned periodicity_sym = static_cast<unsigned>(periodicity) * NOF_OFDM_SYM_PER_SLOT_NORMAL_CP;
  return to_harq_id((current_symbol / periodicity_sym) % nof_harq_processes);
}

configured_grant_scheduler_impl::configured_grant_scheduler_impl(const cell_configuration& cell_cfg_,
                                                                 uci_allocator&            uci_alloc_,
                                                                 ue_repository&            ues_) :
  cell_cfg(cell_cfg_), uci_alloc(uci_alloc_), ues(ues_), logger(ocudulog::fetch_basic_logger("SCHED"))
{
  periodic_pusch_slot_wheel.resize(max_cg_slot_periodicity);
}

const ue_cell_configuration* configured_grant_scheduler_impl::get_ue_cfg(rnti_t rnti) const
{
  auto* u = ues.find_by_rnti(rnti);
  if (u != nullptr) {
    const auto* ue_cc = u->find_cell(cell_cfg.cell_index);
    if (ue_cc != nullptr) {
      return &ue_cc->cfg();
    }
  }
  return nullptr;
}

void configured_grant_scheduler_impl::update_harq_reservation(const ue_cell_configuration& ue_cfg)
{
  auto* u = ues.find_by_rnti(ue_cfg.crnti);
  if (u == nullptr) {
    return;
  }
  auto* ue_cc = u->find_cell(cell_cfg.cell_index);
  if (ue_cc == nullptr) {
    return;
  }

  const auto*    ul_ded = ue_cfg.init_bwp().ul.ded();
  const unsigned nof_cg_reserved =
      (ul_ded != nullptr and ul_ded->cg_cfg.has_value() and ul_ded->cg_cfg->rrc_configured_ul_grant_cfg.has_value())
          ? ul_ded->cg_cfg->nof_harq_processes
          : 0U;

  ue_cc->harqs.reconfigure(ue_cc->harqs.nof_dl_harqs(), ue_cc->harqs.nof_ul_harqs(), {}, {}, nof_cg_reserved);
}

void configured_grant_scheduler_impl::add_ue_to_wheel(const ue_cell_configuration& ue_cfg)
{
  if (ue_cfg.init_bwp().ul.ded() == nullptr or not ue_cfg.init_bwp().ul.ded()->cg_cfg.has_value()) {
    return;
  }
  const cg_configuration& cg_cfg = ue_cfg.init_bwp().ul.ded()->cg_cfg.value();

  // Only handle Type 1 CG (RRC-configured grant; Type 2 requires DCI activation).
  if (not cg_cfg.rrc_configured_ul_grant_cfg.has_value()) {
    return;
  }
  const auto& ul_grant = cg_cfg.rrc_configured_ul_grant_cfg.value();

  const auto period_slots = static_cast<unsigned>(cg_cfg.periodicity);

  // Fill the slot wheel at every slot where a CG PUSCH opportunity occurs.
  for (unsigned wheel_offset = ul_grant.time_domain_offset; wheel_offset < max_cg_slot_periodicity;
       wheel_offset += period_slots) {
    auto& slot_wheel = periodic_pusch_slot_wheel[wheel_offset];

    if (std::find(slot_wheel.begin(), slot_wheel.end(), ue_cfg.crnti) != slot_wheel.end()) {
      logger.error("rnti={}: CG grant already present in slot wheel at offset={}", ue_cfg.crnti, wheel_offset);
      continue;
    }
    slot_wheel.push_back(ue_cfg.crnti);
  }
}

void configured_grant_scheduler_impl::rem_ue(const ue_cell_configuration& ue_cfg)
{
  if (ue_cfg.init_bwp().ul.ded() == nullptr or not ue_cfg.init_bwp().ul.ded()->cg_cfg.has_value()) {
    return;
  }
  const cg_configuration& cg_cfg = ue_cfg.init_bwp().ul.ded()->cg_cfg.value();

  if (not cg_cfg.rrc_configured_ul_grant_cfg.has_value()) {
    return;
  }
  const auto& ul_grant = cg_cfg.rrc_configured_ul_grant_cfg.value();

  const auto period_slots = static_cast<unsigned>(cg_cfg.periodicity);

  for (unsigned wheel_offset = ul_grant.time_domain_offset; wheel_offset < max_cg_slot_periodicity;
       wheel_offset += period_slots) {
    auto& slot_wheel = periodic_pusch_slot_wheel[wheel_offset];

    auto* it = std::find(slot_wheel.begin(), slot_wheel.end(), ue_cfg.crnti);
    if (it == slot_wheel.end()) {
      logger.error(
          "rnti={}: CG grant not found in slot wheel at offset={} during UE removal", ue_cfg.crnti, wheel_offset);
      continue;
    }

    // Swap with last element and pop for O(1) removal.
    if (it != slot_wheel.end() - 1) {
      std::swap(*it, *(slot_wheel.end() - 1));
    }
    slot_wheel.pop_back();
  }
}

void configured_grant_scheduler_impl::add_reconf_ue(const ue_cell_configuration& new_ue_cfg,
                                                    const ue_cell_configuration& old_ue_cfg)
{
  const auto* new_ul_ded = new_ue_cfg.init_bwp().ul.ded();
  const auto* old_ul_ded = old_ue_cfg.init_bwp().ul.ded();

  if (new_ul_ded != nullptr and old_ul_ded != nullptr and new_ul_ded->cg_cfg.has_value() and
      old_ul_ded->cg_cfg.has_value() and new_ul_ded->cg_cfg.value() == old_ul_ded->cg_cfg.value()) {
    // CG configuration unchanged — nothing to do.
    return;
  }

  rem_ue(old_ue_cfg);
  add_ue_to_wheel(new_ue_cfg);
  update_harq_reservation(new_ue_cfg);
}

void configured_grant_scheduler_impl::run_slot(cell_resource_allocator& cell_alloc)
{
  // We only allocate CG PUSCH for the current slot; else, we incur the possibility that the PUSCH gets allocated before
  // the PUCCH.
  static constexpr unsigned look_ahead_alloc_slots = 0U;
  allocate_slot_cg_opportunities(cell_alloc[look_ahead_alloc_slots]);
}

void configured_grant_scheduler_impl::allocate_slot_cg_opportunities(cell_slot_resource_allocator& slot_alloc) const
{
  const auto& rnti_list = periodic_pusch_slot_wheel[slot_alloc.slot.to_uint() % max_cg_slot_periodicity];
  for (const rnti_t rnti : rnti_list) {
    allocate_cg_opportunity(slot_alloc, rnti);
  }
}

bool configured_grant_scheduler_impl::allocate_cg_opportunity(cell_slot_resource_allocator& slot_alloc,
                                                              rnti_t                        rnti) const
{
  // Fetch UE and its cell context.
  auto* u = ues.find_by_rnti(rnti);
  if (u == nullptr) {
    logger.error("rnti={}: CG opportunity scheduled but UE not found", rnti);
    return false;
  }

  ocudu_assert(
      u->ue_cfg_dedicated()->get_cs_rnti().has_value(), "Missing CS-RNTI for UE={} with Configured Grant", rnti);

  auto* ue_cc = u->find_cell(cell_cfg.cell_index);
  if (ue_cc == nullptr) {
    logger.error("rnti={}: CG opportunity scheduled but UE cell not found", rnti);
    return false;
  }
  const ue_cell_configuration& ue_cfg = ue_cc->cfg();

  // Skip if UL is not enabled in this slot (e.g., TDD DL slot).
  if (not ue_cfg.is_ul_enabled(slot_alloc.slot)) {
    return false;
  }

  const slot_point pusch_slot = slot_alloc.slot;

  // CG and UL grant configs — already validated when the UE was added to the wheel.
  const auto* ul_ded   = ue_cfg.init_bwp().ul.ded();
  const auto& cg_cfg   = ul_ded->cg_cfg.value();
  const auto& ul_grant = cg_cfg.rrc_configured_ul_grant_cfg.value();

  // Retrieve the PUSCH time-domain resource allocation entry.
  // TODO: also check the dedicated PUSCH time-domain list when available in UE config.
  if (not cell_cfg.params.ul_cfg_common.init_ul_bwp.pusch_cfg_common.has_value()) {
    logger.error("rnti={}: Common PUSCH config absent, cannot schedule CG PUSCH", rnti);
    return false;
  }
  const auto& pusch_td_list = cell_cfg.params.ul_cfg_common.init_ul_bwp.pusch_cfg_common->pusch_td_alloc_list;
  if (ul_grant.time_domain_allocation >= pusch_td_list.size()) {
    logger.error("rnti={}: CG time_domain_allocation={} is out of range (list size={})",
                 rnti,
                 ul_grant.time_domain_allocation,
                 pusch_td_list.size());
    return false;
  }
  const pusch_time_domain_resource_allocation& pusch_td_cfg = pusch_td_list[ul_grant.time_domain_allocation];
  pusch_config_params                          pusch_params;
  pusch_params.symbols = pusch_td_cfg.symbols;

  // TODO: verify max nof HARQ retx
  // TODO: verify normal mode for CG.
  static constexpr unsigned nof_harq_retx = 0;
  const harq_id_t           h_id =
      get_harq_id(pusch_slot, pusch_params.symbols.start(), cg_cfg.periodicity, cg_cfg.nof_harq_processes);
  auto h_ul = ue_cc->harqs.alloc_ul_harq(pusch_slot, nof_harq_retx, h_id, /*select_normal_mode*/ true);
  ocudu_assert(h_ul.has_value(), "Failed to allocate UL HARQ id={}", fmt::underlying(h_id));

  // Build DMRS information from the CG-specific DMRS configuration.
  static constexpr unsigned nof_layers           = 1;
  static constexpr bool     are_both_cws_enabled = false;
  const dmrs_information    dmrs                 = make_dmrs_info_dedicated(pusch_td_cfg,
                                                         cell_cfg.params.pci,
                                                         cell_cfg.params.dmrs_typeA_pos,
                                                         cg_cfg.cg_dmrs_cfg,
                                                         nof_layers,
                                                         cell_cfg.params.ul_carrier.nof_ant,
                                                         are_both_cws_enabled);

  // Build PUSCH configuration parameters for TBS computation.
  pusch_params.dmrs               = dmrs;
  pusch_params.mcs_table          = cg_cfg.mcs_table;
  pusch_params.nof_layers         = nof_layers;
  pusch_params.tp_pi2bpsk_present = false;
  // CG PUSCH uses CP-OFDM (no transform precoding). The mcs_table_transform_precoder field is separate.
  pusch_params.use_transform_precoder = false;
  // As per TS 38.214, Section 5.1.3.2 and 6.1.4.2, and TS 38.212, Section 7.3.1.1 and 7.3.1.2, TB scaling filed is only
  // used for DCI Format 1-0 (in the DL). Therefore, for the PUSCH this is set to 0.
  pusch_params.tb_scaling_field = 0;
  // As per TS 38.214, Section 6.1.4.2, nof_oh_prb equals xOverhead when configured; otherwise 0.
  pusch_params.nof_oh_prb = ue_cfg.pusch_serving_cell_cfg() != nullptr
                                ? static_cast<unsigned>(ue_cfg.pusch_serving_cell_cfg()->x_ov_head)
                                : static_cast<unsigned>(x_overhead::not_set);
  // If aperiodic CSI is configured, it is assumed that it will be carried by dynamic grants.
  pusch_params.aperiodic_csi     = false;
  pusch_params.nof_harq_ack_bits = uci_alloc.get_scheduled_pdsch_counter_in_ue_uci(pusch_slot, u->crnti);

  // Compute CRBs: CG PUSCH uses non-interleaved VRB-to-PRB mapping, so VRBs = PRBs.
  const bwp_configuration& ul_bwp_cfg    = cell_cfg.params.ul_cfg_common.init_ul_bwp.generic_params;
  const auto&              cg_freq_alloc = std::get<ra_frequency_type1_configuration>(ul_grant.freq_domain_res);
  const auto               cg_vrbs = vrb_interval::start_and_len(cg_freq_alloc.start_vrb, cg_freq_alloc.length_vrb);
  const crb_interval       crbs    = prb_to_crb(ul_bwp_cfg, cg_vrbs.convert_to<prb_interval>());

  // Check for resource collision in the UL grid.
  const grant_info grant{ul_bwp_cfg.scs, pusch_td_cfg.symbols, crbs};
  if (slot_alloc.ul_res_grid.collides(grant)) {
    logger.warning("rnti={}: CG PUSCH at slot={} collides with an existing allocation, skipping", rnti, pusch_slot);
    return false;
  }

  // Check that the PUSCH result list has capacity.
  if (slot_alloc.result.ul.puschs.full()) {
    logger.warning("rnti={}: CG PUSCH cannot be allocated at slot={}: PUSCH list is full", rnti, pusch_slot);
    return false;
  }

  // Compute TBS from the configured MCS and VRB count.
  const sch_mcs_index mcs_idx{ul_grant.mcs};
  // NOTE: the TBS should have been computed to be valid when the UE config was built.
  const units::bytes tbs = compute_ul_tbs_unsafe(pusch_params, mcs_idx, cg_freq_alloc.length_vrb);

  // Fill UL scheduling result.
  ul_sched_info& sched_info = slot_alloc.result.ul.puschs.emplace_back();

  constexpr unsigned rep_idx = 0U;
  build_pusch_cs_rnti(sched_info.pusch_cfg,
                      u->ue_cfg_dedicated()->get_cs_rnti().value(),
                      pusch_params,
                      {mcs_idx, tbs},
                      ue_cfg,
                      ue_cc->active_bwp(),
                      cg_vrbs,
                      rep_to_rv(cg_cfg.rep, rep_idx),
                      h_ul.value().id());

  // Check if there is any UCI grant allocated on the PUCCH that can be moved to the PUSCH.
  // NOTE: aperiodic
  constexpr bool configured_grant = true;
  uci_alloc.multiplex_uci_on_pusch(sched_info, slot_alloc, ue_cfg, pusch_params.aperiodic_csi, configured_grant);

  // Fill decision context (informational; not forwarded to the PHY).
  sched_info.context.ue_index = u->ue_index;
  // Not applicable for CG Type 1 (no PDCCH).
  sched_info.context.ss_id = to_search_space_id(0);
  // Not applicable for CG Type 1 (no PDCCH timing).
  sched_info.context.k2 = 0;
  // With CG, the periodic allocated grants always contained new-tx.
  sched_info.context.nof_retxs  = 0;
  sched_info.context.nof_oh_prb = pusch_params.nof_oh_prb;

  // Mark resources as allocated in the UL resource grid.
  slot_alloc.ul_res_grid.fill(grant);

  logger.debug("CG PUSCH allocated for UE={} for slot={}", rnti, pusch_slot);

  return true;
}
