// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "mac_cell_controller_impl.h"
#include "../mac_dl/mac_dl_configurator.h"

using namespace ocudu;

mac_cell_controller_impl::mac_cell_controller_impl(mac_dl_cell_controller& dl_cell_) : dl_cell(dl_cell_) {}

async_task<void> mac_cell_controller_impl::start()
{
  return dl_cell.start();
}

async_task<void> mac_cell_controller_impl::stop()
{
  return dl_cell.stop();
}

async_task<mac_cell_reconfig_response> mac_cell_controller_impl::reconfigure(const mac_cell_reconfig_request& request)
{
  return dl_cell.reconfigure(request);
}
