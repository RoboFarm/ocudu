// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "si_message_controller.h"
#include "../mac_scheduler_cell_configurator.h"
#include "../segmented_sib_list.h"
#include "ocudu/adt/lockfree_triple_buffer.h"
#include "ocudu/asn1/rrc_nr/bcch_dl_sch_msg.h"
#include "ocudu/asn1/rrc_nr/sys_info.h"
#include "ocudu/ocudulog/ocudulog.h"
#include <algorithm>

using namespace ocudu;

/// Encoder for a static BCCH-DL-SCH SIB1 payload.
class si_message_controller::sib1_static_encoder final : public bcch_dl_sch_msg_encoder
{
public:
  sib1_static_encoder(const byte_buffer& buffer) : len(buffer.length()), current_payload(MAX_BCCH_DL_SCH_PDU_SIZE, 0)
  {
    copy_segments(buffer, current_payload);
  }

  expected<span<const uint8_t>, units::bytes> encode(slot_point_extended /*sl_tx*/,
                                                     const sib_information& si_info) override
  {
    const unsigned tbs = si_info.pdsch_cfg.codewords[0].tb_size_bytes.value();
    if (OCUDU_LIKELY(tbs >= len.value())) {
      return span<const uint8_t>(current_payload.data(), tbs);
    }
    return make_unexpected(len);
  }

private:
  units::bytes         len;
  std::vector<uint8_t> current_payload;
};

/// BCCH-DL-SCH SIB1 buffer that gets hyperSFN auto-updated, when eDRX is enabled.
class si_message_controller::sib1_hypersfn_encoder final : public bcch_dl_sch_msg_encoder
{
public:
  sib1_hypersfn_encoder(const byte_buffer& buffer) : current_payload(MAX_BCCH_DL_SCH_PDU_SIZE, 0)
  {
    // Unpack initial SIB1.
    {
      asn1::cbit_ref bref{buffer};
      auto           err = current_unpacked.unpack(bref);
      report_fatal_error_if_not(err == asn1::OCUDUASN_SUCCESS, "Failed to unpack SIB1 for HyperSFN-aware encoder.");
      report_fatal_error_if_not(current_unpacked.msg.type().value ==
                                        asn1::rrc_nr::bcch_dl_sch_msg_type_c::types_opts::c1 and
                                    current_unpacked.msg.c1().type().value ==
                                        asn1::rrc_nr::bcch_dl_sch_msg_type_c::c1_c_::types_opts::sib_type1,
                                "SIB1 message is not of expected type for HyperSFN-aware encoder.");
    }

    // Ensure HyperSFN is encoded in SIB1 and its value matches the member current_hyper_sfn.
    auto& sib1msg                                                        = current_unpacked.msg.c1().sib_type1();
    sib1msg.non_crit_ext_present                                         = true;
    sib1msg.non_crit_ext.non_crit_ext_present                            = true;
    sib1msg.non_crit_ext.non_crit_ext.non_crit_ext_present               = true;
    sib1msg.non_crit_ext.non_crit_ext.non_crit_ext.hyper_sfn_r17_present = true;
    build_bcch_dl_sch_payload(0);
  }

  expected<span<const uint8_t>, units::bytes> encode(slot_point_extended sl_tx, const sib_information& si_info) override
  {
    const unsigned tbs = si_info.pdsch_cfg.codewords[0].tb_size_bytes.value();
    if (OCUDU_UNLIKELY(tbs < current_len.value())) {
      return make_unexpected(current_len);
    }

    const uint32_t new_hyper_sfn = sl_tx.hyper_sfn();
    if (current_hyper_sfn != new_hyper_sfn) {
      // HyperSFN has changed, re-encode payload.
      build_bcch_dl_sch_payload(new_hyper_sfn);
    }
    return span<const uint8_t>(current_payload.data(), tbs);
  }

private:
  void build_bcch_dl_sch_payload(uint32_t hyper_sfn)
  {
    // Update HyperSFN.
    current_hyper_sfn = hyper_sfn;

    // Update HyperSFN in unpacked SIB1.
    current_unpacked.msg.c1().sib_type1().non_crit_ext.non_crit_ext.non_crit_ext.hyper_sfn_r17.from_number(hyper_sfn);

    // Re-encode.
    byte_buffer   buf;
    asn1::bit_ref bref{buf};
    auto          ret = current_unpacked.pack(bref);
    ocudu_assert(ret == asn1::OCUDUASN_SUCCESS, "Failed to pack SIB1 with updated HyperSFN.");
    size_t n = copy_segments(buf, current_payload);
    ocudu_assert(n <= current_payload.size(), "Encoded SIB1 payload exceeds maximum size.");
    current_len = units::bytes{static_cast<unsigned>(n)};
  }

