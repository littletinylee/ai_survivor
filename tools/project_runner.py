#!/usr/bin/env python3
"""Bounded local supervisor: run a few Codex task cycles, validate, stop. Stdlib only."""
import argparse, contextlib, hashlib, json, os, re, shutil, signal, subprocess, sys, time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DONE = {"DONE", "COMPLETE"}
REQUIRED = ["orchestration/CONTINUE_TASK.md", "orchestration/BUDGET_POLICY.json",
            "tools/codex-task.ps1", "tools/build.ps1", "tools/test.ps1"]
POLL, HEARTBEAT = 1.0, 10.0
JOB_KEYS, JOB_REQUIRED = {"id", "task_file", "model", "max_budget_usd", "provider"}, {"id", "task_file", "model", "max_budget_usd"}
JOB_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}")
JOB_MODELS, JOB_PROVIDERS = ("haiku", "sonnet"), ("auto", "official", "subrouter")
TASK_MAX_BYTES = 64 * 1024


def now():
    return datetime.now(timezone.utc).isoformat()


def paths(root):
    d = Path(root) / "orchestration" / "local" / "runner"
    return {"dir": d, "state": d / "state.json", "stop": d / "STOP", "lock": d / "runner.lock",
            "log": d / "runner.log", "job_request": d / "job-request.json", "jobs": d / "jobs",
            "validation": d / "validation.json"}


def write_state(root, **kw):
    p = paths(root)
    p["dir"].mkdir(parents=True, exist_ok=True)
    data = {"pid": os.getpid(), "heartbeat_utc": now(), **kw}
    tmp = p["state"].with_suffix(".tmp")
    tmp.write_text(json.dumps(data, indent=2), encoding="utf-8")
    os.replace(tmp, p["state"])


@contextlib.contextmanager
def single_instance(lock_path):
    """OS advisory lock; released by the OS if the process dies. Yields False if held."""
    Path(lock_path).parent.mkdir(parents=True, exist_ok=True)
    f = open(lock_path, "a+b")
    got = False
    try:
        f.seek(0)
        if os.name == "nt":
            import msvcrt
            try:
                msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
                got = True
            except OSError:
                pass
        else:
            import fcntl
            try:
                fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                got = True
            except OSError:
                pass
        yield got
    finally:
        if got:
            f.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
        f.close()


def budget_ok(root):
    try:
        cap = json.loads((Path(root) / "orchestration/BUDGET_POLICY.json").read_text("utf-8"))["subrouter_server_limit"]
        v = cap["daily_cny"]
        return (cap["confirmed_by"] == "user" and isinstance(v, (int, float))
                and not isinstance(v, bool) and 0 < v <= 100)
    except (OSError, ValueError, KeyError, TypeError):
        return False


def queue_done(root):
    """True only when an existing queue lists tasks and all are DONE/COMPLETE."""
    q = Path(root) / "orchestration/TASK_QUEUE.json"
    if not q.exists():
        return False
    try:
        tasks = json.loads(q.read_text("utf-8"))["tasks"]
        return bool(tasks) and all(str(t.get("status", "")).upper() in DONE for t in tasks)
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return False


def project_blocked(root):
    try:
        data = json.loads((Path(root) / 'orchestration/PROJECT_STATE.json').read_text('utf-8'))
        return str(data.get('status', '')).upper() in {'BLOCKED', 'PAUSED', 'NEEDS_USER', 'WAITING_USER'}
    except (OSError, ValueError, TypeError):
        return False


def find_exe(name):
    p = shutil.which(name)
    return str(Path(p).resolve()) if p else None


def _git(git, root, *args):
    return subprocess.run([git, *args], cwd=str(root), capture_output=True, check=True,
                          creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)).stdout


def fingerprint(root, git):
    """Hash of HEAD + tracked diff + untracked (non-ignored) file contents."""
    h = hashlib.sha256()
    h.update(_git(git, root, "rev-parse", "HEAD"))
    h.update(_git(git, root, "diff", "HEAD"))
    names = _git(git, root, "ls-files", "--others", "--exclude-standard", "-z").split(b"\0")
    for n in sorted(x for x in names if x):
        h.update(n + b"\0")
        with contextlib.suppress(OSError):
            h.update((Path(root) / n.decode("utf-8", "surrogateescape")).read_bytes())
    return h.hexdigest()


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    os.replace(tmp, path)


def is_under(path, base):
    try:
        path.relative_to(base)
        return True
    except ValueError:
        return False


