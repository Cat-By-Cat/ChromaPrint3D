# ChromaPrint3D Electron 壳

`web/electron` 提供桌面运行壳，负责：

- 启动并管理后端 `chromaprint3d_server`
- 加载前端页面（开发态 URL / 打包态本地静态文件）
- 通过 `preload` 向渲染层注入 `window.electron` 能力

## 开发模式

### 1) 准备后端可执行文件

在仓库根目录：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target chromaprint3d_server -j
```

### 2) 安装依赖并启动

```bash
cd web/electron
npm install
npm run dev
```

`npm run dev` 会同时启动：

- 前端 Vite 开发服务（默认 `http://127.0.0.1:5173`）
- Electron 主进程与窗口
- 后端服务（由主进程自动拉起，默认从 `18080` 起探测空闲端口）

## 运行时路径规则

### 开发态（`app.isPackaged === false`）

- 渲染层默认地址：`http://127.0.0.1:5173`
- 后端默认路径：`<repo>/build/bin/chromaprint3d_server`
- 数据目录默认：`<repo>/data`

### 打包态（`app.isPackaged === true`）

- 渲染层默认入口：`<resources>/frontend-dist/index.html`
- 后端默认路径：`<resources>/backend/chromaprint3d_server(.*)`
- 数据目录默认：`<userData>/data`（首启自动从 `<resources>/data` 同步基础数据）

如需覆盖默认路径，可使用环境变量（见下节）。

## 常用环境变量

- `CHROMAPRINT3D_RENDERER_URL`：覆盖渲染层加载地址
- `CHROMAPRINT3D_BACKEND_PATH`：覆盖后端可执行文件路径
- `CHROMAPRINT3D_BACKEND_PORT`：指定后端优先端口
- `CHROMAPRINT3D_DATA_DIR`：覆盖 `--data` 目录
- `CHROMAPRINT3D_MODEL_PACK_PATH`：覆盖 `--model-pack` JSON 路径
- `CHROMAPRINT3D_MAX_UPLOAD_MB`：覆盖后端 `--max-upload-mb`（默认 256）
- `CHROMAPRINT3D_MAX_PIXELS`：覆盖后端 `--max-pixels`（默认 16384 x 16384）
- `CHROMAPRINT3D_MAX_RESULT_MB`：覆盖后端 `--max-result-mb`（默认 2048）
- `CHROMAPRINT3D_MODEL_DOWNLOAD_RETRIES`：单下载源重试次数（默认 3）
- `CHROMAPRINT3D_MODEL_DOWNLOAD_TIMEOUT_MS`：单次请求超时（默认 600000）
- `CHROMAPRINT3D_MODEL_DOWNLOAD_BACKOFF_MS`：重试退避基值（默认 1500）

## 打包与产物结构

### 本地打包（未签名）

1) 构建 Electron 兼容前端（相对资源路径）：

```bash
cd web/frontend
CHROMAPRINT3D_FRONTEND_TARGET=electron npm run build
```

2) 安装 Electron 依赖并装配 resources：

```bash
cd web/electron
npm install
CHROMAPRINT3D_FRONTEND_DIST_DIR=../frontend/dist \
CHROMAPRINT3D_BACKEND_ARCHIVE=/abs/path/to/backend-<platform>.tar.gz \
CHROMAPRINT3D_BACKEND_PLATFORM=linux \
npm run prepare:resources
```

> Windows 后端包请传 `.zip`，并将 `CHROMAPRINT3D_BACKEND_PLATFORM=windows`。

3) 产出安装包：

```bash
cd web/electron
CHROMAPRINT3D_ELECTRON_PLATFORM=linux \
CHROMAPRINT3D_ELECTRON_ARCH=x64 \
npm run dist:ci
```

默认产物在 `web/electron/release/`。

### CI 打包链路（Release）

- 工作流：`.github/workflows/release-electron.yml`
- 依赖输入：`backend-*` 与 `frontend-dist` artifacts
- 核心步骤：
  - 前端以 `CHROMAPRINT3D_FRONTEND_TARGET=electron` 重新构建
  - 运行 `npm run prepare:resources` 组装依赖链
  - 运行 `npm run smoke:ci` 执行结构/可执行/短时启动检查
  - 运行 `npm run dist:ci` 输出安装包并上传 `electron-*` artifact