  asn1::rrc_nr::bcch_dl_sch_msg_s current_unpacked;
  std::vector<uint8_t>            current_payload;
  units::bytes                    current_len{0};
  uint32_t                        current_hyper_sfn = 0;
};

/// Encoder for a static (non-PWS) SI-message. Static SI-messages are never segmented -- only PWS (SIB6/7/8) content
/// segments across multiple SI-message windows (see \c pws_si_msg_encoder).
class si_message_controller::static_si_msg_encoder final : public bcch_dl_sch_msg_encoder
{
public:
  explicit static_si_msg_encoder(const byte_buffer& si_msg) :
    segment{make_linear_bcch_dl_sch_buffer(si_msg), units::bytes{static_cast<unsigned>(si_msg.length())}}
  {
  }

  expected<span<const uint8_t>, units::bytes> encode(slot_point_extended /*sl_tx*/,
                                                     const sib_information& si_info) override
  {
    const unsigned tbs = si_info.pdsch_cfg.codewords[0].tb_size_bytes.value();
    if (OCUDU_UNLIKELY(tbs < segment.len.value())) {
      return make_unexpected(segment.len);
    }
    return span<const uint8_t>(segment.buffer->data(), tbs);
  }

private:
  bcch_segment segment;
};

/// Encoder owning the repeat/count timing and content of an on-going PWS broadcast sequence. See class doc in the
/// header.
class si_message_controller::pws_si_msg_encoder final : public bcch_dl_sch_msg_encoder
{
public:
  pws_si_msg_encoder(unsigned                         si_msg_idx_,
                     timer_factory                    timers_,
                     mac_scheduler_cell_configurator& sched_,
                     du_cell_index_t                  cell_index_) :
    si_msg_idx(si_msg_idx_), timers(timers_), sched(sched_), cell_index(cell_index_)
  {
  }

  /// Handles a new Write-Replace Warning content push, called from the control executor. A new warning for this
  /// index replaces any in-flight one outright.
  bool handle_pws_broadcast(const mac_cell_sys_info_pdu_update& req)
  {
    ctrl.timer.stop();
    ++ctrl.version;
    ctrl.nof_segments             = req.si_messages.size();
    ctrl.nof_broadcasts_remaining = req.pws_broadcast->nof_broadcasts_requested;
    ctrl.repeat_period            = req.pws_broadcast->repeat_period;
    ctrl.msg_len                  = units::bytes{0};

    // Publish the new content to the real-time path.
    content_snapshot snap;
    snap.version = ctrl.version;
    for (const byte_buffer& segment : req.si_messages) {
      const units::bytes seg_len{static_cast<unsigned>(segment.length())};
      snap.segments.append_segment(bcch_segment{make_linear_bcch_dl_sch_buffer(segment), seg_len});
      // The scheduler sizes the PDSCH grant for this SI-message off the largest segment, since segments may not
      // all be the same length (e.g. a shorter final segment).
      ctrl.msg_len = std::max(ctrl.msg_len, seg_len);
    }
    pending.write_and_commit(snap);

    start_one_broadcast();

    return true;
  }

  /// \brief Activates this SI-message index once, unconditionally, using the given content, and never
  /// auto-deactivates it. Used to seed a test_mode-configured PWS SI-message at cell startup, broadcasting the
  /// configured ETWS/CMAS content indefinitely. No repeat timer is involved -- the scheduler is asked to broadcast
  /// forever, so there is no need to ever re-signal it.
  void activate_forever(span<const byte_buffer> segments)
  {
    ++ctrl.version;

    // Publish the content to the real-time path.
    content_snapshot snap;
    snap.version = ctrl.version;
    units::bytes max_len{0};
    for (const byte_buffer& segment : segments) {
      const units::bytes seg_len{static_cast<unsigned>(segment.length())};
      snap.segments.append_segment(bcch_segment{make_linear_bcch_dl_sch_buffer(segment), seg_len});
      max_len = std::max(max_len, seg_len);
    }
    pending.write_and_commit(snap);

    sched.handle_pws_broadcast_indication(cell_index, si_msg_idx, std::nullopt, max_len);
  }