def parse_job(root, data):
    """Strictly validate a job request. Returns (normalized dict, resolved task path); raises ValueError(reason)."""
    if not isinstance(data, dict):
        raise ValueError("not_an_object")
    if set(data) - JOB_KEYS:
        raise ValueError("unknown_keys")
    if JOB_REQUIRED - set(data):
        raise ValueError("missing_keys")
    jid, task = data["id"], data["task_file"]
    model, provider, budget = data["model"], data.get("provider", "auto"), data["max_budget_usd"]
    if not isinstance(jid, str) or not JOB_ID.fullmatch(jid):
        raise ValueError("invalid_id")
    if model not in JOB_MODELS:
        raise ValueError("invalid_model")
    if provider not in JOB_PROVIDERS:
        raise ValueError("invalid_provider")
    if isinstance(budget, bool) or not isinstance(budget, (int, float)) or not 0.01 <= budget <= 1.0:  # NaN fails too
        raise ValueError("invalid_budget")
    if not isinstance(task, str) or not task or Path(task).is_absolute():
        raise ValueError("invalid_task_file")
    base = Path(root).resolve() / "orchestration"
    try:
        target = (Path(root) / task).resolve(strict=True)  # follows symlinks before the containment check
    except (OSError, RuntimeError, ValueError):
        raise ValueError("task_file_not_found")
    if target.suffix.lower() != ".md" or not target.is_file() or not is_under(target, base) \
            or is_under(target, base / "local"):
        raise ValueError("task_file_not_allowed")
    if target.stat().st_size > TASK_MAX_BYTES:
        raise ValueError("task_file_too_large")
    return {"id": jid, "task_file": task, "model": model, "max_budget_usd": budget, "provider": provider}, target


def take_job(root, ps, cycle):
    """Consume the job request written by this cycle's agent. Returns (job, cmd) or (None, None).

    Malformed or repeated requests raise ValueError before any developer run."""
    req, rec = paths(root)["job_request"], paths(root)["jobs"]
    if not req.exists():
        return None, None
    job, target = parse_job(root, json.loads(req.read_text("utf-8")))
    jid = job["id"]
    if (rec / f"{jid}.request.json").exists() or (rec / f"{jid}.result.json").exists():
        raise ValueError("duplicate_id")
    write_json(rec / f"{jid}.request.json", job)
    write_json(rec / f"{jid}.result.json", {"id": jid, "status": "started", "cycle": cycle, "started_utc": now()})
    req.unlink()  # consumed only now, after the agent process has exited
    cmd = ps + [str(root / "tools/claude-task.ps1"), "-TaskFile", str(target), "-Model", job["model"],
                "-MaxBudgetUsd", str(job["max_budget_usd"]), "-Provider", job["provider"]]
    return job, cmd


def finish_job(root, job, why, rc, cycle):
    status = why if why != "ok" else ("ok" if rc == 0 else "failed")
    write_json(paths(root)["jobs"] / f"{job['id']}.result.json",
               {"id": job["id"], "status": status, "exit": rc, "cycle": cycle, "finished_utc": now()})


def read_unit_xml(root):
    """GoogleTest aggregate counts; None when missing or unreadable (never reported as zero)."""
    x = Path(root) / "build/test-results/unit.xml"
    if not x.is_file():
        return None
    try:
        attrs = ET.parse(x).getroot().attrib
        counts = {k: int(attrs[k]) for k in ("tests", "failures", "errors") if k in attrs}
    except (ET.ParseError, OSError, ValueError):
        return None
    return counts or None


def read_simulation_report(root, seed, policy):
    path = Path(root) / f"build/test-results/simulation-{seed}-{policy}.out.log"
    prefix = "AI_SURVIVORS_SIMULATION_REPORT "
    lines = [line[len(prefix):] for line in path.read_text("utf-8").splitlines()
             if line.startswith(prefix)]
    if len(lines) != 1:
        raise ValueError("missing_or_duplicate_simulation_report")
    report = json.loads(lines[0])
    if not isinstance(report, dict) or report.get("requested_seed") != seed or report.get("policy") != policy:
        raise ValueError("simulation_report_request_mismatch")
    return report


def validation_report(root, git):
    """Saved validation record plus whether it still matches the current source fingerprint."""
    try:
        rec = json.loads(paths(root)["validation"].read_text("utf-8"))
    except FileNotFoundError:
        rec = None
    except (OSError, ValueError):
        return {"record": None, "error": "unreadable", "current_source_matches": False}
    matches = isinstance(rec, dict) and rec.get("source_fingerprint") == fingerprint(root, git)
    return {"record": rec, "current_source_matches": matches}


