在当前项目继续一个有界开发周期。先读 AGENTS.md、PROJECT_STATE.json、HANDOFF.md、TASK_QUEUE.json、MODEL_ROUTING.json 和 BUDGET_POLICY.json。

先根据最新 PROJECT_STATE.json 的 next 和 TASK_QUEUE.json 选择最高优先级的未完成任务。用户已明确当前只验收 Windows，不排查 Linux 或将 Linux CI 作为门槛。当前优先打通 Windows CI，再实现最小可玩流程。日常实现优先通过 tools/claude-task.ps1 -Model sonnet -Provider auto 交给 Claude；必要时用 DeepSeek 文件工具。工作树不得同时有两个写入者。

完成本周期的必要编译、单测、Headless 测试与审查，保存状态和交接后退出。不得自动进入无限任务循环。中转预算按用户确认的每日100 CNY后台硬上限；预算或认证错误停止。不得读取、输出、提交 orchestration/local 中的凭据和日志；只有启动脚本可在本机读取凭据。
