// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "si_test_helpers.h"
#include <gtest/gtest.h>

using namespace ocudu;
using namespace ocudu::test_helpers;

class si_message_controller_test : public ::testing::Test
{
public:
  si_message_controller_test() : bench(make_sys_info_cfg()) {}

  static mac_cell_sys_info_config make_sys_info_cfg()
  {
    mac_cell_sys_info_config cfg;
    cfg.sib1 = make_random_pdu();
    cfg.si_messages.push_back(bcch_dl_sch_payload_type{make_random_pdu()});
    cfg.si_sched_cfg.si_messages.emplace_back();
    return cfg;
  }

  si_bench bench;
};

TEST_F(si_message_controller_test, when_cell_is_created_then_initial_command_holds_version_zero_and_encoders)
{
  const si_update_command& cmd = bench.si_mng.initial_command();

  ASSERT_EQ(cmd.version, 0);
  ASSERT_NE(cmd.sib1, nullptr);
  ASSERT_EQ(cmd.si_msgs.size(), 1);
  ASSERT_NE(cmd.si_msgs[0], nullptr);
}

TEST_F(si_message_controller_test, when_system_information_is_unchanged_then_no_command_is_generated)
{
  ASSERT_FALSE(bench.update_si(bench.sys_info_cfg).has_value());
  ASSERT_FALSE(bench.update_si(bench.sys_info_cfg).has_value())
      << "A repeated no-op SI update must keep being discarded";
}

TEST_F(si_message_controller_test, when_sib1_changes_then_version_is_bumped_and_sib1_encoder_is_replaced)
{
  const si_update_command previous = bench.si_mng.initial_command();

  mac_cell_sys_info_config req = bench.sys_info_cfg;
  req.sib1                     = make_random_pdu();

  std::optional<si_update_command> cmd = bench.update_si(req);
  ASSERT_TRUE(cmd.has_value());
  ASSERT_EQ(cmd->version, previous.version + 1);
  ASSERT_NE(cmd->sib1, previous.sib1);
  ASSERT_EQ(cmd->si_msgs[0], previous.si_msgs[0]) << "An unchanged SI-message must reuse its encoder";
}

TEST_F(si_message_controller_test, when_only_si_scheduling_config_changes_then_version_is_bumped)
{
  const si_update_command previous = bench.si_mng.initial_command();

  mac_cell_sys_info_config req            = bench.sys_info_cfg;
  req.si_sched_cfg.si_messages[0].msg_len = units::bytes{123};

  std::optional<si_update_command> cmd = bench.update_si(req);
  ASSERT_TRUE(cmd.has_value());
  ASSERT_EQ(cmd->version, previous.version + 1);
  ASSERT_EQ(cmd->si_sched_cfg.si_messages[0].msg_len, units::bytes{123});
  ASSERT_EQ(cmd->sib1, previous.sib1) << "An unchanged SIB1 must reuse its encoder";
}

TEST_F(si_message_controller_test, when_si_message_does_not_require_activation_then_pws_broadcast_is_rejected)
{
  // The SI-message at index 0 does not mark requires_activation, so no PWS broadcast state was allocated for it -- a
  // Write-Replace Warning targeting it must be rejected rather than silently misbehave.
  std::vector<byte_buffer>     segments = make_random_segmented_pdu(50, 1);
  mac_cell_sys_info_pdu_update req;
  req.si_msg_idx    = 0;
  req.sib_idx       = 6;
  req.si_messages   = span<byte_buffer>(segments);
  req.pws_broadcast = pws_broadcast_indication{std::chrono::seconds{1}, 1};

  ASSERT_FALSE(bench.si_mng.handle_si_message_pdu_updates(req));
  ASSERT_EQ(bench.sched.nof_pws_broadcast_indications, 0);
}

/// Fixture with a single SI-message pre-provisioned at index 0, mirroring a cell configured with a reserved
/// SIB6/7/8 occasion (the PWS broadcast per-index state in \c si_message_controller is sized once, at construction,
/// from the initial number of SI-messages).
class si_message_controller_pws_test : public ::testing::Test
{
public:
  si_message_controller_pws_test() : bench(make_sys_info_cfg()) {}

