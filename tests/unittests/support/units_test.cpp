// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#include "ocudu/support/units.h"
#include <gtest/gtest.h>

using namespace ocudu;

TEST(bit_units, basic)
{
  using namespace units::literals;

  units::bits a = 2_bits;
  ASSERT_EQ(a.value(), 2);
}

TEST(bit_units, conversion)
{
  using namespace units::literals;

  units::bits a = 10_bits;
  ASSERT_EQ(a.value(), 10);
  ASSERT_EQ(a.truncate_to_bytes().value(), 1);
  ASSERT_EQ(a.round_up_to_bytes().value(), 2);

  units::bits b = 8_bits;
  ASSERT_TRUE(b.is_byte_exact());
}

TEST(byte_units, basic)
{
  using namespace units::literals;

  units::bytes a = 2_bytes;

  // To bit translation methods.
  ASSERT_EQ(a.to_bits(), 16_bits);
  units::bits c = static_cast<units::bits>(a);
  ASSERT_EQ(c, 16_bits);
}

TEST(bitrate_units, basic)
{
  units::bitrate a;
  ASSERT_EQ(a.value(), 0.0F);
  ASSERT_EQ(a.get_unit(), units::bitrate::unit::bit_per_sec);

  units::bitrate b(10.0F, units::bitrate::unit::megabit_per_sec);
  ASSERT_EQ(b.value(), 10.0F);
  ASSERT_EQ(b.get_unit(), units::bitrate::unit::megabit_per_sec);
}

TEST(bitrate_units, conversion)
{
  units::bitrate a(10.0F, units::bitrate::unit::megabit_per_sec);

  // Value in the stored unit is returned as-is.
  ASSERT_EQ(a.to_unit(units::bitrate::unit::megabit_per_sec), 10.0F);

  // Bit units.
  ASSERT_FLOAT_EQ(a.to_unit(units::bitrate::unit::bit_per_sec), 10e6F);
  ASSERT_FLOAT_EQ(a.to_unit(units::bitrate::unit::kilobit_per_sec), 10e3F);
  ASSERT_FLOAT_EQ(a.to_unit(units::bitrate::unit::gigabit_per_sec), 0.01F);

  // Byte units.
  ASSERT_FLOAT_EQ(a.to_unit(units::bitrate::unit::byte_per_sec), 1.25e6F);
  ASSERT_FLOAT_EQ(a.to_unit(units::bitrate::unit::kilobyte_per_sec), 1.25e3F);
  ASSERT_FLOAT_EQ(a.to_unit(units::bitrate::unit::megabyte_per_sec), 1.25F);

  // From byte units to bit units.
  units::bitrate b(1.0F, units::bitrate::unit::gigabyte_per_sec);
  ASSERT_FLOAT_EQ(b.to_unit(units::bitrate::unit::bit_per_sec), 8e9F);
  ASSERT_FLOAT_EQ(b.to_unit(units::bitrate::unit::gigabit_per_sec), 8.0F);
}

TEST(bitrate_units, fmt)
{
  units::bitrate a(10.5F, units::bitrate::unit::megabit_per_sec);
  ASSERT_EQ(fmt::format("{}", a), "10.5Mbps");

  units::bitrate b(2.0F, units::bitrate::unit::kilobyte_per_sec);
  ASSERT_EQ(fmt::format("{}", b), "2kBps");
}
