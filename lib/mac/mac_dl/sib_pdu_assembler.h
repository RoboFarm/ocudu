// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "bcch_dl_sch_encoder.h"
#include "ocudu/adt/lockfree_triple_buffer.h"
#include "ocudu/ocudulog/logger.h"

namespace ocudu {

/// Entity responsible for fetching encoded SIB1 and SI messages based on scheduled SI grants.
class sib_pdu_assembler
{
public:
  sib_pdu_assembler();

  /// \brief Starts the broadcast of the cell System Information.
  /// \param[in] ext_handler Handler used to serve SI PDU updates that bypass the SI modification window.
  /// \param[in] first_cmd First SI epoch of the cell.
  void start_broadcast(std::shared_ptr<si_message_extension_handler> ext_handler, const si_update_command& first_cmd);

  /// Applies a new SI epoch, to be used for the SI grants stamped with its version.
  void handle_si_update(const si_update_command& cmd);

  /// \brief Applies a new ETWS/CMAS SI epoch, to be used for the SI grants stamped with its version.
  ///
  /// It coexists with the SI epoch of the normal operation, which the scheduler goes back to once the warning stops
  /// being broadcast.
  void handle_pws_si_update(const si_update_command& cmd);

  /// \brief Retrieve the encoded SI message.
  /// \note Called from RT path, so it must be lock-free and non-blocking.
  span<const uint8_t> encode_si_pdu(slot_point_extended sl_tx, const sib_information& si_info);

private:
  /// A snapshot of the SIB1 and SI message encoders within a given SI change window.
  struct si_encoder_snapshot {
    si_version_type                                                          version = 0;
    std::shared_ptr<bcch_dl_sch_msg_encoder>                                 sib1;
    static_vector<std::shared_ptr<bcch_dl_sch_msg_encoder>, MAX_SI_MESSAGES> si_msgs;
  };

  ocudulog::basic_logger& logger;

  std::shared_ptr<si_message_extension_handler> ext_handler;

  /// Selects the encoders that a given SI grant was scheduled with, refreshing them if they are not held yet.
  const si_encoder_snapshot& select_snapshot(si_version_type version);

  // Encoders being transferred from the configuration plane to the assembler RT path, one per SI epoch channel.
  lockfree_triple_buffer<si_encoder_snapshot> pending;
  lockfree_triple_buffer<si_encoder_snapshot> pending_pws;

  // Encoders that are being currently used to generate the PDUs sent to lower layers. Both channels are kept, given
  // that the scheduler alternates between them for as long as a warning is being broadcast.
  // Note: These members are only accessed from the RT path.
  si_encoder_snapshot current;
  si_encoder_snapshot current_pws;
};

} // namespace ocudu
