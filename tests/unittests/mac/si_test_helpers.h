// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "lib/mac/mac_ctrl/si_message_controller.h"
#include "lib/mac/mac_dl/sib_pdu_assembler.h"
#include "mac_test_helpers.h"
#include "tests/test_doubles/utils/test_rng.h"
#include "ocudu/asn1/rrc_nr/bcch_dl_sch_msg.h"
#include "ocudu/asn1/rrc_nr/sys_info.h"
#include "ocudu/support/executors/manual_task_worker.h"

namespace ocudu {
namespace test_helpers {

inline byte_buffer make_random_pdu(unsigned size = test_rng::uniform_int<unsigned>(10, 200))
{
  return byte_buffer::create(test_rng::vector_of_uniform_ints<uint8_t>(size)).value();
}

inline std::vector<byte_buffer> make_random_segmented_pdu(unsigned segment_size = test_rng::uniform_int<unsigned>(10,
                                                                                                                  200),
                                                          unsigned nof_segments = test_rng::uniform_int<unsigned>(2, 3))
{
  std::vector<byte_buffer> segmented_pdu;
  for (unsigned i_segment = 0; i_segment != nof_segments; ++i_segment) {
    segmented_pdu.emplace_back(make_random_pdu(segment_size));
  }
  return segmented_pdu;
}

/// \brief Packs a BCCH-DL-SCH message carrying a SIB1 that schedules one SI message per given SIB set.
///
/// Only the fields the SI message controller reads back are filled, so that the payload round-trips through the ASN.1
/// unpacking that a si-BroadcastStatus update requires.
inline byte_buffer make_sib1_with_si_sched_info(span<const sib_type> si_msg_sibs)
{
  asn1::rrc_nr::bcch_dl_sch_msg_s msg;
  asn1::rrc_nr::sib1_s&           sib1 = msg.msg.set_c1().set_sib_type1();

  sib1.cell_access_related_info.plmn_id_info_list.resize(1);
  auto& plmn_info = sib1.cell_access_related_info.plmn_id_info_list[0];
  plmn_info.plmn_id_list.resize(1);
  auto& plmn       = plmn_info.plmn_id_list[0];
  plmn.mcc_present = true;
  plmn.mcc         = {0, 0, 1};
  plmn.mnc.resize(2);
  plmn.mnc[0]           = 0;
  plmn.mnc[1]           = 1;
  plmn_info.tac_present = false;
  plmn_info.cell_id.from_number(0x19b0);
  plmn_info.cell_reserved_for_oper.value = asn1::rrc_nr::plmn_id_info_s::cell_reserved_for_oper_opts::not_reserved;

  sib1.si_sched_info_present          = true;
  sib1.si_sched_info.si_win_len.value = asn1::rrc_nr::si_sched_info_s::si_win_len_opts::s20;
  sib1.si_sched_info.sched_info_list.resize(si_msg_sibs.size());
  for (unsigned i = 0, e = si_msg_sibs.size(); i != e; ++i) {
    const sib_type sib        = si_msg_sibs[i];
    auto&          sched_info = sib1.si_sched_info.sched_info_list[i];
    // Mirrors the DU packer: a PWS SI message is listed as dormant until a warning is on air.
    sched_info.si_broadcast_status.value = is_pws_sib(sib)
                                               ? asn1::rrc_nr::sched_info_s::si_broadcast_status_opts::not_broadcasting
                                               : asn1::rrc_nr::sched_info_s::si_broadcast_status_opts::broadcasting;
    sched_info.si_periodicity.value      = asn1::rrc_nr::sched_info_s::si_periodicity_opts::rf16;
    sched_info.sib_map_info.resize(1);
    auto& sib_info = sched_info.sib_map_info[0];
    switch (sib) {
      case sib_type::sib7:
        sib_info.type.value = asn1::rrc_nr::sib_type_info_s::type_opts::sib_type7;
        break;
      case sib_type::sib8:
        sib_info.type.value = asn1::rrc_nr::sib_type_info_s::type_opts::sib_type8;
        break;
      default:
        sib_info.type.value = asn1::rrc_nr::sib_type_info_s::type_opts::sib_type2;
        break;
    }
  }

  byte_buffer   buf;
  asn1::bit_ref bref{buf};
  report_fatal_error_if_not(msg.pack(bref) == asn1::OCUDUASN_SUCCESS, "Failed to pack the test SIB1");
  return buf;
}

/// Returns the si-BroadcastStatus of each SI message listed in a packed BCCH-DL-SCH SIB1 payload.
inline std::vector<bool> get_si_broadcast_status(span<const uint8_t> sib1_pdu)
{
  byte_buffer                     sib1_buf = byte_buffer::create(sib1_pdu).value();
  asn1::rrc_nr::bcch_dl_sch_msg_s msg;
  asn1::cbit_ref                  bref{sib1_buf};
  report_fatal_error_if_not(msg.unpack(bref) == asn1::OCUDUASN_SUCCESS, "Failed to unpack the SIB1");

  std::vector<bool> broadcasting;
  for (const auto& sched_info : msg.msg.c1().sib_type1().si_sched_info.sched_info_list) {
    broadcasting.push_back(sched_info.si_broadcast_status.value ==
                           asn1::rrc_nr::sched_info_s::si_broadcast_status_opts::broadcasting);
  }
  return broadcasting;
}

inline byte_buffer make_pdu_with_padding(const byte_buffer& payload, units::bytes tbs)
{
  byte_buffer          result = payload.deep_copy().value();
  std::vector<uint8_t> zeros(tbs.value() - result.length(), 0);
  report_fatal_error_if_not(result.append(zeros), "Failed appending zeros");
  return result;
}

inline sib_information make_sib_pdu(std::optional<unsigned> si_msg_index, si_version_type si_version, units::bytes tbs)
{
  sib_information result{};
  result.si_indicator  = si_msg_index.has_value() ? sib_information::other_si : sib_information::sib1;
  result.si_msg_index  = si_msg_index;
  result.version       = si_version;
  result.is_repetition = false;
  result.pdsch_cfg.codewords.emplace_back();
  result.pdsch_cfg.codewords[0].tb_size_bytes = tbs;
  return result;
}

/// \brief Bench pairing an SI message controller with the SIB assembler it feeds.
///
/// The encoders are owned by the controller, so the assembler can only be exercised through the commands the
/// controller emits.
class si_bench
{
public:
  explicit si_bench(mac_cell_sys_info_config sys_info_cfg_) :
    sys_info_cfg(std::move(sys_info_cfg_)),
    si_mng(to_du_cell_index(0), sys_info_cfg, timer_factory{timers, task_worker}, sched)
  {
    assembler.start_broadcast(si_mng.extension_handler(), si_mng.last_command());
  }

