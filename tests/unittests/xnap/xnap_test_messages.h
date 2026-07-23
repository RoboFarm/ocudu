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

/// \brief Generate a dummy Handover Request message.
xnap_message generate_handover_request(local_xnap_ue_id_t local_xnap_ue_id);

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
