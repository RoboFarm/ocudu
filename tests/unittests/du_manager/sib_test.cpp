// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "lib/du/du_high/du_manager/converters/asn1_sys_info_packer.h"
#include "ocudu/asn1/rrc_nr/sys_info.h"
#include "ocudu/du/du_cell_config_helpers.h"
#include "ocudu/ran/sib/system_info_config.h"
#include <chrono>
#include <gtest/gtest.h>
#include <string>

using namespace ocudu;
using namespace odu;

TEST(srs_sib19_test, make_asn1_rrc_cell_sib19_buffer)
{
  sib19_info sib19;
  sib19.ntn_cfg.emplace();
  sib19.ntn_cfg->cell_specific_koffset.emplace(std::chrono::milliseconds(260));
  sib19.ntn_cfg->ephemeris_info.emplace();
  std::get<ecef_coordinates_t>(sib19.ntn_cfg->ephemeris_info.value()).position_x  = 1 * 1.3;
  std::get<ecef_coordinates_t>(sib19.ntn_cfg->ephemeris_info.value()).position_y  = 2 * 1.3;
  std::get<ecef_coordinates_t>(sib19.ntn_cfg->ephemeris_info.value()).position_z  = 3 * 1.3;
  std::get<ecef_coordinates_t>(sib19.ntn_cfg->ephemeris_info.value()).velocity_vx = 4 * 0.06;
  std::get<ecef_coordinates_t>(sib19.ntn_cfg->ephemeris_info.value()).velocity_vy = 5 * 0.06;
  std::get<ecef_coordinates_t>(sib19.ntn_cfg->ephemeris_info.value()).velocity_vz = 6 * 0.06;
  // Call the function being tested
  std::string js_str;
  auto        buf = asn1_packer::pack_sib19(sib19, &js_str);

  // Check that the buffer is not empty
  EXPECT_FALSE(buf.empty());

  // Check that the JSON string is not empty
  EXPECT_FALSE(js_str.empty());

  // Decode the buffer to verify its contents
  asn1::cbit_ref            bref(buf);
  asn1::rrc_nr::sib19_r17_s sib19_decoded;
  asn1::OCUDUASN_CODE       ret = sib19_decoded.unpack(bref);
  EXPECT_EQ(ret, asn1::OCUDUASN_SUCCESS);

  // Check that the decoded SIB19 matches the SIB19 configuration used in the test
  EXPECT_TRUE(sib19_decoded.ntn_cfg_r17_present);
  EXPECT_EQ(sib19_decoded.ntn_cfg_r17.cell_specific_koffset_r17, sib19.ntn_cfg->cell_specific_koffset->count());
  EXPECT_TRUE(sib19_decoded.ntn_cfg_r17.ephemeris_info_r17_present);
  EXPECT_EQ(sib19_decoded.ntn_cfg_r17.ephemeris_info_r17.position_velocity_r17().position_x_r17,
            std::get<ecef_coordinates_t>(sib19.ntn_cfg->ephemeris_info.value()).position_x / 1.3);
}

static asn1::rrc_nr::sib1_s pack_and_unpack_sib1(const odu::du_cell_config& du_cfg)
{
  const byte_buffer buf = asn1_packer::pack_sib1(du_cfg);
  EXPECT_FALSE(buf.empty());

  asn1::cbit_ref       bref(buf);
  asn1::rrc_nr::sib1_s sib1;
  EXPECT_EQ(sib1.unpack(bref), asn1::OCUDUASN_SUCCESS);
  return sib1;
}

TEST(sib1_ta_offset_test, when_ta_offset_is_n25600_then_sib1_contains_n25600)
{
  du_cell_config du_cfg = config_helpers::make_default_du_cell_config();
  du_cfg.ran.ta_offset  = n_ta_offset::n25600;

  const asn1::rrc_nr::sib1_s sib1 = pack_and_unpack_sib1(du_cfg);
  ASSERT_TRUE(sib1.serving_cell_cfg_common_present);
  ASSERT_TRUE(sib1.serving_cell_cfg_common.n_timing_advance_offset_present);
  EXPECT_EQ(sib1.serving_cell_cfg_common.n_timing_advance_offset.value,
            asn1::rrc_nr::serving_cell_cfg_common_sib_s::n_timing_advance_offset_opts::n25600);
}

TEST(sib1_ta_offset_test, when_ta_offset_is_n0_then_sib1_contains_n0)
{
  du_cell_config du_cfg = config_helpers::make_default_du_cell_config();
  du_cfg.ran.ta_offset  = n_ta_offset::n0;

  const asn1::rrc_nr::sib1_s sib1 = pack_and_unpack_sib1(du_cfg);
  ASSERT_TRUE(sib1.serving_cell_cfg_common_present);
  ASSERT_TRUE(sib1.serving_cell_cfg_common.n_timing_advance_offset_present);
  EXPECT_EQ(sib1.serving_cell_cfg_common.n_timing_advance_offset.value,
            asn1::rrc_nr::serving_cell_cfg_common_sib_s::n_timing_advance_offset_opts::n0);
}