  static mac_cell_sys_info_config make_sys_info_cfg()
  {
    mac_cell_sys_info_config cfg;
    cfg.sib1 = make_random_pdu();
    cfg.si_messages.push_back(bcch_dl_sch_payload_type{make_random_pdu()});
    // Mark SI-message index 0 as requiring activation, mirroring a cell configured with a reserved SIB6/7/8
    // occasion -- si_message_controller only allocates PWS broadcast state for such indices.
    si_message_scheduling_config& si_msg_cfg = cfg.si_sched_cfg.si_messages.emplace_back();
    si_msg_cfg.requires_activation           = true;
    return cfg;
  }

  si_bench bench;
};

TEST_F(si_message_controller_pws_test,
       when_pws_broadcast_is_pushed_then_scheduler_is_signalled_immediately_for_one_burst)
{
  std::vector<byte_buffer>     segments = make_random_segmented_pdu(50, 2);
  mac_cell_sys_info_pdu_update req;
  req.si_msg_idx    = 0;
  req.sib_idx       = 7;
  req.si_messages   = span<byte_buffer>(segments);
  req.pws_broadcast = pws_broadcast_indication{std::chrono::seconds{1}, 3};

  ASSERT_TRUE(bench.si_mng.handle_si_message_pdu_updates(req));
  ASSERT_EQ(bench.sched.nof_pws_broadcast_indications, 1);
  ASSERT_EQ(bench.sched.last_pws_si_msg_idx, 0);
  ASSERT_EQ(bench.sched.last_pws_nof_segments, 2);
  // Regression test: the scheduler must be signalled with the real (activation-time) content length, not whatever
  // was configured/encoded for this SI-message at cell startup.
  ASSERT_EQ(bench.sched.last_pws_msg_len, units::bytes{50});
}

TEST_F(si_message_controller_pws_test, when_pws_broadcast_content_is_encoded_then_segments_cycle_in_order)
{
  std::vector<byte_buffer>     segments = make_random_segmented_pdu(50, 2);
  mac_cell_sys_info_pdu_update req;
  req.si_msg_idx    = 0;
  req.sib_idx       = 7;
  req.si_messages   = span<byte_buffer>(segments);
  req.pws_broadcast = pws_broadcast_indication{std::chrono::seconds{1}, 1};
  bench.si_mng.handle_si_message_pdu_updates(req);

  units::bytes    tbs{static_cast<unsigned>(segments[0].length())};
  sib_information si_info = make_sib_pdu(0, 0, tbs);

  si_info.is_repetition    = false;
  span<const uint8_t> pdu0 = bench.assembler.encode_si_pdu(bench.current_slot, si_info);
  ASSERT_EQ(byte_buffer::create(pdu0).value(), segments[0]);

  ++si_info.nof_txs;
  span<const uint8_t> pdu1 = bench.assembler.encode_si_pdu(bench.current_slot, si_info);
  ASSERT_EQ(byte_buffer::create(pdu1).value(), segments[1]);

  ++si_info.nof_txs;
  span<const uint8_t> pdu0_again = bench.assembler.encode_si_pdu(bench.current_slot, si_info);
  ASSERT_EQ(byte_buffer::create(pdu0_again).value(), segments[0])
      << "Segment cycle must wrap back to segment 0 to start the next broadcast";
}

