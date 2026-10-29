#!/usr/bin/env python3
"""Bounded local supervisor: run a few Codex task cycles, validate, stop. Stdlib only."""
import argparse, contextlib, hashlib, json, os, shutil, signal, subprocess, sys, time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DONE = {"DONE", "COMPLETE"}
REQUIRED = ["orchestration/CONTINUE_TASK.md", "orchestration/BUDGET_POLICY.json",
            "tools/codex-task.ps1", "tools/build.ps1", "tools/test.ps1"]
POLL, HEARTBEAT = 1.0, 10.0


def now():
    return datetime.now(timezone.utc).isoformat()


def paths(root):
    d = Path(root) / "orchestration" / "local" / "runner"
    return {"dir": d, "state": d / "state.json", "stop": d / "STOP", "lock": d / "runner.lock",
            "log": d / "runner.log"}


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
                         ("build", ps + [str(root / "tools/build.ps1")]),
                         ("test", ps + [str(root / "tools/test.ps1")])]
                failed = None
                for name, cmd in steps:
                    left = cycle_deadline - time.monotonic()
                    if left <= 0:
                        failed = 'cycle_timeout'
                        break
                    save(phase=name, cycle=cycle)
                    rc, why = bounded(cmd, left)
                    if why != "ok" or rc != 0:
                        failed = f"{name}_{why}" if why != "ok" else f"{name}_exit_{rc}"
                        break
                if failed:
                    reason = failed
                    break
                save(phase="compare", cycle=cycle)
                if fingerprint(root, git) == before:
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
    sub.add_parser("status", help="print saved status JSON")
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
    p["dir"].mkdir(parents=True, exist_ok=True)
    p["stop"].write_text(now(), encoding="utf-8")
    print(f"STOP created: {p['stop']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
