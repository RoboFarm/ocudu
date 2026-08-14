// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#pragma once

#include "fmt/args.h"
#include "fmt/ranges.h"
#include <vector>

namespace ocudulog {

/// Type trait to indicate if a type that is going to be passed through a log channel is unsafe to be copied with the
/// default implementation and requires a user defined copy implementation.
template <typename T>
struct copy_loggable_type {
  static constexpr bool is_copyable = true;
  // static void copy (fmt::dynamic_format_arg_store<fmt::format_context>* store, <user-defined-type> a)
};

/// \brief Owns a copy of the range captured by an \c fmt::join_view, keeping its separator as a view.
///
/// \c fmt::join_view only stores iterators into the caller's range, so it cannot be handed as-is to a logger backend
/// that may render the log entry asynchronously, after the original range has gone out of scope. This type is what
/// \c ocudulog::copy_loggable_type copies a \c fmt::join_view into instead. The separator is kept as a
/// \c fmt::basic_string_view, same as \c fmt::join_view itself: this assumes, as \c fmt::join callers virtually
/// always do, that the separator is a string literal or otherwise outlives the log entry.
template <typename Container, typename Char = char>
struct owning_join_view {
  Container                    values;
  fmt::basic_string_view<Char> sep;
};

/// \brief Owns a copy of the tuple captured by an \c fmt::tuple_join_view, keeping its separator as a view.
///
/// \c fmt::tuple_join_view is even more directly dangerous than \c fmt::join_view: it stores a plain reference to the
/// caller's tuple, so it dangles the moment that tuple goes out of scope, well before an async logging backend gets a
/// chance to render it. This type is what \c ocudulog::copy_loggable_type copies a \c fmt::tuple_join_view into
/// instead. As with \c owning_join_view, the separator is kept as a view, not owned.
template <typename Tuple, typename Char = char>
struct owning_tuple_join_view {
  Tuple                        values;
  fmt::basic_string_view<Char> sep;
};

/// Type trait specialization to instruct the logger to use a user defined copy implementation as it is unsafe to
/// directly copy an \c fmt::join_view: it only stores iterators into the caller's range, which is not guaranteed to
/// outlive the point where the logging backend asynchronously renders this entry. See \c ocudu::owning_join_view for
/// the separator's lifetime caveat, which this copy does not change.
template <typename It, typename Sentinel, typename Char>
struct copy_loggable_type<fmt::join_view<It, Sentinel, Char>> {
  static constexpr bool is_copyable = false;

  using value_type = std::remove_cv_t<typename std::iterator_traits<It>::value_type>;

  static void copy(fmt::dynamic_format_arg_store<fmt::format_context>* store, fmt::join_view<It, Sentinel, Char> j)
  {
    store->push_back(owning_join_view<std::vector<value_type>, Char>{std::vector<value_type>(j.begin, j.end), j.sep});
  }
};

/// Type trait specialization to instruct the logger to use a user defined copy implementation as it is unsafe to
/// directly copy an \c fmt::tuple_join_view: it only stores a reference to the caller's tuple, which is not
/// guaranteed to outlive the point where the logging backend asynchronously renders this entry. See
/// \c ocudu::owning_tuple_join_view for the separator's lifetime caveat, which this copy does not change.
template <typename Char, typename Tuple>
struct copy_loggable_type<fmt::tuple_join_view<Char, Tuple>> {
  static constexpr bool is_copyable = false;

  static void copy(fmt::dynamic_format_arg_store<fmt::format_context>* store, fmt::tuple_join_view<Char, Tuple> j)
  {
    store->push_back(owning_tuple_join_view<Tuple, Char>{Tuple(j.tuple), j.sep});
  }
};

} // namespace ocudulog

namespace fmt {

/// \brief Custom formatter for \c ocudu::owning_join_view<Container, Char>.
///
/// Reconstructs the \c fmt::join_view that was originally captured, over the owned copy of its range and its
/// (viewed, not owned) separator, and delegates to fmt's own join formatter so that per-element format specifiers
/// keep working as with a plain \c fmt::join call.
template <typename Container, typename Char>
struct formatter<ocudulog::owning_join_view<Container, Char>, Char> {
private:
  using const_iterator = decltype(std::declval<const Container&>().begin());
  using join_type      = join_view<const_iterator, const_iterator, Char>;

  formatter<join_type, Char> underlying;

public:
  template <typename ParseContext>
  auto parse(ParseContext& ctx)
  {
    return underlying.parse(ctx);
  }

  template <typename FormatContext>
  auto format(const ocudulog::owning_join_view<Container, Char>& v, FormatContext& ctx) const
  {
    return underlying.format(join_type(v.values.begin(), v.values.end(), v.sep), ctx);
  }
};

/// \brief Custom formatter for \c ocudu::owning_tuple_join_view<Tuple, Char>.
///
/// Reconstructs the \c fmt::tuple_join_view that was originally captured, referencing the owned copy of the tuple
/// (kept alive for the lifetime of \p v) and its (viewed, not owned) separator, and delegates to fmt's own tuple-join
/// formatter so that per-element formatting behaves as with a plain \c fmt::join call over a tuple.
template <typename Tuple, typename Char>
struct formatter<ocudulog::owning_tuple_join_view<Tuple, Char>, Char> {
private:
  formatter<tuple_join_view<Char, Tuple>, Char> underlying;

public:
  template <typename ParseContext>
  auto parse(ParseContext& ctx)
  {
    return underlying.parse(ctx);
  }

  template <typename FormatContext>
  auto format(const ocudulog::owning_tuple_join_view<Tuple, Char>& v, FormatContext& ctx) const
  {
    return underlying.format(tuple_join_view<Char, Tuple>(v.values, v.sep), ctx);
  }
};

} // namespace fmt
