# 调整前端参数联动

## 适用场景

后端新增或变更参数后，需要同步前端默认值、表单构建、请求序列化和结果展示逻辑。

## 影响文件（按优先级）

- `web/frontend/src/domain/params/convertDefaults.ts`
- `web/frontend/src/domain/params/convertParamBuilders.ts`
- `web/frontend/src/components/ParamPanel.vue`
- `web/frontend/src/components/param/*.vue`
- `web/frontend/src/api.ts`
- `web/frontend/src/services/convertService.ts`
- [README.md](../../../README.md)
- [docs/development.md](../../development.md)

## 实施步骤

1. 明确参数来源：后端默认值、前端覆盖值、运行时环境变量。
2. 更新默认值模型（`convertDefaults.ts`）与请求构建器（`convertParamBuilders.ts`）。
3. 更新参数面板 UI（控件、描述、禁用条件、校验规则）。
4. 确认 `api.ts` 的请求字段命名与后端契约一致。
5. 如果参数影响结果展示，更新 `ResultPanel.vue` 或相关 domain 逻辑。
6. 更新参数说明文档与示例。

## 风险点

- 前端字段名与后端字段名不一致。
- 默认值分叉（前端显示与后端真实默认值不一致）。
- 条件禁用逻辑错误，导致参数无效或误提交。
- 测试未覆盖到新参数分支。

## 回归检查

- 前端质量检查：
  - `cd web/frontend && npm run lint`
  - `cd web/frontend && npm run test`
  - `cd web/frontend && npm run build`
  - 仅排查类型问题时可额外执行：`cd web/frontend && npm run typecheck`
- 联调检查：
  - 启动后端后在页面提交任务，确认参数生效
- 文档检查：
  - 参数说明已同步 [README.md](../../../README.md) / [docs/development.md](../../development.md)