TEST_F(si_message_controller_pws_test,
       when_multiple_broadcasts_requested_then_timer_re_triggers_scheduler_until_exhausted)
{
  auto                         segment = make_random_pdu();
  std::vector<byte_buffer>     segments{segment.copy()};
  mac_cell_sys_info_pdu_update req;
  req.si_msg_idx                = 0;
  req.sib_idx                   = 6;
  req.si_messages               = span<byte_buffer>(segments);
  const unsigned nof_broadcasts = 3;
  req.pws_broadcast             = pws_broadcast_indication{std::chrono::seconds{1}, nof_broadcasts};

  const units::bytes seg_len{static_cast<unsigned>(segment.length())};

  bench.si_mng.handle_si_message_pdu_updates(req);
  ASSERT_EQ(bench.sched.nof_pws_broadcast_indications, 1);
  ASSERT_EQ(bench.sched.last_pws_msg_len, seg_len);

  const unsigned ticks_per_broadcast = 1000; // repeat_period == 1 second == 1000 ms ticks.
  for (unsigned b = 1; b != nof_broadcasts; ++b) {
    bench.tick(ticks_per_broadcast);
    ASSERT_EQ(bench.sched.nof_pws_broadcast_indications, b + 1) << "Broadcast #" << (b + 1) << " was not signalled";
    // Regression test: repeats re-triggered by the timer must keep signalling the same content length as the
    // original activation, not reset it to zero.
    ASSERT_EQ(bench.sched.last_pws_msg_len, seg_len);
  }

  // No further broadcasts should be signalled once the requested count has been exhausted.
  bench.tick(ticks_per_broadcast * 2);
  ASSERT_EQ(bench.sched.nof_pws_broadcast_indications, nof_broadcasts);
}

TEST_F(si_message_controller_pws_test, when_new_pws_broadcast_replaces_previous_then_content_and_timer_are_reset)
{
  auto                         segment_a = make_random_pdu();
  std::vector<byte_buffer>     segments_a{segment_a.copy()};
  mac_cell_sys_info_pdu_update req_a;
  req_a.si_msg_idx    = 0;
  req_a.sib_idx       = 6;
  req_a.si_messages   = span<byte_buffer>(segments_a);
  req_a.pws_broadcast = pws_broadcast_indication{std::chrono::seconds{1}, 10};
  bench.si_mng.handle_si_message_pdu_updates(req_a);
  ASSERT_EQ(bench.sched.nof_pws_broadcast_indications, 1);

  auto                         segment_b = make_random_pdu();
  std::vector<byte_buffer>     segments_b{segment_b.copy()};
  mac_cell_sys_info_pdu_update req_b;
  req_b.si_msg_idx    = 0;
  req_b.sib_idx       = 6;
  req_b.si_messages   = span<byte_buffer>(segments_b);
  req_b.pws_broadcast = pws_broadcast_indication{std::chrono::seconds{1}, 1};
  bench.si_mng.handle_si_message_pdu_updates(req_b);
  ASSERT_EQ(bench.sched.nof_pws_broadcast_indications, 2);

  units::bytes        tbs{static_cast<unsigned>(segment_b.length())};
  sib_information     si_info = make_sib_pdu(0, 0, tbs);
  span<const uint8_t> pdu     = bench.assembler.encode_si_pdu(bench.current_slot, si_info);
  ASSERT_EQ(byte_buffer::create(pdu).value(), segment_b)
      << "Replacement content must be served from segment 0, not the superseded warning";

  // The old (10-broadcast) timer must not keep firing after being replaced by the new (1-broadcast) one.
  bench.tick(3000);
  ASSERT_EQ(bench.sched.nof_pws_broadcast_indications, 2);
}

TEST_F(si_message_controller_pws_test, when_unrelated_si_reconfiguration_occurs_then_active_pws_broadcast_is_unaffected)
{
  // Start a multi-broadcast PWS sequence.
  auto                         segment = make_random_pdu();
  std::vector<byte_buffer>     segments{segment.copy()};
  mac_cell_sys_info_pdu_update pws_req;
  pws_req.si_msg_idx    = 0;
  pws_req.sib_idx       = 6;
  pws_req.si_messages   = span<byte_buffer>(segments);
  pws_req.pws_broadcast = pws_broadcast_indication{std::chrono::seconds{1}, 3};
  bench.si_mng.handle_si_message_pdu_updates(pws_req);
  ASSERT_EQ(bench.sched.nof_pws_broadcast_indications, 1);

  // An unrelated SI reconfiguration arrives (e.g. a SIB2 content update), rebuilding all SI-messages, including a
  // placeholder for index 0 that has nothing to do with the active warning.
  mac_cell_sys_info_config unrelated_req = bench.sys_info_cfg;
  unrelated_req.sib1                     = make_random_pdu();
  unrelated_req.si_messages[0]           = bcch_dl_sch_payload_type{make_random_pdu()};
  ASSERT_TRUE(bench.update_si(unrelated_req).has_value());

  // The active PWS broadcast's content must still be served, not the unrelated placeholder.
  units::bytes        tbs{static_cast<unsigned>(segment.length())};
  sib_information     si_info = make_sib_pdu(0, 1, tbs);
  span<const uint8_t> pdu     = bench.assembler.encode_si_pdu(bench.current_slot, si_info);
  ASSERT_EQ(byte_buffer::create(pdu).value(), segment)
      << "Unrelated SI reconfiguration must not disrupt the active PWS broadcast";

  // The repeat timer must still fire the remaining broadcasts.
  bench.tick(2000);
  ASSERT_EQ(bench.sched.nof_pws_broadcast_indications, 3);
}

