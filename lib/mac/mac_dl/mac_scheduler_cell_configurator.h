// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/ran/du_types.h"
#include "ocudu/support/units.h"
#include <optional>

namespace ocudu {

struct du_cell_slice_reconfig_request;
struct sched_cell_ntn_ul_ta_update;
struct si_scheduling_update_request;

/// \brief Interface used to reconfigure a cell that has already been created in the MAC scheduler.
class mac_scheduler_cell_configurator
{
public:
  virtual ~mac_scheduler_cell_configurator() = default;

  /// \brief Update SIB1 and SI scheduling information in scheduler.
  /// \param[in] request Request to change SI sched info and messages.
  /// \remark Must be called from the cell control executor.
  virtual void handle_si_change_indication(const si_scheduling_update_request& request) = 0;

  /// \brief Request the scheduler to broadcast a PWS (ETWS/CMAS) short-message notification and activate the target
  /// SI-message.
  /// \param[in] cell_idx DU-specific index of the cell for which the indication is directed.
  /// \param[in] si_msg_idx Index of the SI-message carrying the SIB6/7/8 to activate.
  /// \param[in] nof_segments Number of segments composing the warning message, or \c std::nullopt for indefinite.
  /// \param[in] msg_len Length, in bytes, of the largest segment of the warning message being activated. Used to
  /// size the PDSCH grant for this SI-message while active.
  /// \remark Must be called from the cell control executor.
  virtual void handle_pws_broadcast_indication(du_cell_index_t         cell_idx,
                                               unsigned                si_msg_idx,
                                               std::optional<unsigned> nof_segments,
                                               units::bytes            msg_len) = 0;

  /// \brief Handle request to update the slice configuration of a cell.
  /// \param[in] req Request to update the RRM policies.
  /// \remark Must be called from the cell DL executor.
  virtual void handle_slice_reconfiguration_request(const du_cell_slice_reconfig_request& req) = 0;

  /// \brief Handle an update of the uplink timing advance at the reference location of an NTN cell.
  /// \param[in] req New reference location T_TA for the cell.
  /// \remark Must be called from the cell DL executor.
  virtual void handle_ntn_ul_ta_update(const sched_cell_ntn_ul_ta_update& req) = 0;
};

} // namespace ocudu
