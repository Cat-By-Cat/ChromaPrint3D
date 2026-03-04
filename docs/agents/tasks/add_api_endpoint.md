# 新增 API 端点

## 适用场景

新增 `/api/v1/*` 路由，或为现有业务新增一个请求入口（含 JSON 路由与二进制下载路由）。

## 影响文件（按优先级）

- `web/backend/src/presentation/api_v1_controller.h`
- `web/backend/src/presentation/api_v1_controller.cpp`
- `web/backend/src/application/server_facade.h`
- `web/backend/src/application/server_facade.cpp`
- `README.md`
- `docs/development.md`

## 实施步骤

1. 在 `ServerFacade` 设计服务方法，约定输入、返回码与 `ServiceResult`。
2. 在 `ApiV1Controller` 注册路由（`METHOD_LIST_BEGIN`），声明并实现处理函数。
3. 路由层仅做请求解析与鉴权/会话提取，核心逻辑放到 `ServerFacade`。
4. 保持返回约定：
   - JSON 路由：`{ok,data}` / `{ok,error}`
   - 二进制路由：直接返回 bytes
5. 若接口需要会话，复用既有 token 识别优先级（cookie/header/query）。
6. 更新对外文档（接口列表、参数、错误码、行为边界）。

## 风险点

- 路由方法签名或路径参数顺序与 Drogon 注册不一致。
- JSON envelope 破坏，导致前端解析失败。
- 会话与跨域策略不一致（尤其跨域 cookie 模式）。
- 接口存在长耗时但未走任务系统，阻塞 HTTP 工作线程。

## 回归检查

- 构建检查：
  - `cmake --build build -j$(nproc)`
- 服务启动检查：
  - `build/bin/chromaprint3d_server --data data --port 8080`
  - `curl -s http://127.0.0.1:8080/api/v1/health`
- 接口检查：
  - 使用 `curl` 或前端页面确认新增路由状态码、响应体与错误分支
- 文档检查：
  - `README.md`、`docs/development.md` 已同步
  - 如涉及部署行为，`docs/deployment.md` 也已同步