  expected<span<const uint8_t>, units::bytes> encode(slot_point_extended /*sl_tx*/,
                                                     const sib_information& si_info) override
  {
    const content_snapshot& latest = pending.read();
    if (latest.version != rt.version) {
      rt.version            = latest.version;
      rt.segments           = latest.segments;
      rt.force_segment_zero = true;
    }

    if (rt.segments.get_nof_segments() == 0) {
      // No PWS content was ever pushed for this index.
      return make_unexpected(units::bytes{0});
    }

    if (rt.force_segment_zero) {
      rt.force_segment_zero = false;
    } else if (rt.segments.get_nof_segments() > 1 and !si_info.is_repetition and (si_info.nof_txs > 0)) {
      // SIB6 is never segmented; advancing only applies to segmented SIB7/8 content.
      rt.segments.advance_current_segment();
    }

    const bcch_segment& seg = rt.segments.get_current_segment();
    const unsigned      tbs = si_info.pdsch_cfg.codewords[0].tb_size_bytes.value();
    if (OCUDU_UNLIKELY(tbs < seg.len.value())) {
      return make_unexpected(seg.len);
    }
    return span<const uint8_t>(seg.buffer->data(), tbs);
  }

private:
  /// Content snapshot published from the control executor to the real-time path.
  struct content_snapshot {
    /// Monotonically increasing version, bumped every time new content is pushed for this index.
    unsigned version = 0;
    /// Segments of the on-going (or last) warning message. Empty if no warning was ever pushed for this index.
    segmented_sib_list<bcch_segment> segments;
  };

  /// Control-executor-owned repeat/count state for an active PWS broadcast sequence.
  struct ctrl_state {
    /// Version of the current warning, incremented every time a new Write-Replace Warning replaces it.
    unsigned version = 0;
    /// Number of segments in the current warning message.
    unsigned nof_segments = 0;
    /// Number of remaining broadcasts (including the on-going/next one) for the current warning.
    unsigned nof_broadcasts_remaining = 0;
    /// Period between successive broadcasts.
    std::chrono::seconds repeat_period{0};
    /// Timer used to trigger successive broadcasts. Only running while \c nof_broadcasts_remaining > 0.
    unique_timer timer;
    /// Length, in bytes, of the largest segment of the current warning message. Passed to the scheduler on every
    /// (re-)activation so it sizes the PDSCH grant for the real content.
    units::bytes msg_len{0};
  };

  /// Real-time-path-owned segment cursor state.
  struct rt_state {
    unsigned                         version = 0;
    segmented_sib_list<bcch_segment> segments;
    /// Whether the next call must serve segment 0 unconditionally, ignoring \c is_repetition/nof_txs.
    bool force_segment_zero = true;
  };

  /// Starts (or continues) the broadcast sequence: signals the scheduler for one burst, and arms the timer for the
  /// next one if any broadcasts remain.
  void start_one_broadcast()
  {
    if (ctrl.nof_broadcasts_remaining == 0) {
      return;
    }
    --ctrl.nof_broadcasts_remaining;

    sched.handle_pws_broadcast_indication(
        cell_index, si_msg_idx, std::optional<unsigned>{ctrl.nof_segments}, ctrl.msg_len);

    if (ctrl.nof_broadcasts_remaining == 0) {
      return;
    }

    if (not ctrl.timer.is_valid()) {
      ctrl.timer = timers.create_timer();
    }
    ctrl.timer.set(std::chrono::duration_cast<timer_duration>(ctrl.repeat_period), [this]() { start_one_broadcast(); });
    ctrl.timer.run();
  }

  unsigned                         si_msg_idx;
  timer_factory                    timers;
  mac_scheduler_cell_configurator& sched;
  du_cell_index_t                  cell_index;

  ctrl_state                               ctrl;
  lockfree_triple_buffer<content_snapshot> pending;
  rt_state                                 rt;
};

si_message_controller::si_message_controller(du_cell_index_t                  cell_index_,
                                             const mac_cell_sys_info_config&  sys_info,
                                             timer_factory                    timers_,
                                             mac_scheduler_cell_configurator& sched_) :
  logger(ocudulog::fetch_basic_logger("MAC")),
  cell_index(cell_index_),
  timers(timers_),
  sched(sched_),
  ext_handler(create_si_message_extension_handler(sys_info))
{
  // Set up PWS encoders, one entry per SI-message index.
  const auto& si_sched_messages = sys_info.si_sched_cfg.si_messages;
  pws_encoders.resize(sys_info.si_messages.size());
  for (unsigned i = 0, e = sys_info.si_messages.size(); i != e; ++i) {
    if (i < si_sched_messages.size() and si_sched_messages[i].requires_activation) {
      pws_encoders[i] = std::make_shared<pws_si_msg_encoder>(i, timers, sched, cell_index);
      if (si_sched_messages[i].test_mode_auto_broadcast) {
        // test_mode ETWS/CMAS config was set for this SI-message. Broadcast its (already encoded) content right
        // away, indefinitely, instead of waiting for a real Write-Replace Warning.
        pws_encoders[i]->activate_forever(sys_info.si_messages[i]);
      }
    }
  }

  // Version starts at 0.
  last_cmd.version = 0;
  build_command(sys_info);
}

