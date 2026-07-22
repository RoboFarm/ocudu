// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#include "uplink_cplane_context_repository.h"
#include <gtest/gtest.h>

using namespace ocudu;
using namespace ofh;

static ul_cplane_context build_test_context(uint8_t start_symbol)
{
  ul_cplane_context context;
  context.filter_index = filter_index_type::standard_channel_filter;
  context.start_symbol = start_symbol;
  context.prb_start    = 0;
  context.nof_prb      = 51;
  context.nof_symbols  = 14;

  return context;
}

TEST(uplink_cplane_context_repository, stored_context_is_retrieved_for_configured_eaxc)
{
  static_vector<unsigned, MAX_NOF_SUPPORTED_EAXC> eaxc_list = {2};
  uplink_cplane_context_repository                repo(20, eaxc_list);
  slot_point                                      slot(subcarrier_spacing::kHz30, 0, 1);

  repo.add(slot, 2, build_test_context(3));

  ul_cplane_context context = repo.get(slot, 2);
  ASSERT_EQ(context.start_symbol, 3);
  ASSERT_EQ(context.nof_prb, 51);
  ASSERT_EQ(context.nof_symbols, 14);
}

TEST(uplink_cplane_context_repository, supports_full_16_bit_eaxc_id_range)
{
  // Per O-RAN.WG4.CUS-Spec section 3.1.3.1.6 the eAxC ID spans the full 16-bit range [0x0000, 0xFFFF].
  static_vector<unsigned, MAX_NOF_SUPPORTED_EAXC> eaxc_list = {0xFFFF, 0x0003, 0x0103};
  uplink_cplane_context_repository                repo(20, eaxc_list);
  slot_point                                      slot(subcarrier_spacing::kHz30, 0, 1);

  repo.add(slot, 0xFFFF, build_test_context(1));

  ul_cplane_context context = repo.get(slot, 0xFFFF);
  ASSERT_EQ(context.start_symbol, 1);
  ASSERT_EQ(context.nof_prb, 51);

  // eAxC IDs that share a low byte must be tracked independently (e.g. 0x0003 and 0x0103).
  repo.add(slot, 0x0003, build_test_context(5));
  repo.add(slot, 0x0103, build_test_context(9));
  ASSERT_EQ(repo.get(slot, 0x0003).start_symbol, 5);
  ASSERT_EQ(repo.get(slot, 0x0103).start_symbol, 9);
}

TEST(uplink_cplane_context_repository, non_configured_eaxc_returns_empty_context)
{
  static_vector<unsigned, MAX_NOF_SUPPORTED_EAXC> eaxc_list = {2};
  uplink_cplane_context_repository                repo(20, eaxc_list);
  slot_point                                      slot(subcarrier_spacing::kHz30, 0, 1);

  repo.add(slot, 2, build_test_context(3));

  // Reading a non-configured eAxC behaves like reading an entry that was never written.
  ul_cplane_context context = repo.get(slot, 4);
  ASSERT_EQ(context.nof_prb, 0);
  ASSERT_EQ(context.nof_symbols, 0);
}
