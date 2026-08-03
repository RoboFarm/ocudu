// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ue_context_retrieval_helpers.h"
#include "ocudu/asn1/rrc_nr/rrc_nr.h"

using namespace ocudu;
using namespace ocucp;

/// Extracts the SSB ARFCN of a cell from the MeasurementTimingConfiguration the peer advertised for it at XN setup.
/// This is the same value the peer's RRC uses when it derives keys for that cell, so both nodes end up with the same
/// KgNB*.
static std::optional<uint32_t> get_ssb_arfcn(const byte_buffer& packed_meas_timing_cfg)
{
  if (packed_meas_timing_cfg.empty()) {
    return std::nullopt;
  }

  asn1::rrc_nr::meas_timing_cfg_s meas_timing_cfg;
  asn1::cbit_ref                  bref{packed_meas_timing_cfg};
  if (meas_timing_cfg.unpack(bref) != asn1::OCUDUASN_SUCCESS) {
    return std::nullopt;
  }

  if (meas_timing_cfg.crit_exts.type() != asn1::rrc_nr::meas_timing_cfg_s::crit_exts_c_::types_opts::c1 ||
      meas_timing_cfg.crit_exts.c1().type() !=
          asn1::rrc_nr::meas_timing_cfg_s::crit_exts_c_::c1_c_::types_opts::meas_timing_conf) {
    return std::nullopt;
  }

  const auto& meas_timing_list = meas_timing_cfg.crit_exts.c1().meas_timing_conf().meas_timing;
  for (const auto& meas_timing : meas_timing_list) {
    if (meas_timing.freq_and_timing_present) {
      return meas_timing.freq_and_timing.carrier_freq;
    }
  }

  return std::nullopt;
}

/// Verifies the token the UE computed with its source AS keys. Only the RRC Reestablishment identity is supported, as
/// the RRC Resume identity requires verifying a ResumeMAC-I instead.
static bool verify_mac_i(const xnap_retrieve_ue_context_request& request, cu_cp_ue& ue, ocudulog::basic_logger& logger)
{
  if (!std::holds_alternative<xnap_ue_context_id_for_rrc_reest>(request.ue_context_id)) {
    logger.info("ue={}: Rejecting UE context retrieval. Cause: only the RRC Reestablishment UE Context ID is supported",
                request.ue_index);
    return false;
  }

  const auto& reest_id = std::get<xnap_ue_context_id_for_rrc_reest>(request.ue_context_id);

  return ue.get_rrc_ue()->get_rrc_ue_control_message_handler().verify_reestablishment_short_mac_i(
      request.mac_i, reest_id.fail_cell_pci, reest_id.c_rnti, request.target_nci);
}

xnap_retrieve_ue_context_response
ocudu::ocucp::collect_ue_context_for_retrieval(const xnap_retrieve_ue_context_request& request,
                                               cu_cp_ue&                               ue,
                                               const guami_t&                          guami,
                                               amf_ue_id_t                             amf_ue_id,
                                               ocudulog::basic_logger&                 logger)
{
  xnap_retrieve_ue_context_response response;

  if (!verify_mac_i(request, ue, logger)) {
    logger.info("ue={}: Rejecting UE context retrieval. Cause: MAC-I verification failed", request.ue_index);
    response.cause = xnap_cause_radio_network_t::unspecified;
    return response;
  }

  // Only a fully attached UE has a context worth transferring, matching the criteria of a local reestablishment.
  const auto& pdu_sessions = ue.get_up_resource_manager().get_pdu_sessions_map();
  if (pdu_sessions.empty()) {
    logger.info("ue={}: Rejecting UE context retrieval. Cause: UE has no PDU sessions", request.ue_index);
    response.cause = xnap_cause_radio_network_t::non_relocation_of_context;
    return response;
  }

  if (amf_ue_id == amf_ue_id_t::invalid) {
    logger.info("ue={}: Rejecting UE context retrieval. Cause: UE has invalid AMF UE ID", request.ue_index);
    response.cause = xnap_cause_radio_network_t::non_relocation_of_context;
    return response;
  }

  // Derive KgNB* for the target cell (TS 33.501 section 6.11). Deriving on a copy keeps the keys of the local UE
  // intact, as it stays in service until the target confirms the retrieval.
  if (!request.target_cell.has_value()) {
    logger.info("ue={}: Rejecting UE context retrieval. Cause: target cell not served by the peer. nci={}",
                request.ue_index,
                request.target_nci);
    response.cause = xnap_cause_radio_network_t::cell_not_available;
    return response;
  }

  const std::optional<uint32_t> target_ssb_arfcn = get_ssb_arfcn(request.target_cell->meas_timing_cfg);
  if (!target_ssb_arfcn.has_value()) {
    logger.info("ue={}: Rejecting UE context retrieval. Cause: unknown SSB ARFCN of the target cell. nci={}",
                request.ue_index,
                request.target_nci);
    response.cause = xnap_cause_radio_network_t::cell_not_available;
    return response;
  }

  security::security_context target_sec_context = ue.get_security_manager().get_security_context();
  target_sec_context.horizontal_key_derivation(request.target_cell->nr_pci, target_ssb_arfcn.value());
  logger.debug("ue={}: Derived KgNB* for the target cell. pci={} ssb-arfcn={}",
               request.ue_index,
               request.target_cell->nr_pci,
               target_ssb_arfcn.value());

  auto& ue_context_info     = response.ue_context_info;
  ue_context_info.amf_ue_id = amf_ue_id_to_uint(amf_ue_id);
  // TODO: Fill the correct AMF address.
  ue_context_info.amf_addr         = transport_layer_address::create_from_string("127.0.0.1");
  ue_context_info.security_context = target_sec_context;
  ue_context_info.ue_ambr          = ue.get_ue_ambr();

  for (const auto& [pdu_session_id, pdu_session_ctxt] : pdu_sessions) {
    cu_cp_pdu_session_res_setup_item pdu_session_item;
    pdu_session_item.pdu_session_id = pdu_session_id;
    // TODO: move PDU session specific members to the PDU session context. For now the PDU session specific
    // information is extracted from the first DRB of the PDU session context.
    pdu_session_item.s_nssai            = pdu_session_ctxt.drbs.begin()->second.s_nssai;
    pdu_session_item.ul_ngu_up_tnl_info = pdu_session_ctxt.ul_ngu_up_tnl_info;
    pdu_session_item.pdu_session_type   = pdu_session_ctxt.type;

    for (const auto& [drb_id, drb_ctxt] : pdu_session_ctxt.drbs) {
      for (const auto& [qfi, qos_flow] : drb_ctxt.qos_flows) {
        qos_flow_setup_request_item qos_flow_setup_item = {};
        qos_flow_setup_item.qos_flow_id                 = qfi;
        qos_flow_setup_item.qos_flow_level_qos_params   = qos_flow.qos_params;
        pdu_session_item.qos_flow_setup_request_items.emplace(qfi, qos_flow_setup_item);
      }
    }

    ue_context_info.pdu_session_res_to_be_setup_list.emplace(pdu_session_id, pdu_session_item);
  }

  ue_context_info.rrc_context =
      ue.get_rrc_ue()->get_rrc_ue_control_message_handler().get_packed_handover_preparation_message();

  response.guami   = guami;
  response.success = true;

  return response;
}
