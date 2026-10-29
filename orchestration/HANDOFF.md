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

PR#5已在ac89113双WindowsCI40831907354/40831845193通过后合并，merge74b12f0c0fb7a1942b78c673e4a7ad388ace25d8。用户确认参考图AI生成，按最终指令ChatGPT产图；新增生产idle-v1.png及确切提示词/ASSET_SOURCES清单，原参考图仍忽略。新分支codex/character-art先让Haiku接入idle贴图；原生成服务未提供，不虚称外部许可已核验。该步骤完成后继续敌人多样性和20分钟平衡。

ART-01a静态角色本地接入通过：107测试+原生贴图+2冷启动；选人文字裁切已改自动换行，截图已检查，缺失配置素材夹具预期exit1。实际游戏现显示ChatGPT生成蓝发鲸尾女仆，stats/碰撞/模拟不变。ART-01仍IN_PROGRESS，行走动画未完成；walk-draft-v1/v2在local，alpha背景实际为0，主要失败是尾巴跨512网格/边缘太近，下一步独立姿态帧方案，不能盲重试同图集。下一步提交当前PR/WindowsCI，再继续冲锋/重装敌人和20分钟正常平衡。所有外部进程已退出；runner旧agent_stop，勿当活跃实例。

PR#6已在598251c双WindowsCI通过后合并main，merge6c6250702503debd4f851f8faf68b037bada7faa。用户授权试Tripo生产角色，现IAB tab1工作区上传按钮弹登录窗，需要用户本人登录；已保存tripo-login.jpg并保留页面，未上传/未生成/未购买。Tripo默认可见性公开，登录后先核对私密选项、账户额度和导出，不盲点付费。代码独立继续P2-02b冲锋/重装敌人。

受控恢复周期：分支codex/enemy-variety。读取已保存P2-02生存策略任务，不重复探针。Claude Haiku单次调用在约176秒后明确ECONNREFUSED，脚本未切备用、未编辑；不是认证/额度拒绝。本会话没有DeepSeek文件编辑入口，Codex接管仅修改src/core/run_simulation.cpp和tests/unit/kite_bot_test.cpp：HP低于0.75时生命优先级95、护甲85，保留新武器100；新增防御排序回归。当前机器执行shell下python安装无SCons模块、scons/py launcher不可用，访问用户目录受限；未找到可执行Godot入口，因此当前改动尚未构建、测试或模拟，严禁以现存旧exe/日志宣称通过。git diff --check通过，未提交。本轮结束前置任务断点；下周期首先在可用Windows开发桌面恢复锁定SCons/Godot验证入口，构建后运行全部测试、Headless/冷启动及真实JSON seed1/2 kite和seed2 stationary报告。再次connection reset/拒绝不得原样循环；费用unknown。

受控恢复（2026-10-29）：未重启任何子总控或模型。确认锁定Godot 4.7.2可执行文件可运行；`tools/build.ps1`失败（Python 3.11无SCons，pip wheel缓存为空）。`tools/test.ps1 -ColdStartCount 2`的现存测试程序报告118/118通过，但后续Godot导入因沙箱不能访问`C:/Users/67408/AppData/Roaming/Godot`及系统CA存储而失败；未修改项目外配置。该测试程序和扩展没有通过本次构建，不能据此验收当前源码。未跑模拟，未提交。下一周期需先恢复项目本地SCons及可写临时Godot设置目录，再从构建开始完成单测、Headless冷启动和真实JSON固定种子报告。差异检查通过；本次费用unknown，无认证/预算拒绝或连接重置。

P2-02b本地117测试+Headless+2冷启动通过，截图已审，正常616/623秒Lost。用户要求我用缓存Subrouter继续：已补runner --provider subrouter（13离线测试PASS），计划启动隐藏6周期/每周期900秒/共4小时的独立总控。启动后本聊天仅只读，不并发改树。CONTINUE_TASK已完整保存上下文/全部Phase0–5目标与当前优先级。Tripo已登录，200额度、默认公开/55、导出提示升级，未生成未购买；暂由桌面处理，不挡代码推进。Sonnet本次自设预算终止而非日预算拒绝，未重试；实际费用unknown。

Subrouter runner PID29232已于07:49:41UTC终止，reasonagent_exit_1；结构化错误为上游connection reset by peer，不是认证/预算拒绝。无遗留Claude或runner进程，仅新增已保存的P2-02-survival-policy-task.md，原117测试基线代码未改。已保存传输故障断点，显式medium减少单次长推理，自动request/stream重试仍0，仅计划一次受控恢复；再次同故障必须停止原样重启。用户明确取消Tripo，继续2D路线；取消所有Tripo待办，不购买/不生成。

2026-10-29桌面复核更正：宿主SCons/Godot/用户目录均正常；此前所谓工具链阻塞仅发生于内层Codex沙箱，不修改用户配置。runner476已退出，cycle3上游HTTP524/connection reset，是第二次传输失败而非预算/认证拒绝，禁止原样重启。宿主重新构建118并完成native/2cold后实测防御选卡实验485/500秒，故撤回。Haiku宿主曲线实验119测试/native/2cold通过，但真实seed1/2为598.82/732.20秒，seed1退步，亦撤回。两个实验均保留local审计，无默认角色20分钟胜利。新增受限job-request宿主Claude交接和host validation/simulation记录；22运行器用例21通过1因Windows符号链接权限跳过。完整Phase0–5继续，Tripo取消。

宿主最终重编译117/117、native Headless和2新目录cold PASS；真实JSON615.683345647/623.033345794/25.133333836，hash全部恢复原基线。runner22用例21PASS1权限skip；provider12检查+mock交接PASS。下一步当前敌人/宿主协议提交PR精确WindowsCI，通过后才合并。费用unknown，Subrouter用户100CNY/day后台硬限额仍为确认依据。
PR#7在7f9cded推送40843550847和PR40843589584双WindowsCI成功及审查后合并main003422ca2c127f18bd1951e731f05bfdaef10635。新分支codex/charger-aware-bot，任务P2-02-charger-aware-task.md。新的短总控/宿主交接协议仅计划一次2周期受控验证；内层不启动Claude或验证进程，第一周期请求Haiku0.25，宿主真实验证，第二周期审查后停止。若传输再失败保存诊断，不原样重启。预算确认100CNY/day，实际费用unknown。
