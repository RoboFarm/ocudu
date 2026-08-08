// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#pragma once

#include "fmt/format.h"
#include "fmt/ranges.h"
#include <complex>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ocudu {

class bit_buffer;

class byte_buffer;
class byte_buffer_slice;
class byte_buffer_view;

class byte_buffer_chain;

struct cbf16_t;
std::complex<float> to_cf(cbf16_t value);

template <typename Integer, Integer MIN_VALUE, Integer MAX_VALUE>
class bounded_integer;

template <size_t N, bool LowestInfoBitIsMSB, typename Tag>
class bounded_bitset;

template <typename T>
class span;

template <typename T, std::size_t MAX_N>
class static_vector;

template <typename T, bool RightClosed, typename Tag>
class interval;

} // namespace ocudu

namespace fmt {

template <>
struct is_range<ocudu::byte_buffer_view, char> : std::false_type {};

/// \brief Custom formatter for byte_buffer_view.
template <>
struct formatter<ocudu::byte_buffer_view> {
  enum { hexadecimal, binary } mode = hexadecimal;

  template <typename ParseContext>
  auto parse(ParseContext& ctx)
  {
    auto it = ctx.begin();
    while (it != ctx.end() and *it != '}') {
      if (*it == 'b') {
        mode = binary;
      }
      ++it;
    }
    return it;
  }

  template <typename T, typename FormatContext>
  auto format(const T& buf, FormatContext& ctx) const
  {
    if (mode == hexadecimal) {
      return format_to(ctx.out(), "{:0>2x}", fmt::join(buf.begin(), buf.end(), " "));
    }
    return format_to(ctx.out(), "{:0>8b}", fmt::join(buf.begin(), buf.end(), " "));
  }
};

template <>
struct is_range<ocudu::byte_buffer, char> : std::false_type {};

/// \brief Custom formatter for byte_buffer.
template <>
struct formatter<ocudu::byte_buffer> {
  enum { hexadecimal, binary } mode = hexadecimal;

  template <typename ParseContext>
  auto parse(ParseContext& ctx)
  {
    auto it = ctx.begin();
    while (it != ctx.end() and *it != '}') {
      if (*it == 'b') {
        mode = binary;
      }
      ++it;
    }
    return it;
  }

  template <typename T, typename FormatContext>
  auto format(const T& buf, FormatContext& ctx) const
  {
    if (mode == hexadecimal) {
      return format_to(ctx.out(), "{:0>2x}", fmt::join(buf.begin(), buf.end(), " "));
    }
    return format_to(ctx.out(), "{:0>8b}", fmt::join(buf.begin(), buf.end(), " "));
  }
};

template <>
struct is_range<ocudu::byte_buffer_slice, char> : std::false_type {};

/// \brief Custom formatter for byte_buffer_slice.
template <>
struct formatter<ocudu::byte_buffer_slice> : public formatter<ocudu::byte_buffer_view> {
  template <typename T, typename FormatContext>
  auto format(const T& buf, FormatContext& ctx) const
  {
    return formatter<ocudu::byte_buffer_view>::format(buf.view(), ctx);
  }
};

template <>
struct is_range<ocudu::byte_buffer_chain, char> : std::false_type {};

/// \brief Custom formatter for byte_buffer_chain.
template <>
struct formatter<ocudu::byte_buffer_chain> : public formatter<ocudu::byte_buffer_view> {
  template <typename T, typename FormatContext>
  auto format(const T& buf, FormatContext& ctx) const
  {
    if (mode == hexadecimal) {
      return format_to(ctx.out(), "{:0>2x}", fmt::join(buf.begin(), buf.end(), " "));
    }
    return format_to(ctx.out(), "{:0>8b}", fmt::join(buf.begin(), buf.end(), " "));
  }
};

template <>
struct formatter<ocudu::bit_buffer> {
  enum { hexadecimal, binary } mode = binary;

  template <typename ParseContext>
  auto parse(ParseContext& ctx)
  {
    auto it = ctx.begin();
    while (it != ctx.end() and *it != '}') {
      if (*it == 'x') {
        mode = hexadecimal;
      }
      ++it;
    }

    return it;
  }

  template <typename T, typename FormatContext>
  auto format(const T& s, FormatContext& ctx) const
  {
    if (mode == hexadecimal) {
      return s.template to_hex_string<decltype(std::declval<FormatContext>().out())>(ctx.out());
    }
    return s.template to_bin_string<decltype(std::declval<FormatContext>().out())>(ctx.out());
  }
};

/// \brief Custom formatter for bounded_bitset<N, LowestInfoBitIsMSB, Tag>
template <size_t N, bool LowestInfoBitIsMSB, typename Tag>
struct formatter<ocudu::bounded_bitset<N, LowestInfoBitIsMSB, Tag>> {
  enum { hexadecimal, binary, bit_positions, intervals } mode = binary;
  enum { forward, reverse } order                             = forward;
  template <typename ParseContext>
  auto parse(ParseContext& ctx)
  {
    auto it = ctx.begin();
    while (it != ctx.end() and *it != '}') {
      if (*it == 'x') {
        mode = hexadecimal;
      }
      if (*it == 'r') {
        order = reverse;
      }
      if (*it == 'n') {
        mode = bit_positions;
      }
      if (*it == 'i') {
        mode = intervals;
      }
      ++it;
    }

    return it;
  }

