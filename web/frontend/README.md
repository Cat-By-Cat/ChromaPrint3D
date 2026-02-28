# ChromaPrint3D Frontend

前端采用 `Vue 3 + TypeScript + Vite + Naive UI`，并已完成面向 Electron 的兼容抽象准备（当前仍以浏览器模式运行）。

## 常用命令

```bash
cd web/frontend

# 开发模式（默认代理 /api -> localhost:8080）
npm run dev

# 类型检查
npm run typecheck

# 代码检查
npm run lint
npm run lint:fix

# 格式化
npm run format
npm run format:check

# 单元测试 + 覆盖率
npm run test

# 生产构建
npm run build
```

## 目录分层（重构后）

```text
src/
├── components/              # 视图组件
│   └── param/               # 参数面板子组件
├── composables/             # 通用交互逻辑（异步任务、缩放拖拽、下载等）
│   └── feature/             # 页面级生命周期编排
├── domain/params/           # 参数域模型与构建器
├── runtime/                 # 运行时能力抽象（browser/electron 兼容层）
├── services/                # 业务服务编排
├── stores/                  # Pinia 全局状态
├── api.ts                   # 后端 API 封装
└── electron.d.ts            # 预留 Electron IPC 类型契约
```

## Electron 兼容准备说明

当前前端已把浏览器强耦合能力抽象到 `runtime/*`：

- `runtime/env.ts`：API 基址来源抽象
- `runtime/storage.ts`：主题等持久化抽象
- `runtime/download.ts`：下载与外链打开抽象
- `runtime/file.ts`：文件选择能力抽象
- `runtime/platform.ts`：运行环境识别

后续接入 Electron 时，只需要在 `preload` 中实现 `window.electron` 对应能力并保持现有接口不变，渲染层代码可直接复用。
