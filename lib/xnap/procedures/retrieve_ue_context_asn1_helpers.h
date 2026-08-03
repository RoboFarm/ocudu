// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "../xnap_asn1_converters.h"
#include "ocudu/asn1/asn1_utils.h"
#include "ocudu/asn1/xnap/xnap_pdu_contents.h"
#include "ocudu/support/io/transport_layer_address.h"
#include "ocudu/xnap/xnap_ue_context_retrieval.h"
#include <variant>

namespace ocudu::ocucp {

/// \brief Convert an ASN.1 MAC-I to common type.
inline security::sec_short_mac_i asn1_to_mac_i(const asn1::fixed_bitstring<16, false, true>& asn1_mac_i)
{
  const auto value = static_cast<uint16_t>(asn1_mac_i.to_number());
  return {static_cast<uint8_t>(value >> 8U), static_cast<uint8_t>(value & 0xffU)};
}

/// \brief Convert an ASN.1 UE Context ID to common type (TS 38.423 section 9.2.3.40).
/// \returns True if the conversion was successful, false if the UE Context ID identifies an E-UTRA cell or uses an
/// unsupported extension.
inline bool asn1_to_ue_context_id(xnap_ue_context_id&                ue_context_id,
                                  const asn1::xnap::ue_context_id_c& asn1_ue_context_id)
{
  using asn1_types = asn1::xnap::ue_context_id_c::types_opts;

  if (asn1_ue_context_id.type() == asn1_types::rrc_resume) {
    const auto& asn1_resume = asn1_ue_context_id.rrc_resume();
    if (asn1_resume.access_pci.type() != asn1::xnap::ng_ran_cell_pci_c::types_opts::nr) {
      return false;
    }

    xnap_ue_context_id_for_rrc_resume resume_id;
    if (asn1_resume.i_rnti.type() == asn1::xnap::i_rnti_c::types_opts::i_rnti_short) {
      // The I-RNTI carries the profile it was composed with (TS 38.300 Annex F), so the node identifier and the UE
      // reference are recovered from the value itself.
      auto short_i_rnti = short_i_rnti_t::from_uint(asn1_resume.i_rnti.i_rnti_short().to_number());
      if (!short_i_rnti.has_value()) {
        return false;
      }
      resume_id.i_rnti = short_i_rnti.value();
    } else if (asn1_resume.i_rnti.type() == asn1::xnap::i_rnti_c::types_opts::i_rnti_full) {
      auto full_i_rnti = full_i_rnti_t::from_uint(asn1_resume.i_rnti.i_rnti_full().to_number());
      if (!full_i_rnti.has_value()) {
        return false;
      }
      resume_id.i_rnti = full_i_rnti.value();
    } else {
      return false;
    }
    resume_id.allocated_c_rnti = to_rnti(asn1_resume.allocated_c_rnti.to_number());
    resume_id.access_pci       = asn1_resume.access_pci.nr();

    ue_context_id = resume_id;
    return true;
  }

  if (asn1_ue_context_id.type() == asn1_types::rrrc_reest) {
    const auto& asn1_reest = asn1_ue_context_id.rrrc_reest();
    if (asn1_reest.fail_cell_pci.type() != asn1::xnap::ng_ran_cell_pci_c::types_opts::nr) {
      return false;
    }

    ue_context_id = xnap_ue_context_id_for_rrc_reest{.c_rnti        = to_rnti(asn1_reest.c_rnti.to_number()),
                                                     .fail_cell_pci = asn1_reest.fail_cell_pci.nr()};
    return true;
  }

  return false;
}

/// \brief Convert an ASN.1 Retrieve UE Context Request to common type (TS 38.423 section 9.1.1.8).
/// \returns True if the conversion was successful, false otherwise.
inline bool asn1_to_retrieve_ue_context_request(xnap_retrieve_ue_context_request&                request,
                                                const asn1::xnap::retrieve_ue_context_request_s& asn1_request)
{
  if (!asn1_to_ue_context_id(request.ue_context_id, asn1_request->ue_context_id)) {
    return false;
  }

  request.mac_i = asn1_to_mac_i(asn1_request->mac_i);

  if (asn1_request->new_ng_ran_cell_id.type() != asn1::xnap::ng_ran_cell_id_c::types_opts::nr) {
    return false;
  }
  auto target_nci = nr_cell_identity::create(asn1_request->new_ng_ran_cell_id.nr().to_number());
  if (!target_nci.has_value()) {
    return false;
  }
  request.target_nci = target_nci.value();

  if (asn1_request->rrc_resume_cause_present) {
    request.rrc_resume_cause = resume_cause_t::rna_upd;
  }

  return true;
}

/// \brief Convert a common type Retrieve UE Context Response to ASN.1 (TS 38.423 section 9.1.1.9).
/// \remark The NG-RAN node UE XnAP IDs are set by the procedure, which owns the local XNAP UE ID.
inline void retrieve_ue_context_response_to_asn1(asn1::xnap::retrieve_ue_context_resp_s&  asn1_response,
                                                 const xnap_retrieve_ue_context_response& response)
{
  asn1_response->guami = guami_to_asn1(response.guami);

  const auto& ue_context_info      = response.ue_context_info;
  auto&       asn1_ue_context_info = asn1_response->ue_context_info_retr_ue_ctxt_resp;

  // Fill NG-C UE associated signalling reference.
  asn1_ue_context_info.ng_c_ue_sig_ref = ue_context_info.amf_ue_id;
  // Fill the signalling TNL association address at the old NG-C side (AMF address).
  asn1_ue_context_info.sig_tnl_at_source.set_endpoint_ip_address();
  tla_to_asn1_bitstring(asn1_ue_context_info.sig_tnl_at_source.endpoint_ip_address(), ue_context_info.amf_addr);
  // Fill UE security capabilities.
  asn1_ue_context_info.ue_security_cap.nr_encyption_algorithms =
      security::supported_algorithms_to_asn1(ue_context_info.security_context.supported_enc_algos);
  asn1_ue_context_info.ue_security_cap.nr_integrity_protection_algorithms =
      security::supported_algorithms_to_asn1(ue_context_info.security_context.supported_int_algos);
  // Fill AS security information. The key is the KgNB* the old NG-RAN node derived for the target cell.
  asn1_ue_context_info.security_info.key_ng_ran_star = security::key_to_asn1(ue_context_info.security_context.k);
  asn1_ue_context_info.security_info.ncc             = ue_context_info.security_context.ncc;
  // Fill UE aggregate maximum bit rate.
  asn1_ue_context_info.ue_ambr.dl_ue_ambr = ue_context_info.ue_ambr.dl;
  asn1_ue_context_info.ue_ambr.ul_ue_ambr = ue_context_info.ue_ambr.ul;

  // Fill PDU session resource to be setup list.
  for (const auto& pdu_session_item : ue_context_info.pdu_session_res_to_be_setup_list) {
    asn1::xnap::pdu_session_res_to_be_setup_item_s asn1_pdu_session_item;
    asn1_pdu_session_item.pdu_session_id = pdu_session_id_to_uint(pdu_session_item.pdu_session_id);
    asn1_pdu_session_item.s_nssai        = s_nssai_to_asn1(pdu_session_item.s_nssai);
    up_transport_layer_info_to_asn1(asn1_pdu_session_item.ul_ng_u_tnl_at_up_f, pdu_session_item.ul_ngu_up_tnl_info);
    asn1_pdu_session_item.pdu_session_type = pdu_session_type_to_asn1(pdu_session_item.pdu_session_type);

    for (const auto& qos_flow : pdu_session_item.qos_flow_setup_request_items) {
      asn1::xnap::qos_flows_to_be_setup_item_s asn1_qos_flow_item;
      asn1_qos_flow_item.qfi = qos_flow_id_to_uint(qos_flow.qos_flow_id);
      asn1_qos_flow_item.qos_flow_level_qos_params =
          qos_flow_level_qos_parameters_to_asn1(qos_flow.qos_flow_level_qos_params);
      asn1_pdu_session_item.qos_flows_to_be_setup_list.push_back(asn1_qos_flow_item);
    }

    asn1_ue_context_info.pdu_session_res_to_be_setup_list.push_back(asn1_pdu_session_item);
  }

  // Fill RRC container (containing HandoverPreparationInformation).
  asn1_ue_context_info.rrc_context = ue_context_info.rrc_context.copy();
}

/// \brief Fill an ASN.1 Retrieve UE Context Failure with the given cause (TS 38.423 section 9.1.1.10).
/// \remark The New NG-RAN node UE XnAP ID is set by the procedure, which owns the peer XNAP UE ID.
inline void retrieve_ue_context_failure_to_asn1(asn1::xnap::retrieve_ue_context_fail_s& asn1_failure,
                                                const xnap_cause_t&                     cause)
{
  asn1_failure->cause = cause_to_asn1(cause);
}

} // namespace ocudu::ocucp
