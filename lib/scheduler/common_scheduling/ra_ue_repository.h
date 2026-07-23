// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "../cell/cell_harq_manager.h"
#include "ocudu/adt/circular_map.h"
#include "ocudu/ran/du_types.h"
#include "ocudu/scheduler/scheduler_rach_handler.h"

namespace ocudu {

class cell_configuration;

/// \brief State of a UE's Random Access attempt, tracked from PRACH detection (TC-RNTI allocation) until
/// contention resolution, shared between the RA scheduler and the UE fallback scheduler.
struct ra_ue_context {
  /// Detected PRACH Preamble which will be associated to the Msg3/MsgA PUSCH to be scheduled.
  rach_indication_message::preamble preamble{};
  /// Slot at which the PRACH preamble was received.
  slot_point prach_slot_rx;
  /// HARQ entity used to allocate the UL HARQ process for Msg3 (native 4-step or 2-step fallback).
  /// \note [TS 38.321, 5.4.2.1] "For UL transmission with UL grant in RA Response, HARQ process identifier 0 is
  /// used".
  unique_ue_harq_entity harq_ent;

  /// TC-RNTI associated with this UE in RA.
  rnti_t tc_rnti() const { return preamble.tc_rnti; }
};

/// \brief Repository of in-flight Random Access attempts, indexed by TC-RNTI in a circular hashing fashion (the
/// UE index is not yet assigned at this stage of the RA procedure). Also owns the pool of HARQ processes backing
/// each attempt's \c harq_ent, so that the two share a single lifetime.
///
/// Shared between the RA scheduler, which owns the RA procedure and is the sole writer, and the UE-dedicated
/// scheduler (fallback scheduler, UE event manager).
class ra_ue_repository
{
  /// Container for RA UE contexts, indexed by TC-RNTI.
  using map_type = circular_map<uint16_t, ra_ue_context>;

  /// (Implementation-defined) limit for maximum number of concurrent Msg3s or MsgBs.
  static constexpr size_t MAX_CONCURRENT_MSG3_OR_MSGB = 512;

public:
  using iterator       = map_type::iterator;
  using const_iterator = map_type::const_iterator;

  explicit ra_ue_repository(const cell_configuration& cell_cfg,
                            ocudulog::basic_logger&   logger,
                            size_t                    capacity = MAX_CONCURRENT_MSG3_OR_MSGB);

  /// HARQ process pool backing the \c harq_ent field of the contexts held in this repository.
  cell_harq_manager& harqs() { return ra_harqs; }

  /// Maps a TC-RNTI to its ring index in this repository.
  static uint16_t ring_key(rnti_t tc_rnti) { return static_cast<uint16_t>(to_value(tc_rnti) % MAX_NOF_DU_UES); }

  /// \brief Clears all entries in the repository.
  void clear() { table.clear(); }

  /// \brief Returns the number of entries in the repository.
  size_t size() const { return table.size(); }

  /// \brief Checks if the repository is empty.
  bool empty() const { return table.empty(); }

  /// \brief Returns the capacity of the repository.
  size_t capacity() const { return table.capacity(); }

  iterator       begin() { return table.begin(); }
  iterator       end() { return table.end(); }
  const_iterator begin() const { return table.begin(); }
  const_iterator end() const { return table.end(); }

  /// \brief Adds a new RA UE entry for the detected preamble's TC-RNTI, together with the UL HARQ entity used for
  /// its Msg3 retransmissions (native 4-step or 2-step fallback).
  /// \return Pointer to the newly created entry; \c nullptr if a ring-key collision was detected (an unrelated,
  /// still-live entry already occupies this TC-RNTI's ring slot).
  ra_ue_context* add(const rach_indication_message::preamble& preamble, slot_point prach_slot_rx)
  {
    ra_ue_context* ctx = add_entry(preamble, prach_slot_rx);
    if (ctx == nullptr) {
      return nullptr;
    }
    ctx->harq_ent = ra_harqs.add_ue(to_du_ue_index(ring_key(preamble.tc_rnti)), preamble.tc_rnti, 1, 1);
    return ctx;
  }

  /// \brief Erase a RA UE entry from the repository.
  iterator erase(rnti_t tc_rnti) { return table.erase(find(tc_rnti)); }
  iterator erase(iterator it) { return table.erase(it); }

  /// \brief Looks up the RA context for a TC-RNTI.
  /// \return The RA context, if an entry exists for this exact TC-RNTI. Returns \c nullptr otherwise.
  const_iterator find(rnti_t tc_rnti) const
  {
    auto it = table.find(ring_key(tc_rnti));
    return it != end() and it->second.tc_rnti() == tc_rnti ? it : end();
  }
  iterator find(rnti_t tc_rnti)
  {
    auto it = table.find(ring_key(tc_rnti));
    return it != end() and it->second.tc_rnti() == tc_rnti ? it : end();
  }

  /// Retrieves UE based on its key without TC-RNTI disambiguation.
  const_iterator find_by_key(uint16_t key) const { return table.find(key); }
  iterator       find_by_key(uint16_t key) { return table.find(key); }

private:
  /// \brief Inserts a new entry for the detected preamble's TC-RNTI, saving the preamble directly.
  /// \return Pointer to the newly created entry; \c nullptr if a ring-key collision was detected (an unrelated,
  /// still-live entry already occupies this TC-RNTI's ring slot).
  ra_ue_context* add_entry(const rach_indication_message::preamble& preamble, slot_point prach_slot_rx)
  {
    const uint16_t key = ring_key(preamble.tc_rnti);
    if (not table.emplace(key)) {
      return nullptr;
    }
    ra_ue_context& ctx = table[key];
    ctx.preamble       = preamble;
    ctx.prach_slot_rx  = prach_slot_rx;
    return &ctx;
  }

  // Manager of UL HARQs for Msg3. Declared before \c table so it outlives every \c ra_ue_context::harq_ent it
  // backs: members are destroyed in reverse declaration order, so the contexts are torn down first.
  cell_harq_manager ra_harqs;

  /// Table of TC-RNTI -> RA UE contexts.
  map_type table;
};

} // namespace ocudu
