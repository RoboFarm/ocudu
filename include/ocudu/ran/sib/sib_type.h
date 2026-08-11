// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/support/ocudu_assert.h"
#include <cstdint>
#include <initializer_list>

namespace ocudu {

enum class sib_type : uint8_t {
  sib1        = 1,
  sib2        = 2,
  sib3        = 3,
  sib4        = 4,
  sib5        = 5,
  sib6        = 6,
  sib7        = 7,
  sib8        = 8,
  sib16       = 16,
  sib19       = 19,
  sib_invalid = 255
};

/// Whether the given SIB carries PWS (ETWS/CMAS) content.
constexpr bool is_pws_sib(sib_type sib)
{
  return sib == sib_type::sib6 or sib == sib_type::sib7 or sib == sib_type::sib8;
}

/// \brief Set of SIBs carried by a single SI message.
///
/// A SIB is mapped to at most one SI message, so this set identifies an SI message within a cell, independently of
/// the position it occupies in the SI scheduling configuration.
class sib_type_set
{
  /// Largest SIB type that fits in the bitmap.
  static constexpr unsigned max_sib_type = 31;

  static constexpr uint32_t to_bit(sib_type sib) { return 1U << static_cast<unsigned>(sib); }

public:
  constexpr sib_type_set() = default;
  sib_type_set(std::initializer_list<sib_type> sibs)
  {
    for (sib_type sib : sibs) {
      add(sib);
    }
  }

  void add(sib_type sib)
  {
    ocudu_assert(static_cast<unsigned>(sib) <= max_sib_type, "Invalid SIB type {}", static_cast<unsigned>(sib));
    bitmap |= to_bit(sib);
  }

  bool contains(sib_type sib) const
  {
    return static_cast<unsigned>(sib) <= max_sib_type and (bitmap & to_bit(sib)) != 0;
  }

  bool empty() const { return bitmap == 0; }

  /// Whether this SI message carries a PWS (ETWS/CMAS) SIB, and therefore is only broadcast while a warning is active.
  bool is_etws_cmas() const { return (bitmap & pws_bitmap) != 0; }

  /// \brief Whether this SI message carries the NTN SIB19.
  ///
  /// Its content is pushed to the PHY immediately, bypassing the SI change modification window, as per TS 38.331,
  /// 5.2.2.2.2 and the \c ntn-Config field descriptions.
  bool is_ntn() const { return contains(sib_type::sib19); }

  bool operator==(const sib_type_set& other) const { return bitmap == other.bitmap; }
  bool operator!=(const sib_type_set& other) const { return bitmap != other.bitmap; }

private:
  static constexpr uint32_t pws_bitmap = (1U << static_cast<unsigned>(sib_type::sib6)) |
                                         (1U << static_cast<unsigned>(sib_type::sib7)) |
                                         (1U << static_cast<unsigned>(sib_type::sib8));

  uint32_t bitmap = 0;
};

} // namespace ocudu
