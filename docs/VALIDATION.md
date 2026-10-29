# 验证记录

## 2026-10-29：Windows 原生启动与计时核心

目录 `E:/AI Survior/ai_survivor`，PowerShell 7，Python 3.11，SCons 4.11.1，MSVC 工具集 14.50.35717（cl 19.50.35728）。引擎 `4.7.2.stable.official.ed1daf0bf`，下载经官方 SHA512 校验；子模块提交见 tools/toolchain-lock.json。

- `./tools/build.ps1`：扩展 DLL、纯 C++ GoogleTest 程序编译链接成功。
- `./tools/test.ps1`：21 项测试通过；覆盖状态表、无效转移、终局锁定、负数/非有限时间、暂停/升级冻结、精确 1200 秒、超大步长、死亡冻结及重开；两项模拟测试包含每次 240000 步的 20 分钟计时模拟及重复一致性。
- 同一脚本运行 Godot Headless 导入及原生场景，输出 `AI_SURVIVORS_INTEGRATION_OK`，退出码 0，无 ERROR 日志。
- 自动录帧：Godot `--write-movie build/preview.png --quit-after 2 --fixed-fps 30`。检查 `build/preview00000001.png`（1100×680），文字完整居中、无裁切，正常窗口保持开启的场景可用。
- 测试曾发现菜单调用 resume 错误进入 Playing，已按命令语义限制来源状态；原有回归测试通过。
- 测试脚本曾捕获 Godot 对编译对象文件的错误导入（引擎退出 0 仍被判失败），已用 .gdignore 隔离构建和源码目录。

CI 结果待补充。当前模拟仅验证计时/状态，尚不验证战斗、敌人、经验或资源容量；Phase 1 实现后补充。
`nDeepSeek 实任务验收：session-e8475b0d-db97-40cc-a112-10446469f451，CLI exit 0，仅文件工具修改 tools/test.ps1 并交回；Codex 复验 21 tests PASS、Headless marker PASS。Claude 官方隔离探针 Not logged in，cost 0 USD，等待用户 OAuth。

Claude 官方通道验证：Haiku5.5 探针 READY，Haiku 真实读写任务退出0、无权限拒绝，新增 CommandsCannotSubstituteForEachOther；本地编译、22单测及Headless PASS。Sonnet5.5探针 READY。三次调用 CLI列表价格估算合计 USD 0.01999685（Pro订阅，非已确认现金扣费）；DeepSeek费用未知。

2026-10-29 Phase1-01：Sonnet实现CharacterDefinition/Registry、PlayerController、JSON角色加载、菜单/选择/战场/暂停；Codex审查修复UTF-8显示并增加滚动角色列表。Windows本机编译通过，32项GoogleTest和原生Headless集成通过。menu/select/battle/pause四张1100x680自动截图已检查，中文清晰、按钮无裁切、鱼形原创占位正常。当前无战斗/敌人/经验。
WindowsCI编译器库存实际证据run40767419942：14.29.30133、14.44.35207、14.51.36231；现显式允许CI14.51.36231、本机14.50.35717，SConstruct拒绝其他版本，未自动升级依赖。

2026-10-29 P1-02：Sonnet完成纯C++CombatWorld、EnemyManager、WeaponManager、ProjectileSystem、DamageSystem、WaveDirector；敌人追击、自动瞄准泡泡、接触伤害冷却、击杀事件、死亡/胜利覆盖层已接入。Codex实测本机构建、52项GoogleTest及现有缓存目录的Headless集成通过；build/combat00000089.png截图可见敌人、泡泡和击杀2。未实现经验/卡片。
新发现的启动回归：将最少运行文件复制到全新项目内build/clean-import-check，第一次Godot编辑器导入退出-1073741819，第二次退出0；无扩展plain-engine-check首次导入0。远端WindowsCI run40768586994同样首次导入访问冲突，不能将缓存目录成功等同于全新安装成功。SDK10.0.0-stable对照仍首次导入失败，已恢复原锁定commit1b089f2，未更改依赖。

2026-10-29 冷启动根因/缓解：与Godot官方已确认issue111048（EditorHelp文档生成在过早退出时的清理竞态）吻合。仅为编辑器导入加入 --frame-delay1000，原有非零码/ERROR/超时检查全部保留，禁止忽略失败或预写缓存。52单测、根目录集成及3个全新隔离目录的首次导入+原生集成都通过；每个新目录仅尝试一次。引擎和godot-cpp锁不变。云端WindowsCI待复验。

2026-10-29 P1-03：Sonnet实现经验掉落/拾取、连续升级队列、带权不放回三选一卡片、等级/稀有度/条件、六武器库存上限和泡泡弹升级。Codex审查修复有限大权重求和溢出偏置、初始运行属性与战斗限值不同导致无效奖励的问题，补回归测试。Windows本地编译、80项GoogleTest、原生Headless及2个全新冷启动目录均通过。build/upgrade00000002.png已检查：3张独立可读卡片，等级3且剩余2次选择，世界和计时冻结。原生集成实际拾取经验进入升级、拒绝旧generation、应用属性并恢复、重开清零。当前只实现泡泡弹攻击，未实现的新武器不会被卡池提供；其他攻击类型留待扩展。
