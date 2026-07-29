// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#pragma once

#include "ocudu/adt/strong_type.h"
#include "fmt/format.h"
#include <climits>
#include <cstdint>

namespace ocudu {

namespace units {

namespace detail {
/// Tag struct used to uniquely identify the bit units type.
struct bit_tag {
  /// Text representation for the units.
  static const char* str() { return "bits"; }
};

/// Tag struct used to uniquely identify the byte units type.
struct byte_tag {
  /// Text representation for the units.
  static const char* str() { return "bytes"; }
};
} // namespace detail

class bytes;

/// \brief Abstraction of bit as a unit of digital information.
///
/// An object of this type will represent an amount of digital information expressed in bits.
class bits : public strong_type<unsigned,
                                detail::bit_tag,
                                strong_arithmetic,
                                strong_increment_decrement,
                                strong_multiplication_with<unsigned>>
{
  /// Type alias for the base class of the bits units class.
  using bits_base = strong_type<unsigned,
                                detail::bit_tag,
                                strong_arithmetic,
                                strong_increment_decrement,
                                strong_multiplication_with<unsigned>>;

public:
  using bits_base::bits_base;

  constexpr bits(const bits_base& other) : bits_base(other) {}

  /// Returns true if the amount of digital information expressed as bits is a multiple of a byte.
  constexpr bool is_byte_exact() const { return ((value() % CHAR_BIT) == 0); }

  /// Returns the amount of digital information expressed as an integer number of bytes, rounded down.
  constexpr bytes truncate_to_bytes() const;

  /// Returns the amount of digital information expressed as an integer number of bytes, rounded up.
  constexpr bytes round_up_to_bytes() const;
};

/// \brief Abstraction of byte as a unit of digital information.
///
/// An object of this class will represent an amount of digital information expressed in bytes. The class also provides
/// a method to convert such amount into a number of information bits.
class bytes : public strong_type<unsigned,
                                 detail::byte_tag,
                                 strong_arithmetic,
                                 strong_increment_decrement,
                                 strong_multiplication_with<unsigned>>
{
  /// Type alias for the base class of the byte units class.
  using bytes_base = strong_type<unsigned,
                                 detail::byte_tag,
                                 strong_arithmetic,
                                 strong_increment_decrement,
                                 strong_multiplication_with<unsigned>>;

public:
  using bytes_base::bytes_base;

  constexpr bytes(const bytes_base& other) : bytes_base(other) {}

  explicit constexpr operator bits() const { return to_bits(); }

  /// Returns the amount of digital information as a number of bits.
  constexpr bits to_bits() const { return bits(value() * CHAR_BIT); }
};

constexpr bytes bits::truncate_to_bytes() const
{
  return bytes(value() / CHAR_BIT);
}

constexpr bytes bits::round_up_to_bytes() const
{
  return bytes((value() + CHAR_BIT - 1) / CHAR_BIT);
}

/// \brief Abstraction of a bitrate, i.e., an amount of digital information transferred per unit of time.
///
/// The value is stored, without normalization, in the unit given at construction time. Decimal prefixes are used,
/// i.e., 1 kbps == 1000 bps.
class bitrate
{
public:
  /// Units in which a bitrate can be expressed.
  enum class unit : uint8_t {
    bit_per_sec,
    kilobit_per_sec,
    megabit_per_sec,
    gigabit_per_sec,
    byte_per_sec,
    kilobyte_per_sec,
    megabyte_per_sec,
    gigabyte_per_sec
  };

  constexpr bitrate() = default;
  explicit constexpr bitrate(float value_, unit unit_ = unit::bit_per_sec) : val(value_), unit_val(unit_) {}

  /// Returns the bitrate value in the unit it was constructed with.
  constexpr float value() const { return val; }

  /// Returns the unit in which the bitrate value is stored.
  constexpr unit get_unit() const { return unit_val; }

  /// Returns the bitrate value converted to the requested unit.
  constexpr float to_unit(unit u) const
  {
    return static_cast<float>(static_cast<double>(val) * bps_factor(unit_val) / bps_factor(u));
  }

  /// Returns the text representation of the given unit.
  static constexpr const char* to_string(unit u)
  {
    switch (u) {
      case unit::bit_per_sec:
        return "bps";
      case unit::kilobit_per_sec:
        return "kbps";
      case unit::megabit_per_sec:
        return "Mbps";
      case unit::gigabit_per_sec:
        return "Gbps";
      case unit::byte_per_sec:
        return "Bps";
      case unit::kilobyte_per_sec:
        return "kBps";
      case unit::megabyte_per_sec:
        return "MBps";
      case unit::gigabyte_per_sec:
      default:
        return "GBps";
    }
  }

private:
  /// Returns the factor that converts a value expressed in the given unit into bits per second.
  static constexpr double bps_factor(unit u)
  {
    switch (u) {
      case unit::bit_per_sec:
        return 1e0;
      case unit::kilobit_per_sec:
        return 1e3;
      case unit::megabit_per_sec:
        return 1e6;
      case unit::gigabit_per_sec:
        return 1e9;
      case unit::byte_per_sec:
        return CHAR_BIT * 1e0;
      case unit::kilobyte_per_sec:
        return CHAR_BIT * 1e3;
      case unit::megabyte_per_sec:
        return CHAR_BIT * 1e6;
      case unit::gigabyte_per_sec:
      default:
        return CHAR_BIT * 1e9;
    }
  }

  /// Bitrate value, expressed in the unit given by \c unit_val.
  float val = 0.0F;
  /// Unit in which the bitrate value is expressed.
  unit unit_val = unit::bit_per_sec;
};

static_assert(sizeof(bitrate) <= 8, "bitrate must not exceed 64 bits");

namespace literals {

/// User defined literal for byte units.
constexpr bytes operator""_bytes(unsigned long long n)
{
  return bytes(n);
}

/// User defined literal for bit units.
constexpr bits operator""_bits(unsigned long long n)
{
  return bits(n);
}

} // namespace literals
} // namespace units
} // namespace ocudu

namespace fmt {

/// Formatter for bit units.
template <>
struct formatter<ocudu::units::bits> : public formatter<ocudu::units::bits::value_type> {
  template <typename FormatContext>
  auto format(ocudu::units::bits s, FormatContext& ctx) const
  {
    return fmt::format_to(ctx.out(), "{}{}", s.value(), ocudu::units::bits::tag_type::str());
  }
};

/// Formatter for byte units.
template <>
struct formatter<ocudu::units::bytes> : public formatter<ocudu::units::bytes::value_type> {
  bool print_units = false;

  template <typename ParseContext>
  auto parse(ParseContext& ctx)
  {
    auto it = ctx.begin();
    while (it != ctx.end() and *it != '}') {
      if (*it == 'B') {
        print_units = true;
      }
      ++it;
    }
    return it;
  }

  template <typename FormatContext>
  auto format(ocudu::units::bytes s, FormatContext& ctx) const
  {
    return fmt::format_to(ctx.out(), "{}{}", s.value(), print_units ? ocudu::units::bytes::tag_type::str() : "");
  }
};

/// Formatter for bitrate.
template <>
struct formatter<ocudu::units::bitrate> : public formatter<float> {
  template <typename FormatContext>
  auto format(ocudu::units::bitrate rate, FormatContext& ctx) const
  {
    return fmt::format_to(ctx.out(), "{}{}", rate.value(), ocudu::units::bitrate::to_string(rate.get_unit()));
  }
};

} // namespace fmt
