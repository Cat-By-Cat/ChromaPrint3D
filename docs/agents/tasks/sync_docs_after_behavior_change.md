# 行为变更后的文档同步

## 适用场景

任何用户可见行为变化，或 API/参数/构建部署流程变化后，需要系统化同步文档，避免“代码已变更、文档仍旧版本”。

## 影响文件（按优先级）

- `README.md`
- `docs/development.md`
- `docs/deployment.md`
- `docs/agents/README.md`
- `docs/agents/<module>/README.md`
- `docs/agents/tasks/*.md`

## 实施步骤

1. 判定变更类型：
   - API 端点或参数变化
   - 配置项/命令行参数变化
   - 构建或部署流程变化
   - 用户可见交互变化
2. 选择文档落点：
   - 全局入口写到 `README.md`
   - 开发行为契约写到 `docs/development.md`
   - 环境和拓扑写到 `docs/deployment.md`
   - 模块导航与落点写到 `docs/agents/*`
3. 更新受影响命令示例、默认值、限制条件和故障排查。
4. 检查文档之间是否互相可跳转（入口链接完整）。

## 风险点

- 只改一处文档，其他入口仍指向旧行为。
- 参数默认值在不同文档里不一致。
- 任务手册未更新，后续协作继续按旧流程执行。

## 回归检查

- 文档完整性检查：
  - 从 `AGENTS.md` 能跳到模块索引与任务手册
  - 从 `README.md` / `docs/development.md` / `docs/deployment.md` 能跳回 AGENTS 导航
- 一致性检查：
  - 关键参数默认值在多份文档中一致
  - API 列表、命令示例与当前实现一致