  /// Forwards a new System Information config to the controller and applies the resulting command, if any.
  std::optional<si_update_command> update_si(const mac_cell_sys_info_config& req)
  {
    std::optional<si_update_command> cmd = si_mng.handle_si_change_request(req);
    if (cmd.has_value()) {
      assembler.handle_si_update(*cmd);
    }
    return cmd;
  }

  /// \brief Builds an SI epoch out of a separate SI configuration and applies it as the ETWS/CMAS one.
  ///
  /// Stands in for the warning snapshot that the SI message controller will derive from the baseline one, so that
  /// the assembler can be exercised with two coexisting epochs.
  si_update_command apply_etws_si(const mac_cell_sys_info_config& etws_cfg, si_version_type version)
  {
    etws_si_mng = std::make_unique<si_message_controller>(
        to_du_cell_index(0), etws_cfg, timer_factory{timers, task_worker}, sched);

    si_update_command cmd = etws_si_mng->last_command();
    cmd.version           = version;
    assembler.handle_etws_si_update(cmd);
    return cmd;
  }

  /// \brief Applies the ETWS/CMAS epoch the controller generated, if any.
  /// \return The applied epoch, whose version the warning grants are stamped with.
  std::optional<si_update_command> apply_pending_etws_si()
  {
    std::optional<si_update_command> cmd = si_mng.take_etws_command();
    if (cmd.has_value()) {
      assembler.handle_etws_si_update(*cmd);
      last_etws_cmd = cmd;
    }
    return cmd;
  }

  /// \brief Properties of the SI message the last generated ETWS/CMAS epoch lists as broadcasting.
  /// \remark The epoch must list exactly one.
  etws_broadcasting_si_message only_broadcasting_si_message() const
  {
    report_fatal_error_if_not(last_etws_cmd.has_value() and last_etws_cmd->broadcasting.size() == 1,
                              "Expected exactly one SI message broadcasting a warning");
    return last_etws_cmd->broadcasting[0];
  }

  /// Advances the timers by the given number of milliseconds, running any dispatched task.
  void tick(unsigned nof_ticks)
  {
    for (unsigned t = 0; t != nof_ticks; ++t) {
      timers.tick();
      task_worker.run_pending_tasks();
    }
  }

  manual_task_worker          task_worker{128};
  timer_manager               timers;
  dummy_mac_scheduler_adapter sched;

  mac_cell_sys_info_config sys_info_cfg;
  si_message_controller    si_mng;
  sib_pdu_assembler        assembler;

  // Stands in for the SI message controller-side production of the ETWS/CMAS epoch.
  std::unique_ptr<si_message_controller> etws_si_mng;

  // Last ETWS/CMAS epoch applied through apply_pending_etws_si.
  std::optional<si_update_command> last_etws_cmd;

  slot_point_extended current_slot{subcarrier_spacing::kHz30, 0};
};

} // namespace test_helpers
} // namespace ocudu