si_message_controller::~si_message_controller() = default;

std::optional<si_update_command> si_message_controller::handle_si_change_request(const mac_cell_sys_info_config& req)
{
  if (not has_si_changed(req)) {
    logger.info("cell={}: Discarding SI update. Cause: The System Information did not change",
                fmt::underlying(cell_index));
    return std::nullopt;
  }

  // Bump the SI epoch and rebuild the encoders that changed.
  ++last_cmd.version;
  build_command(req);

  return last_cmd;
}

bool si_message_controller::has_si_changed(const mac_cell_sys_info_config& req) const
{
  // Note: In case the SIB1/SI message does not change, the comparison between the respective byte_buffers should be
  // fast (as they will point to the same memory location).
  if (req.sib1 != last_sib1 or req.sib1_contains_hypersfn != last_hypersfn_enabled) {
    return true;
  }
  if (req.si_messages.size() != last_si_messages.size()) {
    return true;
  }
  for (unsigned i = 0, e = req.si_messages.size(); i != e; ++i) {
    if (i < pws_encoders.size() and pws_encoders[i] != nullptr) {
      // This SI-message index is exclusively managed by its PWS encoder, and its content does not flow through this
      // (possibly unrelated) SI reconfiguration.
      continue;
    }
    if (req.si_messages[i] != last_si_messages[i]) {
      return true;
    }
  }
  return req.si_sched_cfg != last_cmd.si_sched_cfg;
}

void si_message_controller::build_command(const mac_cell_sys_info_config& req)
{
  // Generate SIB1 encoder, reusing the previous one if the payload did not change.
  if (last_cmd.sib1 == nullptr or req.sib1 != last_sib1 or req.sib1_contains_hypersfn != last_hypersfn_enabled) {
    last_sib1             = req.sib1.copy();
    last_hypersfn_enabled = req.sib1_contains_hypersfn;
    if (req.sib1_contains_hypersfn) {
      last_cmd.sib1 = std::make_shared<sib1_hypersfn_encoder>(req.sib1);
    } else {
      // eDRX not enabled, use static buffer.
      last_cmd.sib1 = std::make_shared<sib1_static_encoder>(req.sib1);
    }
  }

  // Check if SI messages have changed.
  last_cmd.si_msgs.resize(req.si_messages.size());
  last_si_messages.resize(req.si_messages.size());
  for (unsigned i = 0, e = req.si_messages.size(); i != e; ++i) {
    if (i < pws_encoders.size() and pws_encoders[i] != nullptr) {
      // This SI-message index is exclusively managed by its PWS encoder. Its content flows through
      // handle_pws_broadcast, not through this (possibly unrelated) SI reconfiguration. Leave it untouched.
      last_cmd.si_msgs[i] = pws_encoders[i];
      continue;
    }

    if (req.si_messages[i] != last_si_messages[i]) {
      ocudu_assert(req.si_messages[i].size() == 1, "Static SI-messages must not be segmented");
      last_si_messages[i].resize(1);
      last_si_messages[i].front() = req.si_messages[i].front().copy();
      last_cmd.si_msgs[i]         = std::make_shared<static_si_msg_encoder>(req.si_messages[i].front());
    }
  }

  last_cmd.si_sched_cfg = req.si_sched_cfg;
}

bool si_message_controller::handle_si_message_pdu_updates(const mac_cell_sys_info_pdu_update& req)
{
  if (req.pws_broadcast.has_value()) {
    return handle_pws_broadcast(req);
  }
  return ext_handler != nullptr and ext_handler->enqueue_si_pdu_updates(req);
}

bool si_message_controller::handle_pws_broadcast(const mac_cell_sys_info_pdu_update& req)
{
  if (req.si_msg_idx >= pws_encoders.size() or not pws_encoders[req.si_msg_idx]) {
    // SI-message index does not exist, or does not require activation (see si_message_controller constructor).
    return false;
  }
  return pws_encoders[req.si_msg_idx]->handle_pws_broadcast(req);
}
