# 持续推进：本地 project_runner

`tools/project_runner.py`（Python 3.11 标准库）是一个有界的本地监督器，独立于桌面聊天窗口，按周期推进 `orchestration/CONTINUE_TASK.md`。

## 命令

```powershell
python tools/project_runner.py run [--max-cycles 6] [--cycle-timeout 900] [--total-hours 4]
python tools/project_runner.py status   # 打印已保存的状态 JSON
python tools/project_runner.py stop     # 创建 STOP 哨兵，不杀任何进程
```

参数均为正数且有上限：`max-cycles` 1–24，`cycle-timeout` 1–3600 秒，`total-hours` (0, 24]。首次实际后台启动建议 `--max-cycles 1`，由 Codex 在测试通过后执行。

## 每个周期

1. 预算闸门：`BUDGET_POLICY.json` 的 `subrouter_server_limit.confirmed_by` 必须为 `user`，且 `daily_cny` 为 (0, 100] 的正数。该上限由用户确认，runner 没有也不会查询 API 或读取密钥。
2. 所需文件缺失、`TASK_QUEUE.json` 全部为 DONE/COMPLETE、预算不满足时直接停止。
3. 记录指纹（git HEAD + `git diff HEAD` + 未忽略的未跟踪文件内容哈希）。
4. 依次运行，所有步骤共同受本周期剩余时限和总时限约束：
   `pwsh -NoProfile -File tools/codex-task.ps1 -TaskFile orchestration/CONTINUE_TASK.md -Provider auto`，退出码 0 后运行 `tools/build.ps1`，再运行 `tools/test.ps1`。
5. 任一步失败/超时/被 STOP，立即停止，不重试。
6. 前后指纹相同视为无实质进展，停止，不循环同一任务；项目状态为 BLOCKED/PAUSED/NEEDS_USER/WAITING_USER 时不启动新周期。
7. 达到 `max-cycles` 或 `total-hours` 后退出。

不使用 `shell=True`，所有可执行文件用绝对路径；不自动合并、不推送 main、不创建计划任务、不开机自启。

## 状态与停止

状态、日志、STOP、锁都位于被忽略的 `orchestration/local/runner/`（`state.json` 含 PID、phase、cycle、reason、heartbeat_utc；运行中每 10 秒刷新，退出时写检查点）。日志可能含模型输出，不要提交或外传。

- 同一时刻只允许一个 supervisor：使用 OS 咨询锁（Windows `msvcrt`，POSIX `fcntl`），进程崩溃后由系统释放。
- STOP 哨兵只会在新的 `run` 获得锁后被清除。
- 超时或 STOP 时只结束 runner 自己启动的进程树（Windows `taskkill /PID <pid> /T /F`，POSIX 自己的进程组）。

## 与其他机制的差别与限制

- 不同于应用内 `/goal`：runner 不依赖聊天会话存活，但也没有对话上下文，只靠任务文件、状态文件和交接记录。
- 电脑睡眠、关机、注销会使其中断；它不会自启，重启后需手动再次 `run`。睡眠期间墙钟超时可能触发。
- 预算只是用户确认的服务端上限，runner 不做花费记账。
- 工作树同一时刻只能一个写入者：runner 运行期间不要手动修改工作树。
