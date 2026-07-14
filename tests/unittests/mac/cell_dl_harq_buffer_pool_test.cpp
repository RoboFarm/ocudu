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

/// Constructs a pool for the given max number of layers and returns the size (in bytes) of a preallocated HARQ buffer.
size_t harq_buffer_size(unsigned max_nof_layers)
{
  cell_dl_harq_buffer_pool pool(test_nof_prbs, max_nof_layers, MAX_NOF_HARQS);
  const du_ue_index_t      ue_index = to_du_ue_index(0);
  pool.allocate_ue_buffers(ue_index, 1);
  expected<dl_harq_buffer_handle> buffer = pool.allocate_dl_harq_buffer(ue_index, to_harq_id(0));
  EXPECT_TRUE(buffer.has_value());
  return buffer.value().get_buffer().size();
}

} // namespace

TEST(cell_dl_harq_buffer_pool_test, buffer_size_is_bounded_by_layers_not_ports)
{
  // The buffer is sized by the configured maximum number of DL layers (already bounded by the number of antenna ports
  // and the single-codeword layer limit when the cell config is resolved), not by the number of antenna ports.
  EXPECT_EQ(harq_buffer_size(1), max_pdu_len_1_layer);
  EXPECT_EQ(harq_buffer_size(2), max_pdu_len_2_layers);
  EXPECT_EQ(harq_buffer_size(4), max_pdu_len_4_layers);
}
