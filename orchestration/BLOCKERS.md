# 当前状态（2026-10-29）

- 没有阻止常规开发的认证或工具链阻塞。当前仅验收 Windows。
- Claude Haiku/Sonnet 官方可用；DeepSeek 文件开发和 Subrouter 备用已验证；GitHub 插件 PR 操作可用。
- Subrouter 每日100CNY后台硬上限由用户确认；实际花费unknown。
- PR1–5均在各自精确提交的 Windows CI 通过后合并。当前角色贴图接入本地107测试、Headless、2冷启动通过，待新PR/CI。
- 三类武器与改进策略已实现；默认角色seed1/2约621/614秒Lost，正常20分钟平衡未证明；冲锋/重装敌人仍待实现。
- 用户最终要求ChatGPT产美术、Claude写代码。蓝发鲸尾女仆idle已接入开发版；行走图集布局未验收，需独立动作帧等新方案；参考来源与生成记录见assets/ASSET_SOURCES.md。
- 历史Sonnet单次调用达到自设预算上限后已停止、未重试；当前不是每日预算阻塞。两个Claude艺术调用因用户更改任务主动停止，不是额度/认证故障。
- 独立runner旧state为agent_stop，PID不存在；当前聊天接管，禁止并发工作树写入。