### 目录约定

`electron-builder` 打包前，资源目录固定为：

```text
web/electron/resources/
  frontend-dist/   # 前端静态产物
  backend/         # 后端可执行文件与动态库
  data/            # model_pack 与 ColorDB 数据
  icons/           # 从 web/frontend/public/favicon.svg 生成的平台图标
```

打包输出目录：

```text
web/electron/release/
  ChromaPrint3D-<version>-<os>-<arch>.<ext>
```

当前桌面端发布范围为“未签名内测包”，暂不包含签名、公证与自动更新。

## preload 暴露能力

`window.electron` 当前包含：

- `env.apiBase`
- `storage.getItem / setItem`
- `theme.getSystemDarkMode / setWindowBackground`
- `download.openExternal / saveUrlAs / saveObjectUrlAs`
- `file.pickSingleFile`
- `models.getStatus / checkConnectivity / startDownload / cancelDownload / restartApp / onProgress`

类型契约在 `web/frontend/src/electron.d.ts`。

## 安装后手动下载抠图模型

- 安装包不内置 `.onnx`，首次使用抠图功能时可在 Electron 界面手动下载。
- 可先点击“下载前检查”，查看每个下载源的可达性与响应耗时，再决定是否下载（检查结果会短期缓存，避免重复探测；为缩短等待，默认只做少量样本探测）。
- 下载器运行在主进程，支持：
  - 断点续传（`.part` + HTTP Range）
  - SHA256 校验
  - 自动重试（指数退避）
  - 多源回退（优先国内源，再回退）
- 下载面板会展示需下载大小、已下载大小与实时网速。
- 模型清单为 `data/models/models.json`，支持 `sources` 多下载源配置。
- 下载完成后需重启应用，后端才会在启动时加载深度学习抠图模型。

## 维护者：上传模型到国内源（ModelScope）

建议将国内源仓库结构保持与 `data/models/models.json` 的 `models[].path` 一致（例如 `matting/u2net.onnx`）。

1) 在 ModelScope 创建模型仓库（例如 `neroued/ChromaPrint3D-models`）。

2) 本地克隆并启用 LFS：

```bash
git lfs install
git clone "https://www.modelscope.cn/models/neroued/ChromaPrint3D-models.git"
cd ChromaPrint3D-models
```

3) 复制/更新模型文件并提交：

```bash
mkdir -p matting
cp /path/to/u2net.onnx matting/
cp /path/to/u2netp.onnx matting/
cp /path/to/isnet-anime.onnx matting/
git add matting/*.onnx
git commit -m "update matting onnx models"
git push
```

4) 在本仓库更新 `data/models/models.json`：
- `sources` 中配置：
  `https://www.modelscope.cn/models/<org>/<repo>/resolve/master`
- 保持 `models[].path / sha256 / size_bytes` 与已上传文件一致。

## 安全基线

- `contextIsolation: true`
- `sandbox: true`
- `nodeIntegration: false`
- 新窗口默认禁止（`setWindowOpenHandler`）
- 页面导航限制为可信来源（开发态同源 / 打包态本地 `file://`）
- `openExternal` 仅允许 `http/https/mailto`

## 常见问题

- 启动报 `Backend binary not found`
  - 开发态：先在仓库根目录编译后端
  - 打包态：确认资源目录包含 `backend/chromaprint3d_server(.*)`
- 页面空白 / 无法加载
  - 开发态：确认 `5173` 可访问
  - 打包态：确认 `frontend-dist/index.html` 已放入 resources
- 接口连接失败
  - 检查 `data/` 与 `model_pack/model_package.json` 是否存在
  - 可用 `CHROMAPRINT3D_DATA_DIR`、`CHROMAPRINT3D_MODEL_PACK_PATH` 覆盖路径
- 模型下载失败
  - 检查网络是否可访问 `models.json` 中配置的下载源
  - 在抠图面板点击“刷新状态”查看当前缺失模型
  - 如反复失败可调整 `CHROMAPRINT3D_MODEL_DOWNLOAD_TIMEOUT_MS` 与重试参数
