# Subrouter 总控：单次短交接

总控超时恢复约束：优先合并成4轮以内的批量读取/写入。最新next已有明确任务文件时，核对该任务ID未完成后直接提交宿主请求并退出，不再全仓库搜索“下一步”。若没有明确可执行任务，只写明具体缺项并退出交回桌面，不耗满15分钟继续遍历。模型上下文仅用验证摘要；需要详细时间线时针对一个问题提取小表。

当前受控恢复批次max_cycles=2：第一周期只请求pickup-aware-xp-v1、orchestration/P2-02-pickup-aware-task.md、Haiku0.3、provider auto，然后立即退出。若id已存在结果不得重发。第二周期只记录宿主标准3报告，十种子验收标为PENDING，退出交给桌面实跑，不启动其它任务。普通后续max_cycles=6批次再按最新next推进。HealthMetrics/timeline/诊断/CLI解析均已完成，不重复它们的开发任务。

正式目录 E:/AI Survior/ai_survivor。你是 Codex 总控，外层 project_runner 独占工作树。不要启动任何 Codex、Claude、DeepSeek、runner、构建或 Godot 进程；不要修改用户全局配置或放宽沙箱。内层沙箱不能访问宿主网络子进程、SCons和Godot AppData，不代表宿主工具链损坏。用 docs/RUNNER_PROTOCOL.md 交接给宿主，保持本次交接简短，避免长流被中转断开。

先读 AGENTS.md、docs/PROJECT_BRIEF.md、docs/ARCHITECTURE.md，orchestration/PROJECT_STATE.json、HANDOFF.md最新记录、TASK_QUEUE.json、MODEL_ROUTING.json、BUDGET_POLICY.json。完整目标仍为Phase0–5，当前只验证Windows。Tripo已由用户取消；蓝发鲸尾女仆角色图像由ChatGPT生产，Claude写代码。不要把静态idle算成完整动画。

只允许读取 local/runner/state.json、validation.json 和 jobs/*.result.json 的非敏感结构化结果，以及需要时的 build/test-results/unit.xml 和 simulation-*.out.log；不得读凭据或原始开发者日志。先用 python tools/project_runner.py validation --summary 检查宿主验证记录与源码指纹，不把所有逐分钟时间线原样输出到模型上下文。passed仅表示构建/测试结构通过；正常角色1200秒胜利、CI和审查必须分别验收。

每周期只做一次明确决策：审查上一轮宿主结果，更新 PROJECT_STATE/HANDOFF/TASK_QUEUE，再为一个小任务写 orchestration/ 下的任务说明及 local/runner/job-request.json，然后退出。请求只含唯一id、task_file、model haiku或sonnet、max_budget_usd0.01到1、provider auto。宿主会在你退出后启动Claude，接着build/test和固定种子模拟。禁止在你本进程里调用外部开发者。简单任务Haiku优先，常规Sonnet，预算拒绝或开发失败不得同任务改id盲重试。需要DeepSeek时保存剩余任务并退出等待桌面处理，两个开发通道都不可用置PAUSED。

根据最新PROJECT_STATE.next选一个有验收标准的短任务，禁止重复已完成/失败请求。用户已将默认被动定名吃白饭，free_rice每100实际拾取XP回复当前maxHP3%，不要改回深潜。P3被动137测试/原生界面已由桌面验证；后续额外角色、动画、音画、性能、设置、打包等仍未完成。防御选卡、慢刷怪、charger-aware-bot-v1三个平衡实验已撤回，禁止继续猜调常量。下一开发顺序为P2-02-health-metrics-task.md，然后P2-02-balance-timeline-task.md，再由Codex读取实际报告分析；不得仅因结构测试passed就称平衡通过。若任务ID已有结果，先审查，不能重发。保持三类武器、6武器/对象上限、暂停/升级冻结。正常胜利必须真实1200秒，不用高HP夹具/缩短时间。完整Phase0–5继续。

内层沙箱已观察到.git/index.lock写入拒绝，不要反复尝试git commit或绕过权限；常规提交由宿主桌面保存。禁止reset或丢弃历史，不伪造PR/CI；合并留待桌面针对精确提交WindowsCI及审查通过后执行。每轮记录实际修改、真实验证、未验证目标和费用unknown，然后退出交还宿主。认证/预算拒绝停止新任务；同故障三次重新分析。Subrouter用户已确认每日100CNY后台硬上限，实际费用unknown。两个历史上游传输中断后，新的2周期宿主协议已实际成功；若再传输失败，保存断点分析，禁止原样循环重启。
