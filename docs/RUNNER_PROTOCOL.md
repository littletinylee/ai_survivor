# 运行器协议：宿主机受控的 Claude 开发者交接

## 为什么需要

内层 Codex 工作区沙箱无法访问用户的 SCons/Godot AppData，也不能让子 Claude 发起网络请求。外层 Python 监督器在宿主机上运行 `build.ps1` / `test.ps1` 是可靠的，所以权威验证只放在宿主机。

本协议不改变沙箱或安全设置，内层沙箱也不得为此读取或放宽用户 AppData。Codex 只通过一个小请求文件表达"需要 Claude 写文件"，由监督器在宿主机执行固定启动器。同一时刻只有一个写入者。

## 周期顺序

1. **agent**：`codex-task.ps1` 运行 Codex。它可以写计划和 `job-request.json`，然后退出。
2. **developer**：仅当存在 `job-request.json` 时，运行 `claude-task.ps1`（固定启动器，单次、无重试）。
3. **build → test**：宿主机权威验证。
4. 可选 **simulations**：加 `--simulations` 后宿主依次运行真实 JSON 角色 seed1/2 kite 和 seed2 stationary，检查脚本退出码及唯一对应报告。
5. **validation**：写 `validation.json`，下一周期 Codex 审查已验证源码。

没有请求文件时保持旧行为：agent → build → test。

## 请求格式

路径：`orchestration/local/runner/job-request.json`（被 git 忽略）。

```json
{"id":"enemy-variety-2","task_file":"orchestration/P2-enemy-task.md","model":"haiku","max_budget_usd":0.2,"provider":"auto"}
```

- 只允许这五个键，缺少必需键或出现其他键（如命令、环境变量）都会拒绝。
- `id`：`^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$`。已记录过的 id 会被拒绝，避免重复计费或重复执行。
- `task_file`：相对路径，解析后（含符号链接）必须是 `orchestration/` 下存在的 `.md` 文件，不得位于 `orchestration/local/`，且不超过 64 KiB。
- `model`：`haiku` 或 `sonnet`。
- `max_budget_usd`：0.01 到 1 之间的有限实数（与 `claude-task.ps1` 的参数范围一致）。
- `provider`：`auto`（默认）、`official` 或 `subrouter`。

## 失败与记录

- `BUDGET_POLICY.unattended_enabled=false`：启动即停止，原因`unattended_disabled`，不运行控制器/开发者。账号验证阻塞时保持该开关关闭；这不代表每日金额已用完。非法预算文档仍由预算门禁拒绝。

- 请求非法或 id 重复：停止，原因为 `job_request_invalid: <reason>`，开发者不运行。
- 开发者非零退出、超时或 STOP：停止，不重试。
- 记录写在 `orchestration/local/runner/jobs/` 下：`<id>.request.json`（已验证的请求）和 `<id>.result.json`（状态）。
- 请求文件在 Codex 进程退出后才被消费（删除）。

## validation.json

在 build 被执行后写入，内容包括：

- `status`：`passed` 或 `failed`（任何 build/test 失败都只能是 `failed`）
- `cycle`、`timestamp_utc`、`provider`、`job_id`
- `source_fingerprint`：周期结束时的源码指纹
- `build_exit`、`test_exit`、`failure_reason`
- `unit_xml`：`build/test-results/unit.xml` 的 tests/failures/errors 汇总；文件缺失或无法读取时为 `null`，绝不当作 0 个测试。
- `simulation_reports`：本周期实际成功执行并解析的报告；缺失或种子/策略不匹配时周期失败。Lost 仅表示结构验证通过，不表示平衡达标。

构建失败且未执行 test 时 `unit_xml` 为 null，不能借用旧测试计数。

通过条件仍然是 `test.ps1` 退出码为 0。写入是原子的；在下一个代理阶段之前，旧记录保持可读。

查看当前记录是否仍与源码匹配：

```powershell
python tools/project_runner.py validation
```

输出为记录加上 `current_source_matches` 布尔值。它不打印日志或密钥。忽略的 `local/` 文件不计入源码进度。

总控默认使用 `python tools/project_runner.py validation --summary`，保留退出码、测试计数、源码匹配及各运行结果/哈希，省略逐分钟时间线。完整记录仍在宿主本地，按具体问题读取，避免把大段重复报告塞进模型上下文。

## 用法示例

1. Codex 在 agent 阶段写入 `orchestration/P2-enemy-task.md`（任务说明），然后写入 `orchestration/local/runner/job-request.json`，之后退出。
2. 外层运行 `python tools/project_runner.py run --max-cycles 2`：监督器读取请求、记录后运行 `claude-task.ps1 -TaskFile orchestration/P2-enemy-task.md -Model haiku -MaxBudgetUsd 0.2 -Provider auto`，再运行 build 和 test。
3. 下一周期 Codex 运行 `validation`，审查 `status` 为 `passed` 的源码后再决定下一步。
