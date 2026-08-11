// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "../mac_dl/bcch_dl_sch_encoder.h"
#include "ocudu/ocudulog/logger.h"
#include "ocudu/support/timers.h"

namespace ocudu {

class mac_scheduler_cell_configurator;

/// \brief Entity that manages the System Information broadcast by a MAC cell.
///
/// It manages the updating of the BCCH-DL-SCH payloads and turns System Information updates into commands to apply in
/// the MAC cell.
class si_message_controller
{
public:
  si_message_controller(du_cell_index_t                  cell_index,
                        const mac_cell_sys_info_config&  sys_info,
                        timer_factory                    timers,
                        mac_scheduler_cell_configurator& sched);
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

  /// \brief Encoder owning the repeat/count timing and content of an on-going PWS (Write-Replace Warning) broadcast
  /// sequence for a single SI-message index.
  ///
  /// Unlike \c static_si_msg_encoder, this object is created once and persists across unrelated SI reconfigurations.
  /// It is mutated in place by \c handle_pws_broadcast rather than being replaced, since it owns a live repeat timer
  /// that must survive across CU-driven Write-Replace Warning content pushes.
  class pws_si_msg_encoder;

  /// Whether the System Information differs from the one the current encoders were built from.
  bool has_si_changed(const mac_cell_sys_info_config& req) const;

  /// Rebuilds the encoders that changed and updates the command to apply.
  void build_command(const mac_cell_sys_info_config& req);

  bool handle_pws_broadcast(const mac_cell_sys_info_pdu_update& req);

  /// \brief Fetches the PWS encoder of the SI message at a given position of an SI scheduling configuration.
  /// \return The encoder, or nullptr if the position does not exist or its SI message carries no PWS SIB.
  std::shared_ptr<pws_si_msg_encoder> find_pws_encoder(const si_scheduling_config& si_sched_cfg,
                                                       unsigned                    si_msg_idx) const;

  ocudulog::basic_logger&          logger;
  du_cell_index_t                  cell_index;
  timer_factory                    timers;
  mac_scheduler_cell_configurator& sched;

  // Last SIB1 payload used to build the current SIB1 encoder.
  byte_buffer last_sib1;
  bool        last_hypersfn_enabled = false;

  // Last SI messages used to build the current SI-message encoders.
  static_vector<bcch_dl_sch_payload_type, MAX_SI_MESSAGES> last_si_messages;

  // Command matching the System Information currently being broadcast.
  si_update_command last_cmd;

  std::shared_ptr<si_message_extension_handler> ext_handler;

  // PWS encoders, one entry per SI message carrying PWS SIBs, keyed by the SI message identity. Keying by identity
  // rather than by position keeps an on-going warning attached to its SIBs when the SI scheduling layout changes.
  std::vector<std::pair<sib_type_set, std::shared_ptr<pws_si_msg_encoder>>> pws_encoders;
};

} // namespace ocudu
