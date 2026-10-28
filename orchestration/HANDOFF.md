# 恢复入口

正式工作目录 E:/AI Survior/ai_survivor，用户克隆。父目录保留早期断点。分支 feat/phase0-bootstrap 已推送；读取 docs/VALIDATION.md、PROJECT_STATE.json 和 TASK_QUEUE.json。

当前 Codex gpt-6.1-sol 完成原生场景、SCons、Windows 构建/测试脚本、工具链锁及 Linux CI；修复状态命令来源错误和 Godot 导入编译对象问题。21 项单测、Windows Headless 和截图已验证。Linux 构建通过，集成失败；提交 7ae82cd 为 CI 增加错误注释便于通过公开 API读取。main 尚未合并，PR 尚未创建。

Claude 先前 403，禁止循环重试；DeepSeek deepseek-flash 探针成功但沙箱编译受阻。仅在能力/预算就绪后再路由，不需要重复空探针。本轮没有额外调用付费外部模型，Codex 费用无可获取账本。

下一步定位 CI、完成 PR 验收，然后补预算和有界执行器。不要将计时模拟描述为完整战斗模拟。100 CNY/日，北京时间自然日；无可核验成本时不启动无人值守。

优先级更新：用户要求节省 Codex 额度、优先其他模型。Claude 官方 OAuth 已由用户登录；隔离全局 OpenRouter 设置后 Haiku5.5 和 Sonnet5.5 实测 READY，Haiku 写入一项真实状态命令回归测试，22 单测及 Windows Headless PASS。启动 tools/claude-task.ps1，默认 Haiku；常规实现 -Model sonnet。DeepSeek 文件工具任务同样已验证。后续代码优先 Claude，本地脚本执行构建测试，Codex仅做统筹验收。Linux CI import仍exit134（三次后必须重新分析，不能重复猜测修复），下一任务交 Sonnet，先增加引擎无扩展基线对照和可靠崩溃日志。

Subrouter备用已接通（用户确认Key后台每日100CNY硬限额）。Claude Code及Codex CLI走中转均READY。tools/claude-task.ps1和tools/codex-task.ps1默认auto，仅额度事件切换一次、认证/预算错误停止。tools/codex-subrouter.ps1为直接中转入口。独立CLI可按CONTINUE_TASK.md继续，但没有创建常驻/无限循环；不能自动替换桌面chat运行中的provider。密钥仅在gitignored本机文件，禁止读出或提交。

持续推进入口已建立：tools/project_runner.py run/status/stop。12项runner离线测试通过；单实例锁，默认最多6周期/4小时，每周期15分钟，失败/无进展/STOP立即退出。启动后父聊天只能读取本工作树，避免与独立Codex控制器同时写入。首次仅启动1周期实测，外部进程日志和心跳见orchestration/local/runner。