/// Fixture with a single SI-message pre-provisioned at index 0, marked both requires_activation and
/// test_mode_auto_broadcast, mirroring a cell whose test_mode.warning ETWS/CMAS config is set -- the content is
/// already present in si_messages[0] at construction time (built by the DU-manager translators from the test_mode
/// config), and the controller must broadcast it right away, indefinitely.
class si_message_controller_auto_broadcast_test : public ::testing::Test
{
public:
  si_message_controller_auto_broadcast_test() : bench(make_sys_info_cfg()) {}

  static mac_cell_sys_info_config make_sys_info_cfg()
  {
    mac_cell_sys_info_config cfg;
    cfg.sib1 = make_random_pdu();
    cfg.si_messages.push_back(make_random_segmented_pdu(50, 2));
    si_message_scheduling_config& si_msg_cfg = cfg.si_sched_cfg.si_messages.emplace_back();
    si_msg_cfg.requires_activation           = true;
    si_msg_cfg.test_mode_auto_broadcast      = true;
    return cfg;
  }

  si_bench bench;
};

TEST_F(si_message_controller_auto_broadcast_test,
       when_controller_is_constructed_then_scheduler_is_signalled_for_indefinite_broadcast)
{
  ASSERT_EQ(bench.sched.nof_pws_broadcast_indications, 1);
  ASSERT_EQ(bench.sched.last_pws_si_msg_idx, 0);
  ASSERT_FALSE(bench.sched.last_pws_nof_segments.has_value()) << "test_mode auto-broadcast must never auto-deactivate";
  ASSERT_EQ(bench.sched.last_pws_msg_len, units::bytes{50});
}

TEST_F(si_message_controller_auto_broadcast_test, when_content_is_encoded_then_it_matches_configured_si_message)
{
  const auto& segments = bench.sys_info_cfg.si_messages[0];

  units::bytes    tbs{static_cast<unsigned>(segments[0].length())};
  sib_information si_info = make_sib_pdu(0, 0, tbs);

  si_info.is_repetition    = false;
  span<const uint8_t> pdu0 = bench.assembler.encode_si_pdu(bench.current_slot, si_info);
  ASSERT_EQ(byte_buffer::create(pdu0).value(), segments[0]);

  ++si_info.nof_txs;
  span<const uint8_t> pdu1 = bench.assembler.encode_si_pdu(bench.current_slot, si_info);
  ASSERT_EQ(byte_buffer::create(pdu1).value(), segments[1]);
}

TEST_F(si_message_controller_auto_broadcast_test,
       when_real_write_replace_warning_arrives_then_it_overrides_the_test_mode_broadcast)
{
  std::vector<byte_buffer>     segments = make_random_segmented_pdu(50, 1);
  mac_cell_sys_info_pdu_update req;
  req.si_msg_idx    = 0;
  req.sib_idx       = 6;
  req.si_messages   = span<byte_buffer>(segments);
  req.pws_broadcast = pws_broadcast_indication{std::chrono::seconds{1}, 1};

  ASSERT_TRUE(bench.si_mng.handle_si_message_pdu_updates(req));
  ASSERT_EQ(bench.sched.nof_pws_broadcast_indications, 2);
  ASSERT_TRUE(bench.sched.last_pws_nof_segments.has_value());
  ASSERT_EQ(bench.sched.last_pws_nof_segments, 1);
  ASSERT_EQ(bench.sched.last_pws_msg_len, units::bytes{50});
}
