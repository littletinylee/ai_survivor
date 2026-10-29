import argparse, importlib.util, json, sys, tempfile, time, unittest
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[2] / "tools" / "project_runner.py"
spec = importlib.util.spec_from_file_location("project_runner", SCRIPT)
pr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pr)

GOOD_BUDGET = {"subrouter_server_limit": {"confirmed_by": "user", "daily_cny": 100}}


def make_root(tmp, budget=GOOD_BUDGET, queue=None):
    root = Path(tmp)
    for rel in pr.REQUIRED:
        f = root / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text("x", encoding="utf-8")
    (root / "orchestration/BUDGET_POLICY.json").write_text(json.dumps(budget), encoding="utf-8")
    if queue is not None:
        (root / "orchestration/TASK_QUEUE.json").write_text(json.dumps(queue), encoding="utf-8")
    return root


def args(**kw):
    return argparse.Namespace(**{"max_cycles": 3, "cycle_timeout": 5, "total_hours": 1.0, **kw})


def state(root):
    return json.loads(pr.paths(root)["state"].read_text("utf-8"))


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.patches = [mock.patch.object(pr, "find_exe", lambda n: "/abs/" + n)]
        for p in self.patches:
            p.start()
            self.addCleanup(p.stop)

    def test_budget_gate(self):
        for b in ({}, {"subrouter_server_limit": {"confirmed_by": "user"}},
                  {"subrouter_server_limit": {"confirmed_by": "user", "daily_cny": 101}},
                  {"subrouter_server_limit": {"confirmed_by": "user", "daily_cny": 0}},
                  {"subrouter_server_limit": {"confirmed_by": "codex", "daily_cny": 50}}):
            root = make_root(self.tmp.name, budget=b)
            self.assertFalse(pr.budget_ok(root), b)
            with mock.patch.object(pr, "run_bounded") as rb:
                self.assertEqual(pr.run(args(), root), 2)
                rb.assert_not_called()
            self.assertEqual(state(root)["reason"], "budget_gate_failed")
        self.assertTrue(pr.budget_ok(make_root(self.tmp.name)))

    def test_done_queue_starts_nothing(self):
        root = make_root(self.tmp.name, queue={"tasks": [{"status": "DONE"}, {"status": "COMPLETE"}]})
        with mock.patch.object(pr, "run_bounded") as rb, mock.patch.object(pr, "fingerprint", return_value="a"):
            self.assertEqual(pr.run(args(), root), 0)
            rb.assert_not_called()
        self.assertEqual(state(root)["reason"], "queue_completed")

    def test_missing_files_stop(self):
        root = make_root(self.tmp.name)
        (root / "tools/build.ps1").unlink()
        self.assertEqual(pr.run(args(), root), 2)
        self.assertIn("missing_files", state(root)["reason"])

    def test_project_blocked_starts_nothing(self):
        root = make_root(self.tmp.name)
        (root / 'orchestration/PROJECT_STATE.json').write_text('{"status":"BLOCKED"}')
        with mock.patch.object(pr, 'run_bounded') as rb:
            self.assertEqual(pr.run(args(), root), 1)
            rb.assert_not_called()
        self.assertEqual(state(root)['reason'], 'project_blocked')

    def test_cycle_deadline_applies_across_steps(self):
        root = make_root(self.tmp.name)
        clock = [0.0]
        def advance(*a, **kw):
            clock[0] += 6
            return 0, 'ok'
        with mock.patch.object(pr.time, 'monotonic', side_effect=lambda: clock[0]), \
             mock.patch.object(pr, 'run_bounded', side_effect=advance) as rb, \
             mock.patch.object(pr, 'fingerprint', return_value='a'):
            self.assertEqual(pr.run(args(cycle_timeout=5), root), 1)
        self.assertEqual(rb.call_count, 1)
        self.assertEqual(state(root)['reason'], 'cycle_timeout')

    def test_duplicate_lock(self):
        root = make_root(self.tmp.name)
        with pr.single_instance(pr.paths(root)["lock"]) as first:
            self.assertTrue(first)
            with pr.single_instance(pr.paths(root)["lock"]) as second:
                self.assertFalse(second)
            with mock.patch.object(pr, "run_bounded") as rb:
                self.assertEqual(pr.run(args(), root), 3)
                rb.assert_not_called()
        with pr.single_instance(pr.paths(root)["lock"]) as again:
            self.assertTrue(again)  # released on exit

    def test_timeout_and_stop_kill_owned_process(self):
        root = make_root(self.tmp.name)
        p = pr.paths(root)
        sleeper = [sys.executable, "-c", "import time; time.sleep(60)"]
        t0 = time.monotonic()
        rc, why = pr.run_bounded(sleeper, root, 1, p["stop"], p["log"])
        self.assertEqual((rc, why), (None, "timeout"))
        p["stop"].write_text("x")
        rc, why = pr.run_bounded(sleeper, root, 30, p["stop"], p["log"])
        self.assertEqual((rc, why), (None, "stop"))
        self.assertLess(time.monotonic() - t0, 20)

    def test_cycle_loop_bounded_and_clears_stop(self):
        root = make_root(self.tmp.name)
        p = pr.paths(root)
        p["dir"].mkdir(parents=True)
        p["stop"].write_text("old")
        prints = iter(range(100))
        with mock.patch.object(pr, "run_bounded", return_value=(0, "ok")) as rb, \
                mock.patch.object(pr, "fingerprint", side_effect=lambda *a: str(next(prints))):
            self.assertEqual(pr.run(args(max_cycles=2), root), 0)
        self.assertEqual(rb.call_count, 6)  # 2 cycles x (agent, build, test)
        s = state(root)
        self.assertEqual((s["phase"], s["reason"]), ("exited", "max_cycles"))
        self.assertIn("pid", s)
        self.assertIn("heartbeat_utc", s)
        self.assertFalse(p["stop"].exists())

    def test_no_progress_stops(self):
        root = make_root(self.tmp.name)
        with mock.patch.object(pr, "run_bounded", return_value=(0, "ok")) as rb, \
                mock.patch.object(pr, "fingerprint", return_value="same"):
            self.assertEqual(pr.run(args(max_cycles=5), root), 1)
        self.assertEqual(rb.call_count, 3)
        self.assertEqual(state(root)["reason"], "no_progress")

    def test_failed_validation_stops_before_next_model(self):
        root = make_root(self.tmp.name)
        results = iter([(0, "ok"), (0, "ok"), (1, "ok")])  # agent, build ok; test fails
        with mock.patch.object(pr, "run_bounded", side_effect=lambda *a, **k: next(results)) as rb, \
                mock.patch.object(pr, "fingerprint", side_effect=["a", "b", "c", "d"]):
            self.assertEqual(pr.run(args(max_cycles=5), root), 1)
        self.assertEqual(rb.call_count, 3)
        self.assertEqual(state(root)["reason"], "test_exit_1")

    def test_agent_command_shape(self):
        root = make_root(self.tmp.name)
        with mock.patch.object(pr, "run_bounded", return_value=(1, "ok")) as rb, \
                mock.patch.object(pr, "fingerprint", return_value="a"):
            pr.run(args(max_cycles=1), root)
        cmd = rb.call_args_list[0].args[0]
        self.assertEqual(cmd[:3], ["/abs/pwsh", "-NoProfile", "-File"])
        self.assertIn("-Provider", cmd)
        self.assertEqual(cmd[-1], "auto")

    def test_arg_bounds(self):
        for bad in (["run", "--max-cycles", "0"], ["run", "--max-cycles", "999"],
                    ["run", "--cycle-timeout", "-1"], ["run", "--total-hours", "0"]):
            with self.assertRaises(SystemExit), mock.patch("sys.stderr"):
                pr.main(bad)


if __name__ == "__main__":
    unittest.main()
