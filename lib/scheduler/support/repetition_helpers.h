// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/support/ocudu_assert.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace ocudu {

/// \brief RV of a PDSCH or PUSCH repetition occasion, as per TS 38.214 Table 5.1.2.1-2 (PDSCH) and the equivalent
/// UL table (PUSCH).
///
/// The UE assumes the fixed cycle {0, 2, 3, 1}, starting at the RV signalled in the DCI and indexed by the nominal
/// occasion position (dropped occasions consume RVs).
inline uint8_t get_repetition_rv(uint8_t dci_rv, unsigned occasion_idx)
{
  static constexpr std::array<uint8_t, 4> rv_cycle = {0, 2, 3, 1};
  const auto*                             it       = std::find(rv_cycle.begin(), rv_cycle.end(), dci_rv);
  ocudu_assert(it != rv_cycle.end(), "Invalid RV value={}", dci_rv);
  return rv_cycle[(std::distance(rv_cycle.begin(), it) + occasion_idx) % rv_cycle.size()];
}

} // namespace ocudu
