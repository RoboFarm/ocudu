// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "rrc_ue_logger.h"
#include "ocudu/ran/nr_cell_identity.h"
#include "ocudu/ran/pci.h"
#include "ocudu/ran/rnti.h"
#include "ocudu/security/security.h"

namespace ocudu::ocucp {

/// \brief Convert the 16-bit ShortMAC-I/ResumeMAC-I an RRC message carries into the common type, which stores it in
/// network byte order.
inline security::sec_short_mac_i to_short_mac_i(uint16_t value)
{
  return {static_cast<uint8_t>(value >> 8U), static_cast<uint8_t>(value & 0xffU)};
}

/// \brief Verify the ShortMAC-I a UE included in an RRC Reestablishment Request.
///
/// The token is computed over VarShortMAC-Input (TS 38.331 section 5.3.7.4) with the AS keys of the cell the UE came
/// from, so it can only be verified by the node holding those keys. That is the same node for an intra-node
/// reestablishment and the source NG-RAN node for a reestablishment that requires UE context retrieval over Xn
/// (TS 33.501 section 6.11).
///
/// \param[in] short_mac_i The ShortMAC-I received from the UE.
/// \param[in] source_pci PCI of the cell the UE declared a failure on.
/// \param[in] source_c_rnti C-RNTI the UE had in that cell.
/// \param[in] target_nci Identity of the cell the UE is reestablishing on.
/// \param[in] sec_context Security context of the UE in the source cell.
/// \param[in] logger Logger of the UE holding the source AS keys.
/// \return True if the ShortMAC-I matches, false if it does not or if the security context has no selected algorithms.
bool verify_short_mac_i(const security::sec_short_mac_i&  short_mac_i,
                        pci_t                             source_pci,
                        rnti_t                            source_c_rnti,
                        nr_cell_identity                  target_nci,
                        const security::security_context& sec_context,
                        rrc_ue_logger&                    logger);

} // namespace ocudu::ocucp
