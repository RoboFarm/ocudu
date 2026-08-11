// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "lib/mac/mac_ctrl/si_message_controller.h"
#include "lib/mac/mac_dl/sib_pdu_assembler.h"
#include "mac_test_helpers.h"
#include "tests/test_doubles/utils/test_rng.h"
#include "ocudu/support/executors/manual_task_worker.h"

namespace ocudu {
namespace test_helpers {

inline byte_buffer make_random_pdu(unsigned size = test_rng::uniform_int<unsigned>(10, 200))
{
  return byte_buffer::create(test_rng::vector_of_uniform_ints<uint8_t>(size)).value();
}

inline std::vector<byte_buffer> make_random_segmented_pdu(unsigned segment_size = test_rng::uniform_int<unsigned>(10,
                                                                                                                  200),
                                                          unsigned nof_segments = test_rng::uniform_int<unsigned>(2, 3))
{
  std::vector<byte_buffer> segmented_pdu;
  for (unsigned i_segment = 0; i_segment != nof_segments; ++i_segment) {
    segmented_pdu.emplace_back(make_random_pdu(segment_size));
  }
  return segmented_pdu;
}

inline byte_buffer make_pdu_with_padding(const byte_buffer& payload, units::bytes tbs)
{
  byte_buffer          result = payload.deep_copy().value();
  std::vector<uint8_t> zeros(tbs.value() - result.length(), 0);
  report_fatal_error_if_not(result.append(zeros), "Failed appending zeros");
  return result;
}

inline sib_information make_sib_pdu(std::optional<unsigned> si_msg_index, si_version_type si_version, units::bytes tbs)
{
  sib_information result{};
  result.si_indicator  = si_msg_index.has_value() ? sib_information::other_si : sib_information::sib1;
  result.si_msg_index  = si_msg_index;
  result.version       = si_version;
  result.is_repetition = false;
  result.pdsch_cfg.codewords.emplace_back();
  result.pdsch_cfg.codewords[0].tb_size_bytes = tbs;
  return result;
}

/// \brief Bench pairing an SI message controller with the SIB assembler it feeds.
///
/// The encoders are owned by the controller, so the assembler can only be exercised through the commands the
/// controller emits.
class si_bench
{
public:
  explicit si_bench(mac_cell_sys_info_config sys_info_cfg_) :
    sys_info_cfg(std::move(sys_info_cfg_)),
    si_mng(to_du_cell_index(0), sys_info_cfg, timer_factory{timers, task_worker}, sched)
  {
    assembler.start_broadcast(si_mng.extension_handler(), si_mng.last_command());
  }

  /// Forwards a new System Information config to the controller and applies the resulting command, if any.
  std::optional<si_update_command> update_si(const mac_cell_sys_info_config& req)
  {
    std::optional<si_update_command> cmd = si_mng.handle_si_change_request(req);
    if (cmd.has_value()) {
      assembler.handle_si_update(*cmd);
    }
    return cmd;
  }

  /// Advances the timers by the given number of milliseconds, running any dispatched task.
  void tick(unsigned nof_ticks)
  {
    for (unsigned t = 0; t != nof_ticks; ++t) {
      timers.tick();
      task_worker.run_pending_tasks();
    }
  }

  manual_task_worker          task_worker{128};
  timer_manager               timers;
  dummy_mac_scheduler_adapter sched;

  mac_cell_sys_info_config sys_info_cfg;
  si_message_controller    si_mng;
  sib_pdu_assembler        assembler;

  slot_point_extended current_slot{subcarrier_spacing::kHz30, 0};
};

} // namespace test_helpers
} // namespace ocudu
