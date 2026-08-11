// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "mac_cell_controller_impl.h"
#include "../mac_dl/mac_dl_configurator.h"

using namespace ocudu;

mac_cell_controller_impl::mac_cell_controller_impl(const mac_cell_creation_request& cell_cfg,
                                                   timer_factory                    timers,
                                                   mac_scheduler_cell_configurator& sched,
                                                   mac_dl_cell_controller&          dl_cell_) :
  si_mng(cell_cfg.cell_index, cell_cfg.sys_info, timers, sched), dl_cell(dl_cell_)
{
  // Start broadcasting the System Information the cell was created with.
  dl_cell.start_broadcast(si_mng.extension_handler(), si_mng.last_command());
}

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
  return launch_async([this, request, resp = mac_cell_reconfig_response{}, dl_req = mac_dl_cell_reconfig_request{}](
                          coro_context<async_task<mac_cell_reconfig_response>>& ctx) mutable {
    CORO_BEGIN(ctx);

    if (request.new_sys_info.has_value()) {
      // SI message update with SI change notifications and/or SIB1 valueTag update.
      dl_req.si_update = si_mng.handle_si_change_request(*request.new_sys_info);
      resp.si_updated  = dl_req.si_update.has_value();
    }

    if (request.new_si_pdu_info.has_value()) {
      // SI message update without SIB1 valueTag update.
      resp.si_pdus_enqueued = si_mng.handle_si_message_pdu_updates(*request.new_si_pdu_info);
    }

    dl_req.slice_reconf_req = request.slice_reconf_req;
    dl_req.ntn_ul_ta_update = request.ntn_ul_ta_update;

    CORO_AWAIT(dl_cell.reconfigure(dl_req));

    CORO_RETURN(resp);
  });
}
