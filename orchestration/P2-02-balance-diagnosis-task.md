# P2-02 balance evidence diagnosis

基于 balance-timeline-v1 的真实固定种子报告整理证据摘要；禁止修改玩法常量、策略、敌人、卡片、被动或武器逻辑。

验收标准：
- 读取宿主已生成的结构化 timeline 报告并输出简短 Markdown 诊断，涵盖 seed1/seed2 kite 的死亡时间、伤害峰值、治疗/生命卡选择、敌人种类接触伤害，以及 stationary 基线。
- 明确区分结构测试通过与正常角色 1200 秒胜利未验收；不得提出或实施猜测式调参。
- 校验并记录 trace hash 不变：c3dcdf2b24529355、793dd79d12a2825c、184747d172b838a9。
- 仅允许新增诊断文档或审计结果，不得修改 src/、测试逻辑或模拟行为。
