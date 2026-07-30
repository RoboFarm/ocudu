// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#include "tests/test_doubles/utils/test_rng.h"
#include "ocudu/support/math/bit_ops.h"
#include <fmt/format.h>
#include <gtest/gtest.h>

using namespace ocudu;

template <typename T>
class bitmask_test : public ::testing::Test
{
  static_assert(std::is_unsigned_v<T>, "Invalid type T");

protected:
  using Integer                    = T;
  static constexpr size_t nof_bits = sizeof(Integer) * 8U;
};
using mask_integer_types = ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(bitmask_test, mask_integer_types);

TYPED_TEST(bitmask_test, lsb_ones)
{
  using IntegerType = typename TestFixture::Integer;
  // sanity checks.
  ASSERT_EQ(0, mask_lsb_ones<IntegerType>(0));
  ASSERT_EQ(static_cast<IntegerType>(-1), mask_lsb_ones<IntegerType>(this->nof_bits))
      << "for nof_bits=" << (unsigned)this->nof_bits;
  ASSERT_EQ(0b11, mask_lsb_ones<IntegerType>(2));

  // test all combinations.
  for (unsigned nof_ones = 0; nof_ones != this->nof_bits; ++nof_ones) {
    IntegerType expected = (static_cast<uint64_t>(1U) << nof_ones) - 1U;
    ASSERT_EQ(expected, mask_lsb_ones<IntegerType>(nof_ones)) << "for nof_ones=" << nof_ones;
  }
}

TYPED_TEST(bitmask_test, lsb_zeros)
{
  using IntegerType = typename TestFixture::Integer;
  // sanity checks.
  ASSERT_EQ((IntegerType)-1, mask_lsb_zeros<IntegerType>(0));
  ASSERT_EQ(0, mask_lsb_zeros<IntegerType>(this->nof_bits));

  // test all combinations.
  for (unsigned nof_zeros = 0; nof_zeros != this->nof_bits; ++nof_zeros) {
    IntegerType expected = (static_cast<uint64_t>(1U) << nof_zeros) - 1U;
    expected             = ~expected;
    ASSERT_EQ(expected, mask_lsb_zeros<IntegerType>(nof_zeros)) << "for nof_zeros=" << nof_zeros;
    ASSERT_EQ((IntegerType)~mask_lsb_ones<IntegerType>(nof_zeros), mask_lsb_zeros<IntegerType>(nof_zeros));
  }
}

TYPED_TEST(bitmask_test, msb_ones)
{
  using IntegerType = typename TestFixture::Integer;
  // sanity checks.
  ASSERT_EQ(0, mask_msb_ones<IntegerType>(0));
  ASSERT_EQ(static_cast<IntegerType>(-1), mask_msb_ones<IntegerType>(this->nof_bits));

  // test all combinations.
  for (unsigned nof_ones = 0; nof_ones != this->nof_bits; ++nof_ones) {
    IntegerType expected = 0;
    if (nof_ones > 0) {
      unsigned nof_lsb_zeros = this->nof_bits - nof_ones;
      expected               = ~((static_cast<IntegerType>(1U) << (nof_lsb_zeros)) - 1U);
    }
    ASSERT_EQ(expected, mask_msb_ones<IntegerType>(nof_ones)) << "for nof_ones=" << nof_ones;
  }
}

TYPED_TEST(bitmask_test, msb_zeros)
{
  using IntegerType = typename TestFixture::Integer;
  // sanity checks.
  ASSERT_EQ((IntegerType)-1, mask_msb_zeros<IntegerType>(0));
  ASSERT_EQ(0, mask_msb_zeros<IntegerType>(this->nof_bits));

  // test all combinations.
  for (unsigned nof_zeros = 0; nof_zeros != this->nof_bits; ++nof_zeros) {
    IntegerType expected = 0;
    if (nof_zeros > 0) {
      unsigned nof_lsb_ones = this->nof_bits - nof_zeros;
      expected              = ~((static_cast<IntegerType>(1U) << (nof_lsb_ones)) - 1U);
    }
    expected = ~expected;
    ASSERT_EQ(expected, mask_msb_zeros<IntegerType>(nof_zeros)) << "for nof_zeros=" << nof_zeros;
    ASSERT_EQ((IntegerType)~mask_lsb_ones<IntegerType>(nof_zeros), mask_lsb_zeros<IntegerType>(nof_zeros));
  }
}

TYPED_TEST(bitmask_test, first_lsb_one)
{
  using IntegerType = typename TestFixture::Integer;
  std::uniform_int_distribution<IntegerType> rd_int{0, std::numeric_limits<IntegerType>::max()};

  // sanity checks.
  ASSERT_EQ(std::numeric_limits<IntegerType>::digits, find_first_lsb_one<IntegerType>(0));
  ASSERT_EQ(0, find_first_lsb_one<IntegerType>(-1));
  ASSERT_EQ(0, find_first_lsb_one<IntegerType>(0b1));
  ASSERT_EQ(1, find_first_lsb_one<IntegerType>(0b10));
  ASSERT_EQ(0, find_first_lsb_one<IntegerType>(0b11));

  // test all combinations.
  for (unsigned one_idx = 0; one_idx != this->nof_bits - 1; ++one_idx) {
    IntegerType mask  = mask_lsb_zeros<IntegerType>(one_idx);
    IntegerType value = test_rng::uniform_int<IntegerType>() & mask;

    ASSERT_EQ(find_first_lsb_one(mask), one_idx);
    ASSERT_GE(find_first_lsb_one(value), one_idx) << fmt::format("for value {:#b}", value);
  }
}

TYPED_TEST(bitmask_test, first_msb_one)
{
  using IntegerType = typename TestFixture::Integer;

  // sanity checks.
  ASSERT_EQ(std::numeric_limits<IntegerType>::digits, find_first_msb_one<IntegerType>(0));
  ASSERT_EQ(this->nof_bits - 1, find_first_msb_one<IntegerType>(-1));
  ASSERT_EQ(0, find_first_msb_one<IntegerType>(0b1));
  ASSERT_EQ(1, find_first_msb_one<IntegerType>(0b10));
  ASSERT_EQ(1, find_first_msb_one<IntegerType>(0b11));

  // test all combinations.
  for (unsigned one_idx = 0; one_idx != this->nof_bits - 1; ++one_idx) {
    IntegerType mask  = mask_lsb_ones<IntegerType>(one_idx + 1);
    IntegerType value = std::max<IntegerType>(test_rng::uniform_int<IntegerType>() & mask, 1U);

    ASSERT_EQ(one_idx, find_first_msb_one(mask));
    ASSERT_LE(find_first_msb_one(value), one_idx) << fmt::format("for value {:#b}", value);
  }
}