def compact_validation_report(report):
    """Controller context: retain evidence, omit potentially large timelines."""
    record = report.get("record")
    if not isinstance(record, dict):
        return report
    compact = {k: v for k, v in record.items() if k != "simulation_reports"}
    fields = ("requested_seed", "policy", "final_state", "survival_seconds", "trace_hash",
              "hp", "maxhp", "kills", "level", "hp_offers_seen", "hp_cards_selected",
              "hp_offers_declined_healthy")
    compact["simulation_reports"] = {
        name: {key: value[key] for key in fields if key in value}
        for name, value in record.get("simulation_reports", {}).items()
        if isinstance(value, dict)}
    return {**report, "record": compact}


def kill_tree(proc):
    """Kill only the process we started (and its descendants)."""
    if os.name == "nt":
        tk = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "taskkill.exe"
        subprocess.run([str(tk), "/PID", str(proc.pid), "/T", "/F"], capture_output=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    else:
        with contextlib.suppress(ProcessLookupError, PermissionError):
            os.killpg(proc.pid, signal.SIGKILL)
    with contextlib.suppress(Exception):
        proc.kill()
    with contextlib.suppress(Exception):
        proc.wait(timeout=10)


def run_bounded(cmd, cwd, timeout, stop_path, log_path, tick=None):
    """Run cmd (list, no shell). Returns (returncode|None, 'ok'|'timeout'|'stop')."""
    kw = ({"creationflags": subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP}
          if os.name == "nt" else {"start_new_session": True})
    Path(log_path).parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "ab") as log:
        proc = subprocess.Popen(cmd, cwd=str(cwd), stdin=subprocess.DEVNULL, stdout=log,
                                stderr=subprocess.STDOUT, **kw)
        end = time.monotonic() + timeout
        try:
            while proc.poll() is None:
                if Path(stop_path).exists():
                    kill_tree(proc)
                    return None, "stop"
                if time.monotonic() >= end:
                    kill_tree(proc)
                    return None, "timeout"
                if tick:
                    tick()
                time.sleep(min(POLL, max(0.05, end - time.monotonic())))
            return proc.returncode, "ok"
        except BaseException:
            kill_tree(proc)
            raise