  template <typename FormatContext>
  auto format(const ocudu::bounded_bitset<N, LowestInfoBitIsMSB, Tag>& s, FormatContext& ctx) const
  {
    if (mode == hexadecimal) {
      return s.template to_string_of_hex<decltype(std::declval<FormatContext>().out())>(ctx.out(), order == reverse);
    }

    if (mode == intervals) {
      bool first = true;
      fmt::format_to(ctx.out(), "{{");
      for_each_interval(s, [&first, &ctx](size_t start_interval, size_t end_interval) {
        // Append a comma if the interval is not the first.
        if (first) {
          first = false;
        } else {
          fmt::format_to(ctx.out(), ", ");
        }

        // Print interval if it is more than one bit, otherwise a single value.
        if (end_interval - start_interval > 1) {
          fmt::format_to(ctx.out(), "[{}, {})", start_interval, end_interval);
        } else {
          fmt::format_to(ctx.out(), "{}", start_interval);
        }
      });
      fmt::format_to(ctx.out(), "}}");
      return ctx.out();
    }

    if (mode == bit_positions) {
      if (s.empty()) {
        fmt::format_to(ctx.out(), "empty");
      } else if (s.count() == 0) {
        fmt::format_to(ctx.out(), "none");
      } else if (s.is_contiguous()) {
        unsigned lowest  = s.find_lowest();
        unsigned highest = s.find_highest();
        if (lowest == highest) {
          // Single value.
          fmt::format_to(ctx.out(), "{}", lowest);
        } else {
          // Format as a range.
          fmt::format_to(ctx.out(), "[{}, {})", lowest, highest + 1);
        }

      } else {
        // Format as a list of bit positions.
        ocudu::static_vector<size_t, N> bit_pos = s.get_bit_positions();

        fmt::format_to(ctx.out(), "{}", ocudu::span<size_t>(bit_pos));
      }
      return ctx.out();
    }

    return s.template to_string_of_bits<decltype(std::declval<FormatContext>().out())>(ctx.out(), order == reverse);
  }
};

/// Formatter for bounded_integer<...> types.
template <typename Integer, Integer MIN_VALUE, Integer MAX_VALUE>
struct formatter<ocudu::bounded_integer<Integer, MIN_VALUE, MAX_VALUE>> : public formatter<Integer> {
  template <typename FormatContext>
  auto format(const ocudu::bounded_integer<Integer, MIN_VALUE, MAX_VALUE>& s, FormatContext& ctx) const
  {
    if (s.valid()) {
      return fmt::format_to(ctx.out(), "{}", static_cast<Integer>(s));
    }
    return fmt::format_to(ctx.out(), "INVALID");
  }
};

/// Format intervals with the notation [start, stop)
template <typename T, bool RightClosed, typename Tag>
struct formatter<ocudu::interval<T, RightClosed, Tag>> : public formatter<T> {
  template <typename FormatContext>
  auto format(const ocudu::interval<T, RightClosed, Tag>& interv, FormatContext& ctx) const
  {
    return format_to(ctx.out(),
                     "[{}{}{}{}",
                     interv.start(),
                     ocudu::interval<T, RightClosed, Tag>::is_real::value ? ", " : "..",
                     interv.stop(),
                     RightClosed ? ']' : ')');
  }
};

template <typename T>
struct is_range<ocudu::span<T>, char> : std::false_type {};

/// \brief Custom formatter for \c span<T>.
///
/// By default, the elements within the span are separated by a space character. A comma delimiter is available and can
/// be selected by formatting with <tt>{:,}</tt>. The delimiter can be disabled by formatting with <tt>{:#}</tt>.
template <typename T>
struct formatter<ocudu::span<T>> {
  // Stores parsed format string.
  memory_buffer format_buffer;

  // Stores parsed delimiter string.
  memory_buffer delimiter_buffer;

  formatter()
  {
    static constexpr std::string_view DEFAULT_FORMAT    = "{}";
    static constexpr std::string_view DEFAULT_DELIMITER = " ";
    format_buffer.append(DEFAULT_FORMAT.begin(), DEFAULT_FORMAT.end());
    delimiter_buffer.append(DEFAULT_DELIMITER.begin(), DEFAULT_DELIMITER.end());
  }

