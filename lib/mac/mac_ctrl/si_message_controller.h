// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "../mac_dl/bcch_dl_sch_encoder.h"
#include "ocudu/ocudulog/logger.h"
#include "ocudu/support/timers.h"

namespace ocudu {

class mac_dl_cell_controller;

/// \brief Entity that manages the System Information broadcast by a MAC cell.
///
/// It manages the updating of the BCCH-DL-SCH payloads and turns System Information updates into commands to apply in
/// the MAC cell.
class si_message_controller
{
public:
  /// \remark Starts the broadcast of the System Information in the cell, so \c dl_cell must be able to receive SI
  /// epochs.
  si_message_controller(du_cell_index_t                 cell_index,
                        const mac_cell_sys_info_config& sys_info,
                        timer_factory                   timers,
                        mac_dl_cell_controller&         dl_cell);
  ~si_message_controller();

  /// Command generated from the last System Information update.
  const si_update_command& last_command() const { return last_cmd; }

  /// Handler used to serve SI PDU updates that bypass the SI modification window.
  std::shared_ptr<si_message_extension_handler> extension_handler() const { return ext_handler; }

  /// \brief Handles an update of the System Information broadcast by the cell.
  /// \return Command to apply in the MAC cell, or \c std::nullopt if the System Information did not change.
  std::optional<si_update_command> handle_si_change_request(const mac_cell_sys_info_config& req);

  /// \brief Handles an SI message PDU update. If \c req.pws_broadcast is set, this is routed to the PWS
  /// (Write-Replace Warning) broadcast content push and repetition sequence; otherwise it is a plain SI PDU update
  /// enqueued at its proper Tx slots.
  bool handle_si_message_pdu_updates(const mac_cell_sys_info_pdu_update& req);

private:
  /// Encoder for a static BCCH-DL-SCH SIB1 payload.
  class sib1_static_encoder;

  /// Encoder for a BCCH-DL-SCH SIB1 payload whose HyperSFN is auto-updated, when eDRX is enabled.
  class sib1_hypersfn_encoder;

  /// \brief Encoder for a static (non-PWS) SI-message, replaced wholesale whenever its content changes.
  class static_si_msg_encoder;

  /// \brief Encoder of the content of a PWS (Write-Replace Warning) broadcast, cycling through its segments.
  ///
  /// Its content is immutable, so a new warning is broadcast by replacing the encoder, which also restarts the
  /// segment cycle.
  class pws_si_msg_encoder;

  /// \brief Repeat/count sequence of the PWS (Write-Replace Warning) broadcasts of one SI message.
  ///
  /// Unlike the encoders, it persists across unrelated SI reconfigurations, since it owns a live repeat timer that
  /// must survive across CU-driven Write-Replace Warning content pushes.
  class pws_broadcast_sequence;

  /// Whether the System Information differs from the one the current encoders were built from.
  bool has_si_changed(const mac_cell_sys_info_config& req) const;

  /// Rebuilds the encoders that changed and updates the command to apply.
  void build_command(const mac_cell_sys_info_config& req);

  bool handle_pws_broadcast(const mac_cell_sys_info_pdu_update& req);

  /// \brief Derives the ETWS/CMAS SI epoch from the current one and applies it in the cell.
  /// \param new_broadcast Whether a new broadcast of the warnings is starting.
  void push_pws_epoch(bool new_broadcast);

  /// \brief Fetches the PWS broadcast sequence of the SI message at a given position of an SI scheduling config.
  /// \return The sequence, or nullptr if the position does not exist or its SI message carries no PWS SIB.
  pws_broadcast_sequence* find_pws_sequence(const si_scheduling_config& si_sched_cfg, unsigned si_msg_idx) const;

  ocudulog::basic_logger& logger;
  du_cell_index_t         cell_index;
  timer_factory           timers;
  mac_dl_cell_controller& dl_cell;

  // Last SIB1 payload used to build the current SIB1 encoder.
  byte_buffer last_sib1;
  bool        last_hypersfn_enabled = false;

  // Last SI messages used to build the current SI-message encoders.
  static_vector<bcch_dl_sch_payload_type, MAX_SI_MESSAGES> last_si_messages;

  // Last SI epoch handed out. Both the normal operation and the ETWS/CMAS epochs draw from it, so that a version
  // identifies an epoch on its own.
  si_version_type last_version = 0;

  // Command matching the System Information currently being broadcast.
  si_update_command last_cmd;

  // Index of the SI message whose warning content was just pushed, while its epoch is being derived.
  std::optional<unsigned> pending_content_update_idx;

  // SI messages that are currently carrying a warning.
  static_vector<pws_broadcasting_si_message, MAX_PWS_SI_MESSAGES> broadcasting_warnings;

  std::shared_ptr<si_message_extension_handler> ext_handler;

  // PWS broadcast sequences, one entry per SI message carrying PWS SIBs, keyed by the SI message identity. Keying by
  // identity rather than by position keeps an on-going warning attached to its SIBs when the SI scheduling layout
  // changes.
  std::vector<std::pair<sib_type_set, std::unique_ptr<pws_broadcast_sequence>>> pws_sequences;
};

} // namespace ocudu
