// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "../du_manager_test_helpers.h"
#include "ocudu/adt/format.h"
#include "ocudu/du/du_cell_config_helpers.h"
#include "ocudu/du/du_high/du_manager/du_manager_factory.h"
#include "ocudu/support/async/eager_async_task.h"
#include "ocudu/support/enum_utils.h"
#include "ocudu/support/executors/task_worker.h"
#include <gtest/gtest.h>

using namespace ocudu;
using namespace odu;

namespace {

class du_cell_lock_tester
{
public:
  du_cell_lock_tester() :
    cell_cfgs({config_helpers::make_default_du_cell_config()}),
    dependencies(cell_cfgs),
    du_mng(create_du_manager(dependencies.params))
  {
    // Pre-arm the F1 setup and MAC cell start/stop responses so the DU can come up.
    dependencies.f1ap.wait_f1_setup.result.value().cells_to_activate.resize(cell_cfgs.size());
    for (unsigned i = 0; i != cell_cfgs.size(); ++i) {
      dependencies.f1ap.wait_f1_setup.result.value().cells_to_activate[i].cgi = cell_cfgs[i].nr_cgi;
    }
    dependencies.f1ap.wait_f1_setup.ready_ev.set();
    dependencies.f1ap.wait_f1_removal.ready_ev.set();
    dependencies.mac.mac_cell.wait_start.ready_ev.set();
    dependencies.mac.mac_cell.wait_stop.ready_ev.set();

    du_mng->get_controller().start();

    // Clear any reconfigure request captured during setup so the test observes only the
    // events triggered by the lock/unlock under test.
    dependencies.mac.mac_cell.last_cell_recfg_req.reset();
  }