  template <typename ParseContext>
  auto parse(ParseContext& ctx)
  {
    static constexpr std::string_view PREAMBLE_FORMAT = "{:";
    static constexpr std::string_view COMMA_DELIMITER = ", ";

    // Skip if context is empty and use default format.
    if (ctx.begin() == ctx.end()) {
      return ctx.end();
    }

    // Store the format string.
    format_buffer.clear();
    format_buffer.append(PREAMBLE_FORMAT.begin(), PREAMBLE_FORMAT.end());
    for (auto& it : ctx) {
      // Detect if comma is in the context.
      if (it == ',') {
        delimiter_buffer.clear();
        delimiter_buffer.append(COMMA_DELIMITER.begin(), COMMA_DELIMITER.end());
        continue;
      }

      // Detect if the hash sign is in the context. This indicates no delimiter between entries.
      if (it == '#') {
        delimiter_buffer.clear();
        continue;
      }

      format_buffer.push_back(it);

      // Found the end of the context.
      if (it == '}') {
        return &it;
      }
    }

    // No end of context was found.
    return ctx.end();
  }

  template <typename FormatContext>
  auto format(ocudu::span<T> buf, FormatContext& ctx) const
  {
    string_view format_str    = string_view(format_buffer.data(), format_buffer.size());
    string_view delimiter_str = string_view(delimiter_buffer.data(), delimiter_buffer.size());
    return format_to(ctx.out(), format_str, fmt::join(buf.begin(), buf.end(), delimiter_str));
  }
};

template <typename T>
struct is_range<std::vector<T>, char> : std::false_type {};

/// Custom formatter used by the \c copy_loggable_type defined below.
template <typename T>
struct formatter<std::vector<T>> : public formatter<ocudu::span<T>> {
  using formatter<ocudu::span<T>>::delimiter_buffer;
  using formatter<ocudu::span<T>>::format_buffer;

  template <typename FormatContext>
  auto format(const std::vector<T>& buf, FormatContext& ctx) const
  {
    string_view format_str    = string_view(format_buffer.data(), format_buffer.size());
    string_view delimiter_str = string_view(delimiter_buffer.data(), delimiter_buffer.size());
    return format_to(ctx.out(), format_str, fmt::join(buf.begin(), buf.end(), delimiter_str));
  }
};

template <typename T, size_t N>
struct is_range<ocudu::static_vector<T, N>, char> : std::false_type {};

/// Custom formatter used by the \c copy_loggable_type defined below.
template <typename T, size_t N>
struct formatter<ocudu::static_vector<T, N>> : public formatter<ocudu::span<T>> {
  using formatter<ocudu::span<T>>::delimiter_buffer;
  using formatter<ocudu::span<T>>::format_buffer;

  template <typename FormatContext>
  auto format(const ocudu::static_vector<T, N>& buf, FormatContext& ctx) const
  {
    string_view format_str    = string_view(format_buffer.data(), format_buffer.size());
    string_view delimiter_str = string_view(delimiter_buffer.data(), delimiter_buffer.size());
    return format_to(ctx.out(), format_str, fmt::join(buf.begin(), buf.end(), delimiter_str));
  }
};

/// FMT formatter shared by \c std::complex specializations.
template <typename ComplexType>
struct formatter_template {
  // Stores parsed format string.
  memory_buffer format_buffer;

  formatter_template()
  {
    static constexpr std::string_view DEFAULT_FORMAT =
        (std::is_same<ComplexType, std::complex<float>>::value) ? "{:+f}{:+f}j" : "{:+d}{:+d}j";
    format_buffer.append(DEFAULT_FORMAT.begin(), DEFAULT_FORMAT.end());
  }

  template <typename ParseContext>
  auto parse(ParseContext& ctx)
  {
    static constexpr std::string_view PREAMBLE_FORMAT = "{:";

    // Skip if context is empty and use default format.
    if (ctx.begin() == ctx.end()) {
      return ctx.end();
    }

    // Store the format string.
    format_buffer.clear();
    format_buffer.append(PREAMBLE_FORMAT.begin(), PREAMBLE_FORMAT.end());
    for (auto& it : ctx) {
      format_buffer.push_back(it);

      // Found the end of the context.
      if (it == '}') {
        // Replicate the format string for the imaginary part.
        format_buffer.append(format_buffer.begin(), format_buffer.end());
        format_buffer.push_back('j');
        return &it;
      }
    }

    // No end of context was found.
    return ctx.end();
  }

  template <typename FormatContext>
  auto format(ComplexType value, FormatContext& ctx) const
  {
    const string_view format_str = string_view(format_buffer.data(), format_buffer.size());
    return format_to(ctx.out(), format_str, value.real(), value.imag());
  }
};

template <>
struct formatter<std::complex<float>> : public formatter_template<std::complex<float>> {};
template <>
struct formatter<std::complex<int8_t>> : public formatter_template<std::complex<int8_t>> {};
template <>
struct formatter<std::complex<int16_t>> : public formatter_template<std::complex<int16_t>> {};

/// FMT formatter of cbf16_t type.
template <>
struct formatter<ocudu::cbf16_t> {
  formatter_template<std::complex<float>> cf_formatter;

  template <typename ParseContext>
  auto parse(ParseContext& ctx)
  {
    return cf_formatter.parse(ctx);
  }

  template <typename T, typename FormatContext>
  auto format(const T& value, FormatContext& ctx) const
  {
    return cf_formatter.format(ocudu::to_cf(value), ctx);
  }
};

} // namespace fmt