def run(args, root=ROOT):
    root, p = Path(root), paths(root)
    st = {"phase": "starting", "cycle": 0, "reason": ""}
    with single_instance(p["lock"]) as got:
        if not got:
            print("another supervisor holds the lock", file=sys.stderr)
            return 3
        with contextlib.suppress(FileNotFoundError):
            p["stop"].unlink()  # only an explicit new run clears the sentinel
        last = [0.0]

        def save(**kw):
            st.update(kw)
            write_state(root, max_cycles=args.max_cycles, **st)
            last[0] = time.monotonic()

        def tick():
            if time.monotonic() - last[0] >= HEARTBEAT:
                save()

        def bounded(cmd, timeout):
            return run_bounded(cmd, root, timeout, p["stop"], p["log"], tick)

        try:
            save(provider=args.provider)
            missing = [r for r in REQUIRED if not (root / r).is_file()]
            if missing:
                save(phase="stopped", reason="missing_files: " + ", ".join(missing))
                return 2
            if not budget_ok(root):
                save(phase="stopped", reason="budget_gate_failed")
                return 2
            pwsh, git = find_exe("pwsh"), find_exe("git")
            if not pwsh or not git:
                save(phase="stopped", reason="pwsh_or_git_not_found")
                return 2
            deadline = time.monotonic() + args.total_hours * 3600
            ps = [pwsh, "-NoProfile", "-File"]
            reason = "max_cycles"
            for cycle in range(1, args.max_cycles + 1):
                if queue_done(root):
                    reason = "queue_completed"
                    break
                if project_blocked(root):
                    reason = 'project_blocked'
                    break
                if p["stop"].exists():
                    reason = "stop_requested"
                    break
                left = deadline - time.monotonic()
                if left <= 0:
                    reason = "total_hours_exceeded"
                    break
                save(phase="fingerprint", cycle=cycle)
                before = fingerprint(root, git)
                cycle_deadline = min(deadline, time.monotonic() + args.cycle_timeout)
                steps = [("agent", ps + [str(root / "tools/codex-task.ps1"), "-TaskFile",
                                         str(root / "orchestration/CONTINUE_TASK.md"), "-Provider", args.provider]),
                         ("developer", None),  # command comes from the agent's job request, if any
                         ("build", ps + [str(root / "tools/build.ps1")]),
                         ("test", ps + [str(root / "tools/test.ps1")])]
                simulations = {"simulation_1_kite": (1, "kite"),
                               "simulation_2_kite": (2, "kite"),
                               "simulation_2_stationary": (2, "stationary")}
                if getattr(args, "simulations", False):
                    steps.extend((name, ps + [str(root / "tools/simulate.ps1"), "-Seed", str(seed),
                                             "-Policy", policy])
                                 for name, (seed, policy) in simulations.items())
                failed, exits, job_id, reports = None, {}, None, {}
                for name, cmd in steps:
                    left = cycle_deadline - time.monotonic()
                    if left <= 0:
                        failed = 'cycle_timeout'
                        break
                    job = None
                    if name == "developer":
                        try:
                            job, cmd = take_job(root, ps, cycle)
                        except (OSError, ValueError) as e:
                            failed = f"job_request_invalid: {e}"
                            break
                        if job is None:
                            continue  # no request: plain agent -> build -> test
                        job_id = job["id"]
                    save(phase=name, cycle=cycle)
                    rc, why = bounded(cmd, left)
                    if job is not None:
                        finish_job(root, job, why, rc, cycle)
                    exits[name] = rc if why == "ok" else None
                    if why != "ok" or rc != 0:
                        failed = f"{name}_{why}" if why != "ok" else f"{name}_exit_{rc}"
                        break
                    if name in simulations:
                        try:
                            reports[name] = read_simulation_report(root, *simulations[name])
                        except (OSError, ValueError) as e:
                            failed = f"{name}_invalid_report: {type(e).__name__}"
                            break
                fp = None
                if "build" in exits:  # build was reached, so this cycle leaves a validation record
                    save(phase="validate", cycle=cycle)
                    fp = fingerprint(root, git)
                    write_json(p["validation"], {
                        "status": "passed" if failed is None else "failed", "cycle": cycle,
                        "timestamp_utc": now(), "provider": args.provider, "source_fingerprint": fp,
                        "build_exit": exits.get("build"), "test_exit": exits.get("test"),
                        "failure_reason": failed, "job_id": job_id,
                        "unit_xml": read_unit_xml(root) if exits.get("test") == 0 else None,
                        "simulation_reports": reports})
                if failed:
                    reason = failed
                    break
                save(phase="compare", cycle=cycle)
                if fp == before:
                    reason = "no_progress"
                    break
            else:
                if queue_done(root):
                    reason = "queue_completed"
            save(phase="exited", reason=reason)
            return 0 if reason in ("max_cycles", "queue_completed") else 1
        except BaseException as e:
            save(phase="crashed", reason=f"{type(e).__name__}: {e}")
            raise


def bounded_num(kind, lo, hi):
    def conv(s):
        v = kind(s)
        valid = lo < v <= hi if kind is float else lo <= v <= hi
        if not valid:
            raise argparse.ArgumentTypeError(f"must be in range ({lo}, {hi}]")
        return v
    return conv


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="run bounded supervised cycles")
    r.add_argument("--max-cycles", type=bounded_num(int, 1, 24), default=6)
    r.add_argument("--cycle-timeout", type=bounded_num(int, 1, 3600), default=900)
    r.add_argument("--total-hours", type=bounded_num(float, 0, 24), default=4.0)
    r.add_argument("--provider", choices=("auto", "official", "subrouter"), default="auto",
                   help="controller provider; subrouter skips official quota entirely")
    r.add_argument("--simulations", action="store_true",
                   help="validate real JSON character seeds1/2 kite and seed2 stationary on the host")
    sub.add_parser("status", help="print saved status JSON")
    v = sub.add_parser("validation", help="print saved validation record and whether it matches current source")
    v.add_argument("--summary", action="store_true", help="omit detailed timelines for controller context")
    sub.add_parser("stop", help="create STOP sentinel (kills nothing)")
    a = ap.parse_args(argv)
    p = paths(ROOT)
    if a.cmd == "run":
        return run(a)
    if a.cmd == "status":
        if not p["state"].exists():
            print("no runner state")
            return 1
        print(p["state"].read_text("utf-8"))
        return 0
    if a.cmd == "validation":
        git = find_exe("git")
        if not git:
            print("git_not_found", file=sys.stderr)
            return 2
        report = validation_report(ROOT, git)
        print(json.dumps(compact_validation_report(report) if a.summary else report,
                         indent=2, ensure_ascii=False))
        return 0
    p["dir"].mkdir(parents=True, exist_ok=True)
    p["stop"].write_text(now(), encoding="utf-8")
    print(f"STOP created: {p['stop']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
