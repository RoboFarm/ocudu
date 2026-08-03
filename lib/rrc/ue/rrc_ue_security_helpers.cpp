// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "rrc_ue_security_helpers.h"
#include "ocudu/asn1/rrc_nr/nr_ue_variables.h"
#include "ocudu/security/integrity.h"

using namespace ocudu;
using namespace ocucp;

bool ocudu::ocucp::verify_short_mac_i(const security::sec_short_mac_i&  short_mac_i,
                                      pci_t                             source_pci,
                                      rnti_t                            source_c_rnti,
                                      nr_cell_identity                  target_nci,
                                      const security::security_context& sec_context,
                                      rrc_ue_logger&                    logger)
{
  if (!sec_context.sel_algos.algos_selected) {
    logger.log_warning("Cannot verify ShortMAC-I. Cause: no security algorithms selected");
    return false;
  }

  // Get packed varShortMAC-Input.
  asn1::rrc_nr::var_short_mac_input_s var_short_mac_input = {};
  var_short_mac_input.source_pci                          = source_pci;
  var_short_mac_input.target_cell_id.from_number(target_nci.value());
  var_short_mac_input.source_c_rnti = to_value(source_c_rnti);

  byte_buffer   var_short_mac_input_packed = {};
  asn1::bit_ref bref(var_short_mac_input_packed);
  if (var_short_mac_input.pack(bref) != asn1::OCUDUASN_SUCCESS) {
    logger.log_warning("Cannot verify ShortMAC-I. Cause: failed to pack varShortMAC-Input");
    return false;
  }

  logger.log_debug(var_short_mac_input_packed.begin(),
                   var_short_mac_input_packed.end(),
                   "Packed varShortMAC-Input. source_pci={} target_cell_id={} source_c_rnti={}",
                   source_pci,
                   target_nci,
                   source_c_rnti);

  const security::sec_as_config source_as_config = sec_context.get_as_config(security::sec_domain::rrc);
  const bool valid = security::verify_short_mac(short_mac_i, var_short_mac_input_packed, source_as_config);
  logger.log_debug("Verified ShortMAC-I. short_mac_valid={}", valid);

  return valid;
}
