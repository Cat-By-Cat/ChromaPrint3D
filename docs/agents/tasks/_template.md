# <任务名称>

## 适用场景

简述何时使用本手册。

## 影响文件（按优先级）

- `path/to/file_a`
- `path/to/file_b`
- `path/to/file_c`

## 实施步骤

1. 确认输入约束与目标行为
2. 修改核心逻辑
3. 补齐参数映射与边界处理
4. 同步文档与索引

## 风险点

- 行为兼容性风险
- 默认值变化导致的隐式回归
- 多端参数不一致

## 回归检查

- 构建检查：
  - `<build command>`
- 功能检查：
  - `<smoke command>`
- 文档检查：
  - 是否更新 [README.md](../../../README.md) / [docs/development.md](../../development.md) / [docs/deployment.md](../../deployment.md)
  - 是否更新 [docs/agents/](../) 导航与任务手册

