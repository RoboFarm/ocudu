// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ephemeris_info_converter.h"
#include <algorithm>
#include <array>
#include <cmath>

using namespace ocudu;
using namespace ocudu_ntn;

/// Earth's gravitational parameter (GM) [m^3/s^2].
static constexpr double MU = 3.986004418e14;

/// Solve Kepler's equation M = E - e * sin(E) for the eccentric anomaly E, by Newton-Raphson iteration.
static double
solve_kepler_equation(double mean_anomaly, double eccentricity, double tolerance = 1e-12, unsigned max_iterations = 100)
{
  double eccentric_anomaly = (eccentricity < 0.8) ? mean_anomaly : M_PI;
  for (unsigned i = 0; i != max_iterations; ++i) {
    double f       = eccentric_anomaly - eccentricity * std::sin(eccentric_anomaly) - mean_anomaly;
    double f_prime = 1.0 - eccentricity * std::cos(eccentric_anomaly);
    double delta   = f / f_prime;
    eccentric_anomaly -= delta;
    if (std::abs(delta) < tolerance) {
      break;
    }
  }
  return eccentric_anomaly;
}

orbital_elements ephemeris_info_converter::eci_to_orbital(const state_vector& eci_state)
{
  const auto& position = eci_state.position;
  const auto& velocity = eci_state.velocity;
  // Specific angular momentum.
  std::array<double, 3> h = {position.y * velocity.z - position.z * velocity.y,
                             position.z * velocity.x - position.x * velocity.z,
                             position.x * velocity.y - position.y * velocity.x};

  // Node vector.
  std::array<double, 3> n = {-h[1], h[0], 0.0};

  // Eccentricity vector.
  double                v2 = velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z;
  double                r  = std::sqrt(position.x * position.x + position.y * position.y + position.z * position.z);
  double                r_dot_v = position.x * velocity.x + position.y * velocity.y + position.z * velocity.z;
  std::array<double, 3> ev      = {((v2 - MU / r) * position.x - r_dot_v * velocity.x) / MU,
                                   ((v2 - MU / r) * position.y - r_dot_v * velocity.y) / MU,
                                   ((v2 - MU / r) * position.z - r_dot_v * velocity.z) / MU};

  // Semi-major axis.
  double semi_major_axis = -MU / (2.0 * (v2 / 2.0 - MU / r));

  // Eccentricity.
  double eccentricity = std::sqrt(ev[0] * ev[0] + ev[1] * ev[1] + ev[2] * ev[2]);

  // Inclination.
  double h_mag       = std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
  double inclination = std::acos(std::clamp(h[2] / h_mag, -1.0, 1.0));

  // Longitude of ascending node.
  double n_mag     = std::sqrt(n[0] * n[0] + n[1] * n[1]);
  double longitude = std::atan2(n[1], n[0]);
  if (longitude < 0) {
    longitude += 2.0 * M_PI;
  }

  // Argument of periapsis.
  double ev_mag    = std::sqrt(ev[0] * ev[0] + ev[1] * ev[1] + ev[2] * ev[2]);
  double periapsis = std::acos(std::clamp((n[0] * ev[0] + n[1] * ev[1]) / (n_mag * ev_mag), -1.0, 1.0));
  if (ev[2] < 0) {
    periapsis = 2.0 * M_PI - periapsis;
  }

  // True anomaly.
  double cos_ta       = (ev[0] * position.x + ev[1] * position.y + ev[2] * position.z) / (ev_mag * r);
  double true_anomaly = std::acos(std::clamp(cos_ta, -1.0, 1.0));
  if (r_dot_v < 0) {
    true_anomaly = 2.0 * M_PI - true_anomaly;
  }

  // Eccentric anomaly.
  double eccentric_anomaly =
      2.0 * std::atan(std::sqrt((1.0 - eccentricity) / (1.0 + eccentricity)) * std::tan(true_anomaly / 2.0));

  // Mean anomaly, from Kepler's equation M = E - e * sin(E).
  double mean_anomaly = eccentric_anomaly - eccentricity * std::sin(eccentric_anomaly);
  if (mean_anomaly < 0) {
    mean_anomaly += 2.0 * M_PI;
  }

  return {semi_major_axis, eccentricity, inclination, longitude, periapsis, mean_anomaly};
}

state_vector ephemeris_info_converter::orbital_to_eci(const orbital_elements& oe)
{
  // Solve Kepler's equation for eccentric anomaly.
  double eccentric_anomaly = solve_kepler_equation(oe.mean_anomaly, oe.eccentricity);

  // True anomaly.
  double true_anomaly = 2.0 * std::atan2(std::sqrt(1 + oe.eccentricity) * std::sin(eccentric_anomaly / 2.0),
                                         std::sqrt(1 - oe.eccentricity) * std::cos(eccentric_anomaly / 2.0));

  // Orbital radius, i.e. the distance from the focus to the satellite.
  double r = oe.semi_major_axis * (1 - oe.eccentricity * std::cos(eccentric_anomaly));

  // Semi-latus rectum.
  double p = oe.semi_major_axis * (1 - oe.eccentricity * oe.eccentricity);

  // Position in perifocal (PQW) frame.
  double x_p = r * std::cos(true_anomaly);
  double y_p = r * std::sin(true_anomaly);
  double z_p = 0.0;

  // Velocity in perifocal (PQW) frame.
  double vx_p = -std::sqrt(MU / p) * std::sin(true_anomaly);
  double vy_p = std::sqrt(MU / p) * (oe.eccentricity + std::cos(true_anomaly));
  double vz_p = 0.0;

  // Rotation matrix components.
  double cos_raan = std::cos(oe.longitude);
  double sin_raan = std::sin(oe.longitude);
  double cos_i    = std::cos(oe.inclination);
  double sin_i    = std::sin(oe.inclination);
  double cos_w    = std::cos(oe.periapsis);
  double sin_w    = std::sin(oe.periapsis);

  // Perifocal to ECI transformation, the 3-1-3 rotation R_z(-raan) * R_x(-inclination) * R_z(-omega).
  double r11 = cos_raan * cos_w - sin_raan * sin_w * cos_i;
  double r12 = -cos_raan * sin_w - sin_raan * cos_w * cos_i;
  double r13 = sin_raan * sin_i;
  double r21 = sin_raan * cos_w + cos_raan * sin_w * cos_i;
  double r22 = -sin_raan * sin_w + cos_raan * cos_w * cos_i;
  double r23 = -cos_raan * sin_i;
  double r31 = sin_w * sin_i;
  double r32 = cos_w * sin_i;
  double r33 = cos_i;

  // Create state_vector and fill position and velocity.
  state_vector rv;
  rv.position.x = r11 * x_p + r12 * y_p + r13 * z_p;
  rv.position.y = r21 * x_p + r22 * y_p + r23 * z_p;
  rv.position.z = r31 * x_p + r32 * y_p + r33 * z_p;
  rv.velocity.x = r11 * vx_p + r12 * vy_p + r13 * vz_p;
  rv.velocity.y = r21 * vx_p + r22 * vy_p + r23 * vz_p;
  rv.velocity.z = r31 * vx_p + r32 * vy_p + r33 * vz_p;

  return rv;
}
