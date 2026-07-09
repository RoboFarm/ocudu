// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "lib/mac/mac_dl/cell_dl_harq_buffer_pool.h"
#include <gtest/gtest.h>

using namespace ocudu;

namespace {

/// Number of PRBs used across the tests.
constexpr unsigned test_nof_prbs = 4;

// Expected maximum MAC PDU length in bytes, per number of layers:
//   MAX_NRE_PER_RB (156) * test_nof_prbs (4) * nof_layers * MAX_MODULATION_ORDER (8) / 8 = 624 * nof_layers.
constexpr size_t max_pdu_len_1_layer  = 624;
constexpr size_t max_pdu_len_2_layers = 1248;
constexpr size_t max_pdu_len_4_layers = 2496;

/// Constructs a pool for the given number of ports and returns the size (in bytes) of a preallocated HARQ buffer.
size_t harq_buffer_size(unsigned nof_ports)
{
  cell_dl_harq_buffer_pool pool(test_nof_prbs, nof_ports, MAX_NOF_HARQS);
  const du_ue_index_t      ue_index = to_du_ue_index(0);
  pool.allocate_ue_buffers(ue_index, 1);
  expected<dl_harq_buffer_handle> buffer = pool.allocate_dl_harq_buffer(ue_index, to_harq_id(0));
  EXPECT_TRUE(buffer.has_value());
  return buffer.value().get_buffer().size();
}

} // namespace

TEST(cell_dl_harq_buffer_pool_test, buffer_size_is_bounded_by_layers_not_ports)
{
  // For 1-4 antenna ports the number of layers is smaller or equal to MAX_NOF_LAYERS_PER_CODEWORD limit.
  EXPECT_EQ(harq_buffer_size(1), max_pdu_len_1_layer);
  EXPECT_EQ(harq_buffer_size(2), max_pdu_len_2_layers);
  EXPECT_EQ(harq_buffer_size(4), max_pdu_len_4_layers);

  // For 8 antenna ports the number of layers is limited by single codeword MAX_NOF_LAYERS_PER_CODEWORD (4) limit.
  EXPECT_EQ(harq_buffer_size(8), max_pdu_len_4_layers);
}
