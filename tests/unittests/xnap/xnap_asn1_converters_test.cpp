// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "lib/xnap/xnap_asn1_converters.h"
#include <gtest/gtest.h>

using namespace ocudu;
using namespace ocudu::ocucp;

/// The GUAMI must survive a round trip with a PLMN other than the test PLMN.
///
/// Note the deliberate use of a non-test PLMN: guami_t initialises its PLMN member to
/// plmn_identity::test_value(), so a conversion that fails to write the decoded PLMN still returns
/// 00101 and a round trip using the test PLMN passes either way.
TEST(xnap_asn1_converters_test, guami_round_trip_preserves_non_test_plmn)
{
  const guami_t orig{
      .plmn = plmn_identity::parse("20893").value(), .amf_set_id = 1, .amf_pointer = 1, .amf_region_id = 1};

  const expected<guami_t, std::string> decoded = asn1_to_guami(guami_to_asn1(orig));

  ASSERT_TRUE(decoded.has_value()) << decoded.error();
  EXPECT_EQ(decoded.value().plmn, orig.plmn);
  EXPECT_EQ(decoded.value().amf_region_id, orig.amf_region_id);
  EXPECT_EQ(decoded.value().amf_set_id, orig.amf_set_id);
  EXPECT_EQ(decoded.value().amf_pointer, orig.amf_pointer);
}

/// The subcarrier spacing a peer reports for one of its cells is decoded rather than left invalid.
TEST(xnap_asn1_converters_test, subcarrier_spacing_of_a_served_cell_is_decoded)
{
  asn1::xnap::nr_mode_info_c asn1_nr_mode_info;
  auto&                      asn1_tdd_info = asn1_nr_mode_info.set_tdd();
  asn1_tdd_info.nr_transmisson_bw.nr_scs   = asn1::xnap::nr_scs_opts::scs30;
  asn1_tdd_info.nr_transmisson_bw.nr_nrb   = asn1::xnap::nr_nrb_opts::nrb51;

  const cu_cp_nr_mode_info decoded = asn1_to_nr_mode_info(asn1_nr_mode_info);

  ASSERT_TRUE(std::holds_alternative<cu_cp_tdd_info>(decoded));
  EXPECT_EQ(std::get<cu_cp_tdd_info>(decoded).tx_bw.nr_scs, subcarrier_spacing::kHz30);
}

/// A GUAMI whose PLMN bytes are not valid BCD is reported as an error rather than silently accepted.
TEST(xnap_asn1_converters_test, guami_with_invalid_plmn_bytes_is_rejected)
{
  asn1::xnap::guami_s asn1_guami;
  asn1_guami.plmn_id.from_number(0xffffff);
  asn1_guami.amf_region_id.from_number(1);
  asn1_guami.amf_set_id.from_number(1);
  asn1_guami.amf_pointer.from_number(1);

  EXPECT_FALSE(asn1_to_guami(asn1_guami).has_value());
}
