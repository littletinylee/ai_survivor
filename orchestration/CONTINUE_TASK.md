在当前项目继续一个有界开发周期。先读 AGENTS.md、PROJECT_STATE.json、HANDOFF.md、TASK_QUEUE.json、MODEL_ROUTING.json 和 BUDGET_POLICY.json。

当前优先任务是定位 Linux CI Godot 导入 exit134。必须先对照无扩展引擎基线并捕获崩溃证据；相同故障已连续三次尝试，不得重复猜测。日常实现优先通过 tools/claude-task.ps1 -Model sonnet -Provider auto 交给 Claude；必要时用 DeepSeek 文件工具。工作树不得同时有两个写入者。

完成本周期的必要编译、单测、Headless 测试与审查，保存状态和交接后退出。不得自动进入无限任务循环。中转预算按用户确认的每日100 CNY后台硬上限；预算或认证错误停止。不得读取、输出、提交 orchestration/local 中的凭据和日志；只有启动脚本可在本机读取凭据。
