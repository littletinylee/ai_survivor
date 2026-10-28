# 开发通道备用（Provider Fallback）

## 前提

- Subrouter 配置文件：`orchestration/local/subrouter.json`（已被 Git 忽略），格式为 `{"base_url": "https://…/v1", "api_key": "…"}`。脚本只在运行时读取，不打印、不记录、不放入命令参数。
- `base_url` 必须是 `https` 绝对地址且不含账户信息；`api_key` 不能为空。不满足时脚本直接报错，不发起请求。
- 所有环境变量和 `-c` 覆盖都只作用于当前进程，不写入全局 Claude 或 Codex 配置。

## 命令

Claude 任务（默认 `auto`：官方优先，仅在明确额度或限流信号时切换一次到 Subrouter）：

```powershell
pwsh -File tools/claude-task.ps1 -TaskFile <任务文件> -Model sonnet -Provider auto
```

- `-Provider official`：只用 Claude 官方认证，不切换。
- `-Provider subrouter`：直接使用 Subrouter 中转，模型映射为 haiku→claude-haiku-5-5、sonnet→claude-sonnet-5-5、opus→claude-opus-5-5。
- `-MaxBudgetUsd` 默认 0.25，仅传给 CLI 作为单次估算上限。

Codex 独立任务（默认模型 `gpt-6.1-sol`）：

```powershell
pwsh -File tools/codex-task.ps1 -TaskFile <任务文件> -Provider auto
pwsh -File tools/codex-subrouter.ps1 -TaskFile <任务文件>
```

## 切换规则

- Claude 官方结果须为可解析 JSON；Codex 官方结果须包含结构化失败事件。只有出现 429、rate_limit、quota exhausted、usage limit、hit limit 等明确额度或限流信号时切换。
- 401/403、认证失败、预算上限、权限拒绝、任务文件错误均不切换。
- 无 JSON 结果时直接报错，不盲目切换。
- 切换前写入检查点：`orchestration/local/checkpoints/provider-task-<时间>-<唯一ID>.json`，包含来源和目标通道、任务文件路径、当前分支名（只读取 `.git/HEAD`，不执行 git）、切换原因。Claude 还保存截断的官方结果摘要。不包含密钥。
- 每次切换最多尝试一次 Subrouter。失败后停止并报错，不循环重试。

## 边界（必须遵守）

- 调用方必须在桌面端额度耗尽之前，主动启动一个独立的 CLI 进程。切换只发生在该独立进程内。
- 不会自动迁移当前桌面聊天，也不会接管正在进行的会话。
- 用户于 2026-10-29 确认该 Key 在 Subrouter 后台设置每日 100 CNY 硬上限；此信息来源为用户确认，未由计费 API 独立读取。脚本不改变该限额。没有创建后台无限循环或系统定时任务，当前提供独立单任务启动入口。
- CLI 显示的费用是估算值，不是实际扣费；不承诺按 token 用量得到实际账单结果。
- 切换不会重置工作树；修改仍需遵守一个工作树同一时刻只有一个写入者的规则。
- 检查点只用于恢复上下文，不替代 Git 提交和测试。

## 已验证

- Subrouter `/v1/chat/completions` 与 `/v1/messages` 的 Haiku 5.5、`/v1/responses` 的 GPT‑6.1 Sol 都返回 READY。
- Claude Code 与 Codex CLI 通过项目备用脚本走中转，均实测成功。
- `pwsh -File tests/integration/provider-routing.ps1`：12项规则检查及模拟 CLI 交接，覆盖额度切换一次、认证错误停止、预算停止、检查点写入和环境恢复；不请求网络、不需要真实密钥。
- Codex 自定义 provider 使用进程级 `-c` 和环境变量配置，参见 [官方配置文档](https://learn.chatgpt.com/docs/config-file/config-advanced)。
