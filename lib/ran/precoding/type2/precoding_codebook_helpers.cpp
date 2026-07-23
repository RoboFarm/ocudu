// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ocudu/ran/precoding/precoding_codebook_helpers.h"
#include "ocudu/adt/to_array.h"
#include "ocudu/support/ocudu_assert.h"
#include <array>
#include <cmath>

using namespace ocudu;

/// Wideband amplitude coefficients \f$p^{(1)}\f$ indexed by \f$k^{(1)}\f$, as per TS38.214 Table 5.2.2.2.3-2.
static const auto typeII_wideband_amplitudes = to_array<float>({0.0F,
                                                                std::sqrt(1.0F / 64),
                                                                std::sqrt(1.0F / 32),
                                                                std::sqrt(1.0F / 16),
                                                                std::sqrt(1.0F / 8),
                                                                std::sqrt(1.0F / 4),
                                                                std::sqrt(1.0F / 2),
                                                                1.0F});

/// Subband amplitude coefficients \f$p^{(2)}\f$ indexed by \f$k^{(2)}\f$, as per TS38.214 Table 5.2.2.2.3-3.
static const auto typeII_subband_amplitudes = to_array<float>({std::sqrt(1.0F / 2), 1.0F});

/// Combinatorial coefficients \f$C(x, y)\f$ as per TS38.214 Table 5.2.2.2.3-1, indexed as [x][y-1].
static constexpr std::array<std::array<unsigned, max_nof_typeII_beams>, 16> combinatorial_coefficient_table = {
    {{0, 0, 0, 0},
     {1, 0, 0, 0},
     {2, 1, 0, 0},
     {3, 3, 1, 0},
     {4, 6, 4, 1},
     {5, 10, 10, 5},
     {6, 15, 20, 15},
     {7, 21, 35, 35},
     {8, 28, 56, 70},
     {9, 36, 84, 126},
     {10, 45, 120, 210},
     {11, 55, 165, 330},
     {12, 66, 220, 495},
     {13, 78, 286, 715},
     {14, 91, 364, 1001},
     {15, 105, 455, 1365}}};

/// Returns the combinatorial coefficient \f$C(x, y)\f$ from TS38.214 Table 5.2.2.2.3-1.
static unsigned combinatorial_coefficient(unsigned x, unsigned y)
{
  ocudu_assert((x < combinatorial_coefficient_table.size()) && (y >= 1) && (y <= max_nof_typeII_beams),
               "The combinatorial coefficient C({}, {}) is out of the table range.",
               x,
               y);
  return combinatorial_coefficient_table[x][y - 1];
}

pmi_typeII_beam_selection ocudu::get_typeII_beam_selection(unsigned i_1_1, unsigned o1, unsigned o2)
{
  ocudu_assert((o1 != 0) && (o2 != 0), "The oversampling factors must be non-zero.");

  // O2 is always a power of two, so extracting those subfields is equivalent to a plain division/modulo.
  unsigned q2 = i_1_1 % o2;
  unsigned q1 = i_1_1 / o2;

  ocudu_assert(q1 < o1, "The decoded beam selection q1={} is out of range (O1={}).", q1, o1);
  ocudu_assert(q2 < o2, "The decoded beam selection q2={} is out of range (O2={}).", q2, o2);

  return {q1, q2};
}

static_vector<pmi_typeII_beam_group, max_nof_typeII_beams>
ocudu::get_typeII_beam_groups(unsigned i_1_2, unsigned n1, unsigned n2, unsigned nof_beams)
{
  unsigned n1n2 = n1 * n2;

  ocudu_assert((nof_beams != 0) && (nof_beams <= max_nof_typeII_beams) && (nof_beams <= n1n2),
               "Invalid number of beams L={} for N1 * N2={}.",
               nof_beams,
               n1n2);

  // Greedy decode combinatorial index i_1_2. The search starts from the largest allowed value downwards. Each linear
  // position n^(i) is then split into its (n1, n2) indices.
  static_vector<pmi_typeII_beam_group, max_nof_typeII_beams> beam_groups;
  unsigned                                                   remaining = i_1_2;
  unsigned                                                   upper     = n1n2 - 1;
  for (unsigned i = 0; i != nof_beams; ++i) {
    unsigned x = upper;
    unsigned y = nof_beams - i;
    unsigned comb_coef;
    while ((comb_coef = combinatorial_coefficient(x, y)) > remaining) {
      --x;
    }
    remaining -= comb_coef;

    unsigned linear = n1n2 - 1 - x;
    beam_groups.push_back({linear % n1, linear / n1});
    upper = (x == 0) ? 0 : (x - 1);
  }

  return beam_groups;
}

float ocudu::get_typeII_wideband_amplitude(unsigned k1)
{
  ocudu_assert(k1 < typeII_wideband_amplitudes.size(),
               "The wideband amplitude index (i.e., {}) is out of range (i.e., {}).",
               k1,
               typeII_wideband_amplitudes.size());
  return typeII_wideband_amplitudes[k1];
}

float ocudu::get_typeII_subband_amplitude(unsigned k2)
{
  ocudu_assert(k2 < typeII_subband_amplitudes.size(),
               "The subband amplitude index (i.e., {}) is out of range (i.e., {}).",
               k2,
               typeII_subband_amplitudes.size());
  return typeII_subband_amplitudes[k2];
}
