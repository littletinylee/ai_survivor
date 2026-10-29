# 恢复入口

正式工作目录 E:/AI Survior/ai_survivor，用户克隆。父目录保留早期断点。分支 feat/phase0-bootstrap 已推送；读取 docs/VALIDATION.md、PROJECT_STATE.json 和 TASK_QUEUE.json。

当前 Codex gpt-6.1-sol 完成原生场景、SCons、Windows 构建/测试脚本、工具链锁及 Linux CI；修复状态命令来源错误和 Godot 导入编译对象问题。21 项单测、Windows Headless 和截图已验证。Linux 构建通过，集成失败；提交 7ae82cd 为 CI 增加错误注释便于通过公开 API读取。main 尚未合并，PR 尚未创建。

Claude 先前 403，禁止循环重试；DeepSeek deepseek-flash 探针成功但沙箱编译受阻。仅在能力/预算就绪后再路由，不需要重复空探针。本轮没有额外调用付费外部模型，Codex 费用无可获取账本。

下一步定位 CI、完成 PR 验收，然后补预算和有界执行器。不要将计时模拟描述为完整战斗模拟。100 CNY/日，北京时间自然日；无可核验成本时不启动无人值守。

优先级更新：用户要求节省 Codex 额度、优先其他模型。Claude 官方 OAuth 已由用户登录；隔离全局 OpenRouter 设置后 Haiku5.5 和 Sonnet5.5 实测 READY，Haiku 写入一项真实状态命令回归测试，22 单测及 Windows Headless PASS。启动 tools/claude-task.ps1，默认 Haiku；常规实现 -Model sonnet。DeepSeek 文件工具任务同样已验证。后续代码优先 Claude，本地脚本执行构建测试，Codex仅做统筹验收。Linux CI import仍exit134（三次后必须重新分析，不能重复猜测修复），下一任务交 Sonnet，先增加引擎无扩展基线对照和可靠崩溃日志。

Subrouter备用已接通（用户确认Key后台每日100CNY硬限额）。Claude Code及Codex CLI走中转均READY。tools/claude-task.ps1和tools/codex-task.ps1默认auto，仅额度事件切换一次、认证/预算错误停止。tools/codex-subrouter.ps1为直接中转入口。独立CLI可按CONTINUE_TASK.md继续，但没有创建常驻/无限循环；不能自动替换桌面chat运行中的provider。密钥仅在gitignored本机文件，禁止读出或提交。

持续推进入口已建立：tools/project_runner.py run/status/stop。12项runner离线测试通过；单实例锁，默认最多6周期/4小时，每周期15分钟，失败/无进展/STOP立即退出。启动后父聊天只能读取本工作树，避免与独立Codex控制器同时写入。首次仅启动1周期实测，外部进程日志和心跳见orchestration/local/runner。

优先级覆盖：用户要求当前仅Windows，停止Linux排障。独立运行器首轮已terminal agent_stop且PID不存在，当前Goal接管；下一任务打通WindowsCI然后Phase1，不得重启Linux排障。

Phase1-01本机已验收（32测试+Headless+4截图），云端WindowsCI待确认；代码角色/移动/菜单已接入，禁止声称完整游戏。下一步P1-02自动攻击、追击敌人、伤害死亡。CI工具集14.51.36231按库存显式锁定，本机仍14.50.35717。

P1-02已由Sonnet实现并经Codex52单测/缓存目录Headless/战斗截图验证。关键回归：全新最小项目首次加载扩展在Windows复现访问冲突(-1073741819)，再次导入0；无扩展基线首次0。稳定SDK对照不解决，已恢复原SDK锁。优先修复冷启动，不合并main；GitHub连接器现在可用了，将创建草稿PR。下一任务由Sonnet分析首次动态扩展装载/卸载路径，禁止忽略第一次崩溃或提交.godot缓存绕过。

冷启动已在本地缓解：官方issue111048明确是EditorHelp文档后台回调与过早退出竞态，使用导入参数--frame-delay1000保留所有失败检查。新增冷启动回归，每次唯一新目录无缓存，3个目录首次导入+游戏集成全部0。52单测通过。SDK/引擎保持原锁。草稿PR#1已创建并附到聊天；待WindowsCI确认后方可合并。

