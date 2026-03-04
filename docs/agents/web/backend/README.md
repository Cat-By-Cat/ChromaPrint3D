# Web Backend 模块索引

## 模块职责

`web/backend/` 是基于 Drogon 的 HTTP 服务，提供 `/api/v1/*` 接口、会话管理、异步任务执行和校准板缓存能力。

## 分层结构

- 进程入口：`web/backend/server_main.cpp`
- 配置层：`web/backend/src/config/`
  - 参数定义与解析：`server_config.h` / `server_config.cpp`
- 基础设施层：`web/backend/src/infrastructure/`
  - 会话：`session_store.*`
  - 任务运行时：`task_runtime.*`
  - 数据加载：`data_repository.*`
  - 校准板缓存：`board_runtime_cache.*`
- 应用层：`web/backend/src/application/server_facade.*`
  - 聚合业务流程并返回统一 `ServiceResult`
- 表现层：`web/backend/src/presentation/`
  - 路由与请求处理：`api_v1_controller.*`
  - 运行时注册：`backend_runtime.*`
  - CORS 处理：`cors_advice.*`

## 常见改动落点

| 目标 | 入口文件 |
|---|---|
| 新增 API 路由 | `src/presentation/api_v1_controller.h/.cpp` |
| 新增业务能力 | `src/application/server_facade.h/.cpp` |
| 新增服务启动参数 | `src/config/server_config.h/.cpp` + `server_main.cpp` |
| 调整任务并发/TTL/内存预算 | `src/infrastructure/task_runtime.h/.cpp` |
| 调整会话策略与 token 行为 | `src/infrastructure/session_store.h/.cpp` |
| 调整 CORS / cookie 策略 | `src/presentation/cors_advice.*` + `api_v1_controller.*` |

## API 修改建议流程

1. 在 `ServerFacade` 定义并实现服务方法。
2. 在 `ApiV1Controller` 声明路由与处理函数。
3. 复用统一 envelope：`{ok,data}` / `{ok,error}`。
4. 若涉及会话，确认 cookie/header/query 的 token 识别链路不被破坏。
5. 同步更新 [README.md](../../../../README.md) 与 [docs/development.md](../../../development.md) 的接口说明。

## 最小验证

```bash
cmake --build build -j$(nproc)
build/bin/chromaprint3d_server --help
```

本地接口冒烟（服务启动后）：

```bash
curl -s http://127.0.0.1:8080/api/v1/health
```

## 相关任务手册

- [docs/agents/tasks/add_api_endpoint.md](../../tasks/add_api_endpoint.md)
- [docs/agents/tasks/extend_cli_flag.md](../../tasks/extend_cli_flag.md)（当后端参数与 CLI 参数需要联动）