  ~du_cell_lock_tester()
  {
    std::atomic<bool> done{false};
    worker.push_task_blocking([this, &done]() {
      du_mng->get_controller().stop();
      done = true;
    });
    while (not done) {
      dependencies.worker.run_pending_tasks();
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    worker.wait_pending_tasks();
  }

  /// Tick timers and run pending tasks for at least `nof_ticks` iterations or until `pred` returns true.
  template <typename Pred>
  void pump_until(unsigned max_ticks, Pred&& pred)
  {
    for (unsigned i = 0; i < max_ticks; ++i) {
      if (pred()) {
        return;
      }
      dependencies.timers.tick();
      dependencies.worker.run_pending_tasks();
    }
  }

  void pump(unsigned nof_ticks)
  {
    for (unsigned i = 0; i < nof_ticks; ++i) {
      dependencies.timers.tick();
      dependencies.worker.run_pending_tasks();
    }
  }

  task_worker                 worker{"worker", 16};
  std::vector<du_cell_config> cell_cfgs;
  du_manager_test_bench       dependencies;
  std::unique_ptr<du_manager> du_mng;
};

class du_cell_lock_test : public du_cell_lock_tester, public ::testing::Test
{};

} // namespace

TEST_F(du_cell_lock_test, when_cu_deactivates_cell_then_mib_cell_barred_is_set_true)
{
  // CU sends gNB-CU Configuration Update with the cell in cells_to_be_deactivated_list.
  gnbcu_config_update_request req;
  req.cells_to_deactivate.push_back(cell_cfgs[0].nr_cgi);

  async_task<gnbcu_config_update_response> resp_task =
      du_mng->get_f1ap_event_handler().handle_cu_context_update_request(req);
  lazy_task_launcher<gnbcu_config_update_response> launcher(resp_task);

  // The graceful stop procedure bars the cell and drains its UEs concurrently, holding the cell until the
  // SSB-period-derived settling window elapses, then stops MAC. Pump enough ticks for the timer waits.
  pump_until(500, [&]() { return launcher.ready(); });

  ASSERT_TRUE(launcher.ready()) << "Stop procedure did not complete in time";

  // The bar-first step issues a MAC cell reconfigure with cell_barred_mod=true. The mock
  // captures the most recent reconfigure; with no UEs to drain and no later reconfigure
  // call in the stop path, this is the bar-first request.
  ASSERT_TRUE(dependencies.mac.mac_cell.last_cell_recfg_req.has_value()) << "MAC cell reconfigure was never invoked";
  ASSERT_TRUE(dependencies.mac.mac_cell.last_cell_recfg_req->cell_barred_mod.has_value())
      << "Reconfigure did not carry cell_barred_mod";
  ASSERT_TRUE(dependencies.mac.mac_cell.last_cell_recfg_req->cell_barred_mod.value())
      << "Bar-first should set cell_barred_mod=true";
}

TEST_F(du_cell_lock_test, when_cu_activates_cell_after_deactivate_then_mib_cell_barred_is_restored)
{
  // First deactivate the cell.
  gnbcu_config_update_request deact_req;
  deact_req.cells_to_deactivate.push_back(cell_cfgs[0].nr_cgi);
  async_task<gnbcu_config_update_response> deact_task =
      du_mng->get_f1ap_event_handler().handle_cu_context_update_request(deact_req);
  lazy_task_launcher<gnbcu_config_update_response> deact_launcher(deact_task);
  pump_until(500, [&]() { return deact_launcher.ready(); });
  ASSERT_TRUE(deact_launcher.ready());

  // Sanity check: deactivate set cell_barred_mod=true.
  ASSERT_TRUE(dependencies.mac.mac_cell.last_cell_recfg_req.has_value());
  ASSERT_TRUE(dependencies.mac.mac_cell.last_cell_recfg_req->cell_barred_mod.value_or(false));

  // Now activate the cell. du_cell_manager::start should reconfigure the MAC with the
  // configured cell_barred (false by default), restoring the live MIB after the bar-first
  // stop transient.
  gnbcu_config_update_request act_req;
  f1ap_cell_to_activate       cell_act{};
  cell_act.cgi = cell_cfgs[0].nr_cgi;
  act_req.cells_to_activate.push_back(cell_act);

  async_task<gnbcu_config_update_response> act_task =
      du_mng->get_f1ap_event_handler().handle_cu_context_update_request(act_req);
  lazy_task_launcher<gnbcu_config_update_response> act_launcher(act_task);
  pump_until(200, [&]() { return act_launcher.ready(); });

  ASSERT_TRUE(act_launcher.ready()) << "Start procedure did not complete in time";

  // The most recent reconfigure should be the cell_barred restore step (set to the
  // configured value, which is false by default).
  ASSERT_TRUE(dependencies.mac.mac_cell.last_cell_recfg_req.has_value());
  ASSERT_TRUE(dependencies.mac.mac_cell.last_cell_recfg_req->cell_barred_mod.has_value())
      << "Activate should reconfigure cell_barred to the configured value";
  ASSERT_FALSE(dependencies.mac.mac_cell.last_cell_recfg_req->cell_barred_mod.value())
      << "Configured cell_barred is false; activate should restore it";
}

TEST_F(du_cell_lock_test, when_graceful_stop_then_bar_is_issued_at_once_and_cell_is_held_until_settling)
{
  // The settling window is derived from the cell's configured SSB period (two periods), not a hardcoded
  // constant, so the test tracks the same source as the procedure.
  const unsigned settling_ms = 2 * to_value(cell_cfgs[0].ran.ssb_cfg.ssb_period);

  gnbcu_config_update_request req;
  req.cells_to_deactivate.push_back(cell_cfgs[0].nr_cgi);

  async_task<gnbcu_config_update_response> resp_task =
      du_mng->get_f1ap_event_handler().handle_cu_context_update_request(req);
  lazy_task_launcher<gnbcu_config_update_response> launcher(resp_task);

  // The bar-first reconfigure is issued at the very start of the procedure, concurrently with the UE drain
  // (when_all), so the barred MIB reaches the air promptly. A couple of ticks suffices for it to reach the MAC.
  pump(2);
  ASSERT_TRUE(dependencies.mac.mac_cell.last_cell_recfg_req.has_value())
      << "Bar-first reconfigure was not issued at the start of the stop procedure";
  ASSERT_TRUE(dependencies.mac.mac_cell.last_cell_recfg_req->cell_barred_mod.value_or(false))
      << "Bar-first should set cell_barred_mod=true";
  ASSERT_FALSE(launcher.ready()) << "Stop completed before the settling window elapsed";

  // The cell must not be torn down before the settling window elapses, so the barred MIB is advertised on air
  // before SSB stops. With no UEs to drain, completion is gated by the settling window; just inside it the
  // procedure must still be pending.
  pump(settling_ms - 4);
  ASSERT_FALSE(launcher.ready()) << "Stop completed before the settling window elapsed";

  // Once the window (and the post-stop grant-flush wait) elapses, the procedure runs to completion.
  pump_until(500, [&]() { return launcher.ready(); });
  ASSERT_TRUE(launcher.ready()) << "Stop procedure did not complete after the settling window";
}

TEST_F(du_cell_lock_test, when_f1c_connection_is_lost_then_cell_stop_skips_barring)
{
  // The disconnection handler re-runs the F1 setup procedure after stopping cells. Empty the
  // pre-armed cells_to_activate so the re-setup does not restart the cell — a restart would
  // record the MIB cellBarred restore reconfigure, which is unrelated to the stop path under test.
  dependencies.f1ap.wait_f1_setup.result.value().cells_to_activate.clear();

  // F1-C connection loss stops cells with ue_removal_mode::no_f1_triggers: a fault path with no
  // graceful UE handling, and in particular no MIB mutation.
  du_mng->get_f1ap_event_handler().handle_f1c_connection_loss();
  pump(50);
  ASSERT_FALSE(dependencies.mac.mac_cell.last_cell_recfg_req.has_value()) << "Silent cell stop must not touch the MIB";

  // Prove the loss path actually stopped the cell: a follow-up graceful deactivate early-returns
  // promptly (no bar, no settling window) because the cell is already inactive. If the cell were
  // still active this request would take the graceful path and fail both assertions below.
  gnbcu_config_update_request req;
  req.cells_to_deactivate.push_back(cell_cfgs[0].nr_cgi);
  async_task<gnbcu_config_update_response> resp_task =
      du_mng->get_f1ap_event_handler().handle_cu_context_update_request(req);
  lazy_task_launcher<gnbcu_config_update_response> launcher(resp_task);
  pump_until(50, [&]() { return launcher.ready(); });
  ASSERT_TRUE(launcher.ready()) << "Deactivate of an already-stopped cell should return promptly";
  ASSERT_FALSE(dependencies.mac.mac_cell.last_cell_recfg_req.has_value())
      << "Deactivating an already-stopped cell must not bar it";
}

// Note: a "deactivate unknown CGI" path test belongs on the CU-CP side (cu_cp_cell_command_handler_test)
// rather than here. cu_configuration_procedure::stop_cell currently passes INVALID_DU_CELL_INDEX into
// du_cell_stop_procedure for unknown CGIs, which trips an assertion in du_cell_manager::is_cell_active.
// Our coverage of unknown CGIs lives in the CU-CP test where the validation happens before any F1AP
// message is emitted toward the DU.
