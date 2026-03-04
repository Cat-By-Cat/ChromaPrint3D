# 扩展 CLI 参数

## 适用场景

为 `apps/*.cpp` 中任一命令行工具新增、修改或废弃参数（含默认值变化）。

## 影响文件（按优先级）

- `apps/<tool>.cpp`
- `apps/CMakeLists.txt`（仅在新增工具时）
- [apps/README.md](../../../apps/README.md)
- [README.md](../../../README.md)
- [docs/development.md](../../development.md)

## 实施步骤

1. 在对应 `apps/<tool>.cpp` 增加参数解析、合法性检查与 `--help` 文案。
2. 将新参数映射到核心库请求结构（例如 `ConvertRasterRequest` 或 `VectorizerConfig`）。
3. 补齐默认值，并确保与文档描述一致。
4. 若参数语义与 Web/API 共用，检查后端与前端的同名字段是否需要联动更新。
5. 更新 [apps/README.md](../../../apps/README.md) 以及仓库级文档中的命令示例。

## 风险点

- 参数值解析与边界检查不充分（负值、空值、非法枚举）。
- `--help` 文案与真实行为不一致。
- CLI 默认值与 API/前端默认值分叉，导致结果不一致。
- 新参数未传递到核心逻辑，成为“无效参数”。

## 回归检查

- 构建检查：
  - `cmake --build build -j$(nproc)`
- 帮助信息检查：
  - `build/bin/<tool> --help`
- 功能检查：
  - 用最小输入跑一轮命令并验证输出产物存在
- 文档检查：
  - [apps/README.md](../../../apps/README.md)、[README.md](../../../README.md)、[docs/development.md](../../development.md) 已同步

