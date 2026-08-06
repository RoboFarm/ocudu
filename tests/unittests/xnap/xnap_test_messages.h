// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/ran/rb_id.h"
#include "ocudu/xnap/xnap.h"
#include "ocudu/xnap/xnap_message.h"
#include "ocudu/xnap/xnap_types.h"
#include <vector>

namespace ocudu::ocucp {

/// \brief Generate the information of an NR cell an NG-RAN node serves, as advertised at XN setup.
cu_cp_served_cell_info generate_served_cell_info(pci_t pci, const nr_cell_global_id_t& cgi, tac_t tac = 7);

/// \brief Generate a dummy Handover Request message. \c include_drb_to_qos_flow_mapping controls whether the
/// source's DRB-to-QoS-flow mapping (DRB1 <-> QFI1, matching the admitted PDU session) is included via the Data
/// Forwarding and Offloading Info from source NG-RAN node IE, letting the target confirm and prefer DRB1's
/// numbering (TS 38.423 Section 9.2.1.17). Pass false to simulate a source that didn't signal it.
/// \c include_as_config_drb_mapping reports the same mapping through AS-Config in the RRC container instead
/// (TS 38.331 Section 11.2.3), as this node does; use it to simulate a source that only signals it that way.
xnap_message generate_handover_request(local_xnap_ue_id_t local_xnap_ue_id,
                                       bool               include_drb_to_qos_flow_mapping = true,
                                       bool               include_as_config_drb_mapping   = false);

/// \brief Generate a dummy Handover Preparation Failure message.
xnap_message generate_handover_preparation_failure(peer_xnap_ue_id_t peer_xnap_ue_id);

/// \brief Generate a dummy Handover Request Ack message.
xnap_message generate_handover_request_ack(local_xnap_ue_id_t local_xnap_ue_id, peer_xnap_ue_id_t peer_xnap_ue_id);

/// \brief Generate a dummy SN RAN Status Transfer message. \c extra_drb_ids adds further DRBs to the DRBs Subject to
/// Status Transfer List IE, e.g. to simulate a source DRB that was not admitted at the target.
xnap_message generate_sn_status_transfer(local_xnap_ue_id_t           local_xnap_ue_id,
                                         peer_xnap_ue_id_t            peer_xnap_ue_id,
                                         const std::vector<drb_id_t>& extra_drb_ids = {});

/// \brief Generate a dummy UE Context Release message.
xnap_message generate_ue_context_release(local_xnap_ue_id_t local_xnap_ue_id, peer_xnap_ue_id_t peer_xnap_ue_id);

} // namespace ocudu::ocucp
