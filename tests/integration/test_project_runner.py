import argparse, importlib.util, io, json, os, sys, tempfile, time, unittest
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
    return argparse.Namespace(**{"max_cycles": 3, "cycle_timeout": 5, "total_hours": 1.0,
                                "provider": "auto", **kw})


def state(root):
    return json.loads(pr.paths(root)["state"].read_text("utf-8"))


def job_root(tmp):
    root = make_root(tempfile.mkdtemp(dir=tmp))
    for rel, body in (("orchestration/job-task.md", "task"), ("orchestration/local/secret.md", "x"),
                      ("orchestration/big.md", "x" * (64 * 1024 + 1)), ("tools/x.md", "x")):
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(body, encoding="utf-8")
    return root


def job_request(**kw):
    return {"id": "job-1", "task_file": "orchestration/job-task.md", "model": "haiku",
            "max_budget_usd": 0.2, "provider": "auto", **kw}


def fake_run(root, request=None, outcome=None):
    """Stand-in for run_bounded. The agent step writes `request` (dict or raw text) and exits 0.
    `outcome` maps a step name to the (rc, why) it returns; other steps succeed."""
    outcome, calls = outcome or {}, []

    def fake(cmd, *a, **kw):
        name = next(n for n in ("codex-task", "claude-task", "build.ps1", "test.ps1")
                    if any(n in part for part in cmd))
        calls.append(name)
        if name == "codex-task" and request is not None:
            text = request if isinstance(request, str) else json.dumps(request)
            pr.paths(root)["dir"].mkdir(parents=True, exist_ok=True)
            pr.paths(root)["job_request"].write_text(text, encoding="utf-8")
        return outcome.get(name, (0, "ok"))
    return fake, calls


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.patches = [mock.patch.object(pr, "find_exe", lambda n: "/abs/" + n)]
        for p in self.patches:
            p.start()
            self.addCleanup(p.stop)

    def test_budget_gate(self):
        for b in ({}, [], None, "invalid", {"subrouter_server_limit": {"confirmed_by": "user"}},
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

    def test_disabled_unattended_policy_starts_nothing(self):
        root = make_root(self.tmp.name, {**GOOD_BUDGET, "unattended_enabled": False})
        with mock.patch.object(pr, "run_bounded") as rb:
            self.assertEqual(pr.run(args(), root), 2)
        rb.assert_not_called()
        self.assertEqual(state(root)["reason"], "unattended_disabled")

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
        for provider in ("auto", "official", "subrouter"):
            root = make_root(self.tmp.name)
            with mock.patch.object(pr, "run_bounded", return_value=(1, "ok")) as rb, \
                    mock.patch.object(pr, "fingerprint", return_value="a"):
                pr.run(args(max_cycles=1, provider=provider), root)
            cmd = rb.call_args_list[0].args[0]
            self.assertEqual(cmd[:3], ["/abs/pwsh", "-NoProfile", "-File"])
            self.assertIn("-Provider", cmd)
            self.assertEqual(cmd[-1], provider)
            self.assertEqual(state(root)["provider"], provider)

    def test_provider_cli_selection(self):
        with mock.patch.object(pr, "run", return_value=0) as run:
            self.assertEqual(pr.main(["run", "--provider", "subrouter"]), 0)
            self.assertEqual(run.call_args.args[0].provider, "subrouter")

    def test_arg_bounds(self):
        for bad in (["run", "--max-cycles", "0"], ["run", "--max-cycles", "999"],
                    ["run", "--cycle-timeout", "-1"], ["run", "--total-hours", "0"],
                    ["run", "--provider", "unknown"]):
            with self.assertRaises(SystemExit), mock.patch("sys.stderr"):
                pr.main(bad)

    def run_with_request(self, root, request, outcome=None):
        fake, calls = fake_run(root, request, outcome)
        with mock.patch.object(pr, "run_bounded", side_effect=fake), \
                mock.patch.object(pr, "fingerprint", return_value="a"):
            return pr.run(args(), root), calls

    def test_job_runs_after_agent_before_build_and_test(self):
        root = job_root(self.tmp.name)
        fake, calls = fake_run(root, job_request(model="sonnet", max_budget_usd=0.5, provider="subrouter"))
        prints = iter(range(100))
        with mock.patch.object(pr, "run_bounded", side_effect=fake) as rb, \
                mock.patch.object(pr, "fingerprint", side_effect=lambda *a: str(next(prints))):
            self.assertEqual(pr.run(args(max_cycles=1), root), 0)
        self.assertEqual(calls, ["codex-task", "claude-task", "build.ps1", "test.ps1"])
        cmd = rb.call_args_list[1].args[0]
        self.assertEqual(cmd[:3], ["/abs/pwsh", "-NoProfile", "-File"])
        self.assertEqual(cmd[3:], [str(root / "tools/claude-task.ps1"), "-TaskFile",
                                   str((root / "orchestration/job-task.md").resolve()),
                                   "-Model", "sonnet", "-MaxBudgetUsd", "0.5", "-Provider", "subrouter"])
        jobs = pr.paths(root)["jobs"]
        self.assertEqual(json.loads((jobs / "job-1.request.json").read_text("utf-8"))["model"], "sonnet")
        self.assertEqual(json.loads((jobs / "job-1.result.json").read_text("utf-8"))["status"], "ok")
        self.assertFalse(pr.paths(root)["job_request"].exists())  # consumed after the agent exited
        record = json.loads(pr.paths(root)["validation"].read_text("utf-8"))
        self.assertEqual((record["status"], record["job_id"]), ("passed", "job-1"))

    def test_duplicate_job_id_never_reaches_developer(self):
        root = job_root(self.tmp.name)
        pr.paths(root)["jobs"].mkdir(parents=True)
        (pr.paths(root)["jobs"] / "job-1.result.json").write_text("{}", encoding="utf-8")
        rc, calls = self.run_with_request(root, job_request())
        self.assertEqual((rc, calls), (1, ["codex-task"]))
        self.assertEqual(state(root)["reason"], "job_request_invalid: duplicate_id")

    def test_invalid_job_requests_rejected_before_developer(self):
        cases = [
            {"task_file": "../outside.md"}, {"task_file": "tools/x.md"},
            {"task_file": "orchestration/local/secret.md"}, {"task_file": "orchestration/big.md"},
            {"task_file": "tools/build.ps1"}, {"task_file": "orchestration/CONTINUE_TASK.md.bak"},
            {"model": "opus"}, {"max_budget_usd": 0}, {"max_budget_usd": 1.5}, {"max_budget_usd": True},
            {"max_budget_usd": "0.2"}, {"max_budget_usd": float("nan")}, {"max_budget_usd": 0.005},
            {"id": "../x"}, {"id": "a b"}, {"id": "x" * 65}, {"id": "-lead"}, {"id": None},
            {"provider": "fast"}, {"command": "rm -rf x"}, {"env": {"X": "1"}},
        ]
        for bad in cases:
            root = job_root(self.tmp.name)
            rc, calls = self.run_with_request(root, job_request(**bad))
            self.assertEqual((rc, calls), (1, ["codex-task"]), bad)
            self.assertTrue(state(root)["reason"].startswith("job_request_invalid"), bad)
            self.assertFalse(pr.paths(root)["jobs"].exists(), bad)
        absolute = job_root(self.tmp.name)
        rc, calls = self.run_with_request(absolute, job_request(task_file=str(absolute / "orchestration/job-task.md")))
        self.assertEqual((rc, calls), (1, ["codex-task"]))
        for raw in ("{not json", "[1, 2]", json.dumps({k: v for k, v in job_request().items() if k != "model"})):
            root = job_root(self.tmp.name)
            rc, calls = self.run_with_request(root, raw)
            self.assertEqual((rc, calls), (1, ["codex-task"]), raw)
            self.assertFalse(pr.paths(root)["jobs"].exists())

    def test_symlink_escape_rejected(self):
        root = job_root(self.tmp.name)
        try:
            os.symlink(root / "orchestration/local/secret.md", root / "orchestration/link.md")
        except OSError:
            self.skipTest("symlink creation needs privilege on this Windows account")
        rc, calls = self.run_with_request(root, job_request(task_file="orchestration/link.md"))
        self.assertEqual((rc, calls), (1, ["codex-task"]))
        self.assertTrue(state(root)["reason"].startswith("job_request_invalid"))

    def test_developer_failure_stops_before_build(self):
        for outcome, reason, status in (((1, "ok"), "developer_exit_1", "failed"),
                                        ((None, "timeout"), "developer_timeout", "timeout")):
            root = job_root(self.tmp.name)
            rc, calls = self.run_with_request(root, job_request(), {"claude-task": outcome})
            self.assertEqual((rc, calls), (1, ["codex-task", "claude-task"]))
            self.assertEqual(state(root)["reason"], reason)
            result = json.loads((pr.paths(root)["jobs"] / "job-1.result.json").read_text("utf-8"))
            self.assertEqual(result["status"], status)
            self.assertFalse(pr.paths(root)["validation"].exists())

    def test_validation_record_and_stale_detection(self):
        root = job_root(self.tmp.name)  # no job request: plain agent -> build -> test
        xml = root / "build/test-results/unit.xml"
        xml.parent.mkdir(parents=True)
        xml.write_text('<testsuites tests="117" failures="0" errors="0"></testsuites>', encoding="utf-8")
        with mock.patch.object(pr, "run_bounded", return_value=(0, "ok")), \
                mock.patch.object(pr, "fingerprint", side_effect=["a", "b", "c"]):
            self.assertEqual(pr.run(args(max_cycles=1), root), 0)
        record = json.loads(pr.paths(root)["validation"].read_text("utf-8"))
        self.assertEqual((record["status"], record["source_fingerprint"], record["build_exit"], record["test_exit"]),
                         ("passed", "b", 0, 0))
        self.assertEqual(record["unit_xml"], {"tests": 117, "failures": 0, "errors": 0})
        fake, _ = fake_run(root, None, {"codex-task": (1, "ok")})  # next agent phase fails; record must stay
        with mock.patch.object(pr, "run_bounded", side_effect=fake), \
                mock.patch.object(pr, "fingerprint", return_value="c"):
            self.assertEqual(pr.run(args(max_cycles=1), root), 1)
        self.assertEqual(json.loads(pr.paths(root)["validation"].read_text("utf-8"))["status"], "passed")
        with mock.patch.object(pr, "fingerprint", return_value="b"):
            self.assertTrue(pr.validation_report(root, "git")["current_source_matches"])
        with mock.patch.object(pr, "fingerprint", return_value="d"):
            self.assertFalse(pr.validation_report(root, "git")["current_source_matches"])
        out = io.StringIO()
        with mock.patch.object(pr, "ROOT", root), mock.patch.object(pr, "fingerprint", return_value="d"), \
                mock.patch("sys.stdout", out):
            self.assertEqual(pr.main(["validation"]), 0)
        self.assertFalse(json.loads(out.getvalue())["current_source_matches"])

    def test_controller_summary_preserves_evidence_without_large_timeline(self):
        report = {"current_source_matches": True, "record": {
            "status": "passed", "unit_xml": {"tests": 168, "failures": 0},
            "simulation_reports": {"simulation_1_kite": {
                "requested_seed": 1, "policy": "kite", "final_state": "Lost",
                "survival_seconds": 613.0, "trace_hash": "c3dcdf2b24529355",
                "timeline": [{"time": i} for i in range(22)]}}}}
        compact = pr.compact_validation_report(report)
        self.assertTrue(compact["current_source_matches"])
        self.assertEqual(compact["record"]["unit_xml"]["tests"], 168)
        sample = compact["record"]["simulation_reports"]["simulation_1_kite"]
        self.assertEqual((sample["final_state"], sample["survival_seconds"]), ("Lost", 613.0))
        self.assertNotIn("timeline", sample)
        self.assertEqual(len(report["record"]["simulation_reports"]["simulation_1_kite"]["timeline"]), 22)

    def test_controller_summary_keeps_missing_validation_unknown(self):
        missing = {"record": None, "current_source_matches": False, "error": "unreadable"}
        self.assertEqual(pr.compact_validation_report(missing), missing)

    def test_failed_build_and_test_record_failed(self):
        root = job_root(self.tmp.name)
        xml = root / "build/test-results/unit.xml"
        xml.parent.mkdir(parents=True)
        xml.write_text('<testsuites tests="999" failures="0"/>', encoding="utf-8")
        fake, calls = fake_run(root, None, {"build.ps1": (2, "ok")})
        with mock.patch.object(pr, "run_bounded", side_effect=fake), \
                mock.patch.object(pr, "fingerprint", side_effect=["a", "b"]):
            self.assertEqual(pr.run(args(max_cycles=1), root), 1)
        record = json.loads(pr.paths(root)["validation"].read_text("utf-8"))
        self.assertEqual((record["status"], record["failure_reason"], record["build_exit"], record["test_exit"]),
                         ("failed", "build_exit_2", 2, None))
        self.assertEqual(calls, ["codex-task", "build.ps1"])
        self.assertIsNone(record["unit_xml"])  # old binary's XML cannot validate failed source build
        root = job_root(self.tmp.name)
        fake, _ = fake_run(root, None, {"test.ps1": (1, "ok")})
        with mock.patch.object(pr, "run_bounded", side_effect=fake), \
                mock.patch.object(pr, "fingerprint", side_effect=["a", "b"]):
            self.assertEqual(pr.run(args(max_cycles=1), root), 1)
        record = json.loads(pr.paths(root)["validation"].read_text("utf-8"))
        self.assertEqual((record["status"], record["failure_reason"], record["test_exit"]), ("failed", "test_exit_1", 1))

    def test_host_simulations_follow_tests_and_record_reports(self):
        root = job_root(self.tmp.name)
        calls = []

        def execute(cmd, *a, **kw):
            calls.append(cmd)
            if str(root / "tools/simulate.ps1") in cmd:
                seed, policy = int(cmd[cmd.index("-Seed") + 1]), cmd[cmd.index("-Policy") + 1]
                out = root / f"build/test-results/simulation-{seed}-{policy}.out.log"
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_text("AI_SURVIVORS_SIMULATION_REPORT " + json.dumps(
                    {"requested_seed": seed, "policy": policy, "final_state": "Lost"}), encoding="utf-8")
            return 0, "ok"

        with mock.patch.object(pr, "run_bounded", side_effect=execute), \
                mock.patch.object(pr, "fingerprint", side_effect=["a", "b"]):
            self.assertEqual(pr.run(args(max_cycles=1, simulations=True), root), 0)
        self.assertEqual(len(calls), 6)  # controller, build, test, then three real-character reports
        self.assertTrue(calls[2][3].endswith("test.ps1"))
        record = json.loads(pr.paths(root)["validation"].read_text("utf-8"))
        self.assertEqual(record["status"], "passed")
        reports = record["simulation_reports"]
        self.assertEqual([(r["requested_seed"], r["policy"]) for r in reports.values()],
                         [(1, "kite"), (2, "kite"), (2, "stationary")])

    def test_missing_or_mismatched_simulation_report_stops_cycle(self):
        for body in (None, 'AI_SURVIVORS_SIMULATION_REPORT {"requested_seed":2,"policy":"kite"}'):
            root = job_root(self.tmp.name)
            if body:
                out = root / "build/test-results/simulation-1-kite.out.log"
                out.parent.mkdir(parents=True)
                out.write_text(body, encoding="utf-8")
            with mock.patch.object(pr, "run_bounded", return_value=(0, "ok")) as rb, \
                    mock.patch.object(pr, "fingerprint", side_effect=["a", "b"]):
                self.assertEqual(pr.run(args(max_cycles=1, simulations=True), root), 1)
            self.assertEqual(rb.call_count, 4)
            record = json.loads(pr.paths(root)["validation"].read_text("utf-8"))
            self.assertEqual(record["status"], "failed")
            self.assertTrue(record["failure_reason"].startswith("simulation_1_kite_invalid_report"))


if __name__ == "__main__":
    unittest.main()
