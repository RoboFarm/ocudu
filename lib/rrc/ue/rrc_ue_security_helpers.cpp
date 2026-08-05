// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "rrc_ue_security_helpers.h"
#include "ocudu/asn1/rrc_nr/nr_ue_variables.h"
#include "ocudu/security/integrity.h"

using namespace ocudu;
using namespace ocucp;

namespace {

/// Verifies a MAC-I computed over one of the VarShortMAC-Input / VarResumeMAC-Input variables, which carry the same
/// three fields and differ only in the ASN.1 type they pack as.
template <typename VarMacInput>
bool verify_mac_i(const char*                       mac_i_type,
                  const security::sec_short_mac_i&  mac_i,
                  pci_t                             source_pci,
                  rnti_t                            source_c_rnti,
                  nr_cell_identity                  target_nci,
                  const security::security_context& sec_context,
                  rrc_ue_logger&                    logger)
{
  if (!sec_context.sel_algos.algos_selected) {
    logger.log_warning("Cannot verify {}. Cause: no security algorithms selected", mac_i_type);
    return false;
  }

  VarMacInput var_mac_input = {};
  var_mac_input.source_pci  = source_pci;
  var_mac_input.target_cell_id.from_number(target_nci.value());
  var_mac_input.source_c_rnti = to_value(source_c_rnti);

  byte_buffer   var_mac_input_packed = {};
  asn1::bit_ref bref(var_mac_input_packed);
  if (var_mac_input.pack(bref) != asn1::OCUDUASN_SUCCESS) {
    logger.log_warning("Cannot verify {}. Cause: failed to pack the MAC-I input variable", mac_i_type);
    return false;
  }

  logger.log_debug(var_mac_input_packed.begin(),
                   var_mac_input_packed.end(),
                   "Packed {} input. source_pci={} target_cell_id={} source_c_rnti={}",
                   mac_i_type,
                   source_pci,
                   target_nci,
                   source_c_rnti);

  const security::sec_as_config source_as_config = sec_context.get_as_config(security::sec_domain::rrc);
  const bool                    valid = security::verify_short_mac(mac_i, var_mac_input_packed, source_as_config);
  logger.log_debug("Verified {}. valid={}", mac_i_type, valid);

  return valid;
}

} // namespace

bool ocudu::ocucp::verify_short_mac_i(const security::sec_short_mac_i&  short_mac_i,
                                      pci_t                             source_pci,
                                      rnti_t                            source_c_rnti,
                                      nr_cell_identity                  target_nci,
                                      const security::security_context& sec_context,
                                      rrc_ue_logger&                    logger)
{
  return verify_mac_i<asn1::rrc_nr::var_short_mac_input_s>(
      "ShortMAC-I", short_mac_i, source_pci, source_c_rnti, target_nci, sec_context, logger);
}

bool ocudu::ocucp::verify_resume_mac_i(const security::sec_short_mac_i&  resume_mac_i,
                                       pci_t                             source_pci,
                                       rnti_t                            source_c_rnti,
                                       nr_cell_identity                  target_nci,
                                       const security::security_context& sec_context,
                                       rrc_ue_logger&                    logger)
{
  return verify_mac_i<asn1::rrc_nr::var_resume_mac_input_s>(
      "ResumeMAC-I", resume_mac_i, source_pci, source_c_rnti, target_nci, sec_context, logger);
}