Windows推送与PR CI均在0d58b1f完成success。PR#1已验收并合并main，merge=dbead02acaff65c2e860fa6c313f5a8fe3efc63f，未绕过检查。新分支feat/experience-upgrades开始P1-03；P0-01/P1-01/P1-02已DONE。现有玩法还没有经验和卡片，继续纯C++实现。

P1-03经验/三选一/连续升级已实现并本地80单测、Headless(含真实经验拾取升级)及2冷启动通过，截图已看。卡片应用使用generation+ID拒绝重复/旧回调，泡泡弹升级有效；六槽库存/容量过滤有测试，但真实攻击仍只泡泡弹，无效未实现武器不会出现。此轮Sonnet编写，Codex补TextServer/Texture2D绑定依赖并修数值边界。下一步WindowsCI/PR合并，再完整20分钟战斗+经验+卡片模拟（不能拿仅计时模拟替代）。

PR#2已在dfafce1推送/PR双CI成功后合并main（699d24c94974709978bad587daf26ee351a983c7）。P1-03 DONE，新分支feat/full-run-simulation做P1-04。要求模拟与实际场景复用同一纯C++帧顺序和升级应用逻辑；正常鱼AI平衡与受控胜利夹具分开报告，不能拿无敌参数或仅计时测试冒充正常20分钟体验。

P1-04本地验收：Sonnet共享帧/完整模拟，Haiku原生真实JSON适配；87测试、Headless、2冷启动和实际报告通过，详见docs/SIMULATION.md。正常鱼自动策略35.77/47.22秒Lost，不能宣称20分钟平衡通过。下一步提交PR、WindowsCI，之后P2-01实际三类武器；TASK_QUEUE已补Phase2–5，不能把最小版本当整个项目完成。外部开发者当前均已退出，runner旧PID36136不存在。实际模型费用unknown，CLI列表估价不是账单。

PR#3已在8c9cad5推送/PR双WindowsCI成功后合并main，merge=ae4d7973041c4a75b3c80c4e24a43d19ca2f3c38。P1-04功能流程DONE，不代表默认鱼平衡完成。新分支codex/weapon-types开始P2-01，Claude Sonnet负责实现，运行时Codex只读。

P2-01三类真实武器本地完成：100测试+Headless实际卡片/区域/升级+2冷启动通过，装备栏/黄环/绿池截图已检查。Sonnet session12972/exec82399已终止（error_max_budget_usd，列表估价1.0084553USD，不是已知实际扣费）；没有重试付费调用。Codex接管修复并验证。下一步PR/WindowsCI，成功才合并。正常鱼seed1自动策略150.53秒Lost，后续P2-02改善敌人/策略/平衡，绝不宣称已通20分钟。当前无外部写入者，允许Codex修改；runner仍旧agent_stop。

PR#4在d3399da推送/PR双WindowsCI通过后合并main（0c582a3a40f98730d4b9f7053a0b705e889e1ed3），P2-01 DONE。P2-02先拆较小的模拟避障/选卡任务给Haiku，保持真实战斗数值不变以建立性能基线；之后再处理敌人多样性和正常角色20分钟平衡。

P2-02a Haiku策略任务成功（CLI列表估价0.03433168USD）；Codex修接触边界代价不连续，最终107测试+Headless+2冷启动通过，真实鱼seed1/2约621/614秒Lost；未证20分钟平衡。用户美术新指令优先：大肥鱼是蓝发鲸尾女仆少女，图片生产最终选ChatGPT。Claude艺术调用已因用户更改输入/生产者主动停止，不是认证/额度失败，不应重试。ChatGPT image_gen已产透明idle V1于orchestration/local/art，RGBA1314x1197；未接入游戏，来源许可unknown仅内部候选。docs/ART_DIRECTION.md和MODEL_ROUTING已更新。仍需提交策略PR/CI、敌人多样性、20分钟平衡、角色素材接入和完整Phase3-5。
