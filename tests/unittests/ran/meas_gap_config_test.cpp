// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ocudu/ran/meas_gap_config.h"
#include <gtest/gtest.h>

using namespace ocudu;

TEST(supported_meas_gap_patterns_test, default_only_supports_mandatory_patterns_0_and_1)
{
  const supported_meas_gap_patterns default_patterns;

  // Gap patterns 0 and 1 are mandatory and always supported.
  EXPECT_TRUE(default_patterns.is_supported(0));
  EXPECT_TRUE(default_patterns.is_supported(1));
  // No other gap pattern is supported when no UE capabilities are available.
  for (unsigned pattern_id = 2; pattern_id != nof_meas_gap_patterns; ++pattern_id) {
    EXPECT_FALSE(default_patterns.is_supported(pattern_id)) << "pattern " << pattern_id;
  }
}

TEST(supported_meas_gap_patterns_test, mandatory_patterns_are_supported_even_if_not_marked)
{
  // Even if patterns 0 and 1 are never explicitly marked, they must remain supported.
  supported_meas_gap_patterns patterns;
  patterns.mark_supported(4);

  EXPECT_TRUE(patterns.is_supported(0));
  EXPECT_TRUE(patterns.is_supported(1));
  EXPECT_TRUE(patterns.is_supported(4));
}

namespace {

/// Gap configuration used in the tests below: 6ms gap every 80ms, starting at subframe 10.
constexpr meas_gap_config test_gap{10, meas_gap_length::ms6, meas_gap_repetition_period::ms80};

/// Builds a 15kHz slot (1 slot == 1 subframe) whose position within the gap period is \c phase subframes after the
/// gap offset.
slot_point slot_at_gap_phase(unsigned phase, unsigned nof_periods = 70)
{
  const unsigned mgrp_ms  = static_cast<unsigned>(test_gap.mgrp);
  const unsigned slot_idx = nof_periods * mgrp_ms + test_gap.offset + phase;
  return slot_point{subcarrier_spacing::kHz15, slot_idx / 10, slot_idx % 10};
}

} // namespace

TEST(is_inside_meas_gap_test, gap_spans_exactly_mgl_slots)
{
  // MGL is 6ms and there is one slot per subframe at 15kHz, so the gap covers the 6 slots at phases 0..5. Phase 6 is
  // the first slot after the gap.
  for (unsigned phase = 0; phase != 6; ++phase) {
    EXPECT_TRUE(is_inside_meas_gap(test_gap, slot_at_gap_phase(phase))) << "phase " << phase;
  }
  EXPECT_FALSE(is_inside_meas_gap(test_gap, slot_at_gap_phase(6)));
}
