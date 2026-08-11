// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "sib_pdu_assembler.h"
#include "ocudu/ocudulog/ocudulog.h"

using namespace ocudu;

/// Payload of zeros sent to lower layers when an error occurs.
static const std::vector<uint8_t> zeros_payload(MAX_BCCH_DL_SCH_PDU_SIZE, 0);

sib_pdu_assembler::sib_pdu_assembler() : logger(ocudulog::fetch_basic_logger("MAC")) {}

void sib_pdu_assembler::set_extension_handler(std::shared_ptr<si_message_extension_handler> handler)
{
  ocudu_assert(ext_handler == nullptr, "The SI message extension handler cannot be set more than once");
  ext_handler = std::move(handler);
}

void sib_pdu_assembler::handle_si_update(const si_update_command& cmd)
{
  pending.write_and_commit(si_encoder_snapshot{cmd.version, cmd.sib1, cmd.si_msgs});

  if (current.sib1 == nullptr) {
    // First SI epoch of the cell. No slot indications are processed yet, so there is no race with the RT path.
    current = pending.read();
  }
}

span<const uint8_t> sib_pdu_assembler::encode_si_pdu(slot_point_extended sl_tx, const sib_information& si_info)
{
  ocudu_assert(si_info.pdsch_cfg.codewords.size() == 1, "SIB grants always carry exactly one codeword");
  const unsigned tbs = si_info.pdsch_cfg.codewords[0].tb_size_bytes.value();
  ocudu_assert(tbs <= MAX_BCCH_DL_SCH_PDU_SIZE, "BCCH-DL-SCH is too long. Revisit constant");

  if (si_info.version != current.version) {
    // Current SI message version is too old. Fetch new version from shared buffer.
    current = pending.read();
    if (current.version != si_info.version) {
      // Versions do not match.
      logger.error("SI message version mismatch. Expected: {}, got: {}", si_info.version, current.version);
      // We force the version to avoid more than one error log message.
      current.version = si_info.version;
    }
  }

  if (si_info.si_indicator == sib_information::si_indicator_type::sib1) {
    if (not current.sib1) {
      logger.error("Failed to encode SIB1 in PDSCH. Cause: No SIB1 was provided for the cell");
      return span<const uint8_t>{zeros_payload}.first(tbs);
    }
    auto payload = current.sib1->encode(sl_tx, si_info);
    if (not payload.has_value()) {
      units::bytes sib1_len = payload.error();
      logger.warning(
          "Failed to encode SIB1 PDSCH. Cause: PDSCH TB size {} is smaller than the SIB1 length {}", tbs, sib1_len);
      return span<const uint8_t>{zeros_payload}.first(tbs);
    }
    return payload.value();
  }

  ocudu_assert(si_info.si_msg_index.has_value(), "Invalid SI message index");
  const unsigned idx = si_info.si_msg_index.value();
  if (idx >= current.si_msgs.size() or not current.si_msgs[idx]) {
    logger.error("Failed to encode SI-message in PDSCH. Cause: SI message index {} does not exist", idx);
    return span<const uint8_t>{zeros_payload}.first(tbs);
  }

  if (ext_handler) {
    auto si_pdu = ext_handler->get_pdu(sl_tx, si_info);
    if (!si_pdu.empty()) {
      return si_pdu;
    }
  }

  auto payload = current.si_msgs[idx]->encode(sl_tx, si_info);
  if (not payload.has_value()) {
    units::bytes min_len = payload.error();
    logger.warning(
        "Failed to encode SI-message {} PDSCH. Cause: PDSCH TB size {} is smaller than the SI-message length {}",
        idx,
        tbs,
        min_len.value());
    return span<const uint8_t>{zeros_payload}.first(tbs);
  }
  return payload.value();
}
