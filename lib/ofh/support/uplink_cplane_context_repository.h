// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#pragma once

#include "context_repository_helpers.h"
#include "ocudu/adt/span.h"
#include "ocudu/adt/static_vector.h"
#include "ocudu/ofh/ofh_constants.h"
#include "ocudu/support/ocudu_assert.h"
#include "ocudu/ofh/serdes/ofh_cplane_message_properties.h"
#include "ocudu/ran/slot_pdu_capacity_constants.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <vector>

namespace ocudu {
namespace ofh {

/// Uplink Control-Plane context.
struct ul_cplane_context {
  /// Filter index.
  filter_index_type filter_index;
  /// Start symbol identifier.
  uint8_t start_symbol;
  /// Starting PRB of data section.
  uint16_t prb_start;
  /// Number of contiguous PRBs per data section.
  uint16_t nof_prb;
  /// Number of symbols.
  uint8_t nof_symbols;
};

/// Uplink Control-Plane context repository.
class uplink_cplane_context_repository
{
  using repo_entry = std::array<std::atomic<uint64_t>, MAX_NOF_SUPPORTED_EAXC>;

  /// Repository storage.
  std::vector<repo_entry> repo;
  /// Configured eAxC IDs. Per O-RAN.WG4.CUS-Spec section 3.1.3.1.6 eAxC IDs span the full 16-bit range, so contexts
  /// are stored by position in this list instead of in an array densely indexed by eAxC value.
  static_vector<unsigned, MAX_NOF_SUPPORTED_EAXC> eaxc_list;

  /// Returns the storage index for the given eAxC, or std::nullopt when the eAxC is not configured.
  std::optional<unsigned> get_eaxc_index(unsigned eaxc) const
  {
    auto it = std::find(eaxc_list.begin(), eaxc_list.end(), eaxc);
    if (it == eaxc_list.end()) {
      return std::nullopt;
    }
    return static_cast<unsigned>(std::distance(eaxc_list.begin(), it));
  }

  /// Returns the entry of the repository for the given slot and eAxC index.
  std::atomic<uint64_t>& get_entry(slot_point slot, unsigned eaxc_index)
  {
    unsigned index = calculate_repository_index(slot, repo.size());
    return repo[index][eaxc_index];
  }

  /// Returns the entry of the repository for the given slot and eAxC index.
  const std::atomic<uint64_t>& get_entry(slot_point slot, unsigned eaxc_index) const
  {
    unsigned index = calculate_repository_index(slot, repo.size());
    return repo[index][eaxc_index];
  }

  /// Packs the given context.
  static uint64_t pack_context(ul_cplane_context context)
  {
    uint64_t data = 0;
    data |= static_cast<uint8_t>(context.filter_index);
    data |= static_cast<uint64_t>(context.start_symbol) << 8;
    data |= static_cast<uint64_t>(context.prb_start) << 16;
    data |= static_cast<uint64_t>(context.nof_prb) << 32;
    data |= static_cast<uint64_t>(context.nof_symbols) << 48;

    return data;
  }

  /// Unpacks the given packed context.
  static ul_cplane_context unpack_context(uint64_t data)
  {
    return {static_cast<filter_index_type>(data),
            static_cast<uint8_t>(data >> 8),
            static_cast<uint16_t>(data >> 16),
            static_cast<uint16_t>(data >> 32),
            static_cast<uint8_t>(data >> 48)};
  }

public:
  uplink_cplane_context_repository(unsigned size_, span<const unsigned> eaxc_list_) :
    repo(size_), eaxc_list(eaxc_list_.begin(), eaxc_list_.end())
  {
    static_assert(MAX_PRACH_OCCASIONS_PER_SLOT == 1,
                  "Uplink Control-Plane context repository only supports one context per slot and eAxC");
  }

  /// Add the given context to the repo at the given slot and eAxC.
  void add(slot_point slot, unsigned eaxc, ul_cplane_context new_context)
  {
    std::optional<unsigned> eaxc_index = get_eaxc_index(eaxc);
    ocudu_assert(eaxc_index, "Cannot store Control-Plane context for non-configured eAxC value '{}'", eaxc);

    auto& entry = get_entry(slot, *eaxc_index);
    entry.store(pack_context(new_context), std::memory_order_relaxed);
  }

  /// Returns a context that matches the given slot and eAxC.
  ul_cplane_context get(slot_point slot, unsigned eaxc) const
  {
    std::optional<unsigned> eaxc_index = get_eaxc_index(eaxc);
    if (!eaxc_index) {
      // Same result as reading an entry that was never written: the caller discards the message on context mismatch.
      return unpack_context(0);
    }

    const auto& entry = get_entry(slot, *eaxc_index);
    return unpack_context(entry.load(std::memory_order_relaxed));
  }
};

} // namespace ofh
} // namespace ocudu
