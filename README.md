# ChromaPrint3D

[English](#english) | [中文](#中文)

---

<a id="中文"></a>

## 简介

ChromaPrint3D 是一个多色 3D 打印图像处理系统。它将输入图像的颜色映射为多通道打印配方，并导出可直接用于多色 3D 打印机的 3MF 文件。项目包含 C++20 核心库、命令行工具、HTTP 服务器、Web 前端以及基于 Beer-Lambert 定律的 Python 颜色建模管线。

### 主要功能

- **颜色数据库（ColorDB）**：加载/保存颜色数据库，支持 Lab 和 RGB 色彩空间的 KD-Tree 最近邻匹配
- **图像预处理**：缩放、去噪、透明度遮罩
- **独立图像抠图**：支持 OpenCV 阈值抠图和深度学习模型（运行时可选），提供独立 API 和 Web 界面
- **配方匹配**：基于 ColorDB 的颜色匹配，可选基于物理模型的候选生成
- **3MF 导出**：通过 lib3mf 将体素网格转为 3MF 多色模型
- **校准板生成**：自动生成校准板 3MF 和元数据文件，支持 2-8 色
- **Web 服务**：完整的 HTTP API 和 Vue 3 前端界面
- **Python 建模管线**：基于薄层堆叠 Beer-Lambert 模型的颜色预测与参数拟合

## 目录结构

```
ChromaPrint3D/
├── core/               # C++ 核心库（颜色匹配、体素化、3MF 导出等）
├── infer/              # 深度学习推理引擎封装（ONNX Runtime 后端）
├── apps/               # 命令行工具
├── web/
│   ├── frontend/       # Web 前端（Vue 3 + TypeScript + Naive UI）
│   └── backend/        # HTTP 服务器（C++ Drogon）
├── modeling/           # Python 颜色建模管线
│   ├── core/           #   核心模块（前向模型、色彩空间、IO 工具）
│   └── pipeline/       #   管线步骤（5 步流程）
├── data/               # 运行时数据
│   ├── dbs/            #   ColorDB JSON 文件
│   ├── model_pack/     #   模型包 JSON 文件
│   ├── models/matting/ #   抠图模型文件（.onnx）与配置（.json）
│   └── recipes/        #   预计算配方文件（如 8 色校准板）
├── scripts/            # 辅助脚本
│   └── download_models.py  # 下载 ONNX 模型（跨平台，Python 3.8+）
├── 3dparty/            # 第三方依赖（git submodules）
├── Dockerfile          # 运行时容器
└── Dockerfile.build    # 编译环境容器
```

## 获取代码

```bash
git clone https://github.com/neroued/ChromaPrint3D.git
cd ChromaPrint3D
git submodule update --init --recursive
```

## 构建

### 本地编译

#### 系统要求

- CMake >= 3.25
- C++20 编译器（推荐 g++-13 或更高版本）
- OpenCV >= 4.5 开发库
- Node.js >= 22（用于 Web 前端构建）
- Python >= 3.10 + NumPy + OpenCV（用于建模管线，可选）

#### 安装依赖

```bash
# Ubuntu / Debian
sudo apt install libopencv-dev

# macOS
brew install opencv
```

#### 编译 C++ 核心与应用

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

CMake 构建选项：

| 选项 | 默认值 | 说明 |
|---|---|---|
| `CHROMAPRINT3D_BUILD_APPS` | `ON` | 构建可执行文件 |
| `CHROMAPRINT3D_BUILD_TESTS` | `OFF` | 构建单元测试 |
| `CHROMAPRINT3D_BUILD_INFER` | `ON` | 构建深度学习推理模块（ONNX Runtime 自动下载） |

**加速 C++ 构建（含 lib3mf）**：项目已集成 lib3mf 源码，首次编译较慢。可安装 [ccache](https://ccache.dev/)，CMake 会自动启用，重复编译与增量构建会明显加快。Release 流水线中已对 `build/` 与 ccache 做缓存，第二次及以后的发布构建会跳过 lib3mf 的重新编译。

编译完成后，可执行文件位于 `build/bin/`：

| 可执行文件 | 说明 |
|---|---|
| `chromaprint3d_server` | HTTP 服务器（含 Web API） |
| `gen_calibration_board` | 生成校准板 3MF 与元数据 |
| `build_colordb` | 从校准板照片构建 ColorDB |
| `raster_to_3mf` | 将图像转为多色 3MF 模型 |
| `gen_stage` | 生成建模所需的阶梯色板 |
| `gen_representative_board` | 从配方文件生成代表性校准板 |

#### 构建 Web 前端

```bash
cd web/frontend
npm ci
npm run build
```

如果你在中国大陆公开部署站点，可在构建前通过环境变量配置页脚备案展示（浏览器模式生效，Electron 默认不展示）：

```bash
# 建议写入 web/frontend/.env.production.local（默认不会提交到 Git）
VITE_SITE_ICP_NUMBER=京ICP备12345678号-1
VITE_SITE_ICP_URL=https://beian.miit.gov.cn/
VITE_SITE_PUBLIC_SECURITY_RECORD_NUMBER=京公网安备11010502000001号
VITE_SITE_PUBLIC_SECURITY_RECORD_URL=https://beian.mps.gov.cn/
```

构建产物位于 `web/frontend/dist/`，可通过服务器 `--web` 参数指定该目录以提供静态文件服务。

### Docker 编译

项目提供 `Dockerfile.build` 作为一致的编译环境（Ubuntu 22.04 + g++-13 + CMake + Node.js 22）。

#### 1. 构建编译镜像

```bash
docker build -f Dockerfile.build -t chromaprint3d-build .
```

#### 2. 编译 C++

```bash
docker run --rm -v $(pwd):/src -w /src chromaprint3d-build \
  bash -c "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
```

#### 3. 构建 Web 前端

```bash
docker run --rm -v $(pwd):/src -w /src/web/frontend chromaprint3d-build \
  bash -c "npm ci && npm run build"
```

#### 4. 构建运行时镜像

完成上述编译后，可直接构建精简的运行时镜像：

```bash
docker build -t chromaprint3d .
```

启动容器：

```bash
docker run -d -p 8080:8080 --name chromaprint3d chromaprint3d
```

如需挂载自定义数据或模型包：

```bash
docker run -d -p 8080:8080 \
  -v ./my_data:/app/data \
  -v ./my_model_pack:/app/model_pack \
  --name chromaprint3d chromaprint3d
```

### 快速体验（使用预构建镜像）

如果不需要自行编译，可直接使用发布的 Docker 镜像：

```bash
docker run -d -p 8080:8080 --name chromaprint3d neroued/chromaprint3d:latest
```

访问 `http://localhost:8080` 即可使用。

## 使用

### 启动服务器

```bash
build/bin/chromaprint3d_server \
  --data data \
  --web web/frontend/dist \
  --model-pack data/model_pack/model_package.json \
  --port 8080
```

服务器参数：

| 参数 | 必需 | 默认值 | 说明 |
|---|---|---|---|
| `--data DIR` | 是 | — | 数据根目录（必须可加载至少一个 ColorDB；优先扫描 `<data>/dbs`，不存在时回退扫描 `<data>`） |
| `--port PORT` | 否 | 8080 | HTTP 端口 |
| `--host HOST` | 否 | 0.0.0.0 | 绑定地址 |
| `--web DIR` | 否 | — | 静态文件目录（Web 前端） |
| `--model-pack PATH` | 否 | — | 模型包 JSON 文件 |
| `--max-upload-mb N` | 否 | 50 | 最大上传文件大小（MB） |
| `--http-threads N` | 否 | 4 | Drogon HTTP 工作线程数 |
| `--max-tasks N` | 否 | 4 | 异步任务工作线程数（不影响 HTTP 线程） |
| `--max-queue N` | 否 | 256 | 异步任务队列上限（有界队列） |
| `--task-ttl N` | 否 | 3600 | 任务缓存过期时间（秒） |
| `--session-ttl N` | 否 | 3600 | 会话有效期（秒） |
| `--max-owner-tasks N` | 否 | 32 | 单会话可同时进行的任务上限 |
| `--max-result-mb N` | 否 | 512 | 任务结果总内存上限（MB） |
| `--max-pixels N` | 否 | 16777216 | 单张图片解码像素上限 |
| `--max-session-colordbs N` | 否 | 10 | 单会话可上传 ColorDB 数量上限 |
| `--board-cache-ttl N` | 否 | 600 | 校准板下载缓存过期时间（秒） |
| `--log-level LEVEL` | 否 | info | 日志级别 |
| `--cors-origin URL` | 否 | — | 允许的 CORS 来源（跨域部署时使用） |

并行调优建议：当启用 OpenMP 时，单机总体并行度约为
`--max-tasks × OMP_NUM_THREADS`。若机器出现线程争用，可优先降低 `--max-tasks` 或设置较小的
`OMP_NUM_THREADS`（例如按逻辑核数做均分）。

前端上传预校验阈值可用 `VITE_UPLOAD_MAX_MB` / `VITE_UPLOAD_MAX_PIXELS` 覆盖；
Electron 壳可用 `CHROMAPRINT3D_MAX_UPLOAD_MB` / `CHROMAPRINT3D_MAX_PIXELS` 透传到后端参数。

启动后访问 `http://localhost:8080` 即可使用 Web 界面。
API 前缀为 `/api/v1/*`。
默认 JSON 接口统一返回 `{ok,data}` 或 `{ok,error}`；二进制下载接口直接返回文件内容。
会话 token 识别优先级为：`session` cookie > `X-ChromaPrint3D-Session` header > `?session=` query。
`<data>/recipes/8color_boards.json` 仅用于 8 色校准板接口；缺失不会阻止服务启动，但
`/api/v1/calibration/boards/8color` 会返回 `503`。

### 转换任务分层预览（调试）

`convert/raster` 与 `convert/vector` 任务完成后，`GET /api/v1/tasks/:id` 的
`result.layer_previews` 会返回分层预览摘要：

- `enabled`：是否可用
- `layers`：层数（如 5 / 10）
- `width` / `height`：单层图像尺寸
- `layer_order`：`Top2Bottom` 或 `Bottom2Top`
- `palette`：通道描述（`channel_idx/color/material`）
- `artifacts`：每层 artifact key（如 `layer-preview-0`）

每层 PNG 可通过现有 artifact 路由下载：

`GET /api/v1/tasks/:id/artifacts/layer-preview-{idx}`

### 命令行工具

```bash
# 生成 4 色校准板
build/bin/gen_calibration_board --channels 4 --out board.3mf --meta board.json

# 从校准板照片构建 ColorDB
build/bin/build_colordb --image calib_photo.png --meta board.json --out color_db.json

# 图像转 3MF
build/bin/raster_to_3mf --image input.png --db color_db.json --out output.3mf --preview preview.png

# 栅格图像转 SVG（共享边界向量化）
build/bin/raster_to_svg --image input.png --out output.svg --colors 16 --contour-simplify 0.45
```

`raster_to_svg` 全部参数含义、调参方向与场景建议见：`docs/development.md` 中“`raster_to_svg` 参数详解与调参指南（共享边界向量化）”章节。

### Python 建模管线

建模管线用于拟合颜色预测模型参数，需从项目根目录以模块方式运行：

```bash
# Step 1: 提取单色阶梯数据
python -m modeling.pipeline.step1_extract_stages --help

# Step 2: 拟合 Stage A 参数（E, k）
python -m modeling.pipeline.step2_fit_stage_a --help

# Step 3: 拟合 Stage B 参数（E, k, γ, δ, C₀）
python -m modeling.pipeline.step3_fit_stage_b --help

# Step 4: 选择代表性配方
python -m modeling.pipeline.step4_select_recipes --help

# Step 5: 构建运行时模型包
python -m modeling.pipeline.step5_build_model_package --help

# 生成 8 色校准板配方
python -m modeling.pipeline.gen_8color_board_recipes --help
```

## Web 界面

Web 前端提供四个顶层功能页面：

1. **图像转换** — 上传图像，选择 ColorDB 和参数，转换为多色 3MF 模型（提供跳转到 ColorDB 上传页的入口）
2. **图像预处理工具** — 包含两个子页面：图像抠图、图像矢量化
3. **校准工具** — 包含两个子页面：四色及以下模式、八色模式（Beta），用于生成校准板并构建 ColorDB
4. **上传 ColorDB** — 独立上传已有 ColorDB JSON 到当前会话，避免在校准页面重复操作

> 在“图像转换”中，SVG 输入模式已支持与栅格模式一致的模型门控参数
> （`model_enable` / `model_only` / `model_threshold` / `model_margin`）。
> 这组参数仅在选择 **BambooLab + PLA** 的 ColorDB 时可启用，其他材料/厂商组合会自动禁用。
>
> “图像抠图”现支持后处理参数（阈值、闭运算、孤立区域过滤、描边），
> 并可额外获取 `alpha`、`processed-mask`、`processed-foreground`、`outline` 产物用于预览和下载。
>
> 转换结果面板支持“左侧主图 + 右侧分层预览”并列展示：
> 左侧可继续切换“预览图/颜色源掩码”，右侧使用竖向滑块按层查看 `layer_previews`。
>
> 上传入口会按后端默认能力预校验：仅允许 `JPG/PNG/BMP/TIFF`（转换页额外允许 `SVG`），
> 文件默认上限 `50MB`，位图默认像素上限 `16777216`（约 `4096×4096`）。

开发模式下运行前端：

```bash
cd web/frontend
npm run dev
# 可选质量检查
npm run typecheck
npm run lint
npm run test
```

前端开发服务器默认在 `localhost:5173`，自动代理 `/api` 请求到 `localhost:8080`。
更多前端重构后分层与 Electron 兼容抽象说明见 `web/frontend/README.md`。

### Electron 本地开发（自动拉起后端）

如果希望以桌面壳方式本地开发，可使用 `web/electron`。该模式下 Electron 主进程会自动
启动 `chromaprint3d_server`，无需手动单独开后端。

```bash
# 先在项目根目录构建后端可执行文件
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target chromaprint3d_server -j

# 再启动 Electron 开发模式
cd web/electron
npm install
npm run dev
```

若启动失败（后端二进制缺失、模型路径错误、端口冲突等），可参考 `web/electron/README.md`
中的“常见问题”，以及 `web/frontend/README.md` 的运行时说明。

## 第三方依赖

### 系统依赖（需手动安装）

| 库 | 用途 | 安装方式 |
|---|---|---|
| [OpenCV](https://github.com/opencv/opencv) >= 4.5 | 图像处理（core, imgproc, imgcodecs） | `apt install libopencv-dev` |

OpenMP 为可选依赖，编译时自动检测。启用后可并行加速匹配、网格构建、校准统计与分层预览等核心热点。

### Submodule 依赖（自动管理）

以下依赖通过 git submodules 管理，执行 `git submodule update --init --recursive` 后自动获取：

| 库 | 用途 |
|---|---|
| [lib3mf](https://github.com/3MFConsortium/lib3mf) | 3MF 文件读写 |
| [spdlog](https://github.com/gabime/spdlog) | 结构化日志 |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON 序列化（header-only） |
| [Drogon](https://github.com/drogonframework/drogon) | HTTP 服务器框架 |

### CMake FetchContent 依赖（自动下载）

以下依赖在 CMake 配置阶段自动下载，无需手动安装：

| 库 | 用途 |
|---|---|
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | 深度学习推理后端（CPU / CUDA） |

## 发布

项目使用 GitHub Actions 自动化发布。推送版本标签后自动触发：

```bash
git tag v1.0.0
git push origin v1.0.0
```

CI 会自动完成以下步骤：

1. 并行编译四个平台的 C++ 后端（Linux x86_64、macOS x86_64、macOS arm64、Windows x86_64）及 Web 前端
2. 构建 Docker 镜像并推送至 [Docker Hub](https://hub.docker.com/r/neroued/chromaprint3d) 和 GitHub Container Registry
3. 基于 `electron-builder` 构建 Electron 安装包（Linux / macOS / Windows）
4. 创建 GitHub Release，附带各平台原生二进制压缩包（含 ONNX Runtime 与运行时动态库）、前端静态文件与 Electron 安装包

> **当前限制（桌面端）：** Release 中的 Electron 安装包为未签名内测包，暂不包含代码签名/公证与自动更新能力。

> **手动触发：** 也可在 GitHub Actions → Release → Run workflow 手动触发构建（默认不推送 Docker 镜像，勾选 `push_artifacts` 可同时推送）。

**首次配置：** 需要在仓库 Settings → Secrets and variables → Actions 中添加：

| Secret | 说明 |
|---|---|
| `DOCKERHUB_USERNAME` | Docker Hub 用户名 |
| `DOCKERHUB_TOKEN` | Docker Hub [Access Token](https://hub.docker.com/settings/security) |

GitHub Container Registry 使用仓库自带的 `GITHUB_TOKEN`，无需额外配置。

## 开发路线图

> 以下是核心叠色算法的计划改进方向，按优先级排列。

### P0 — 近期

- [ ] **误差扩散半色调（Error Diffusion Dithering）** — 将经典半色调技术扩展到多层配方空间，通过在相邻像素间扩散 Lab 色差向量，用有限色域模拟更丰富的视觉颜色，消除渐变区域的色阶断裂（banding）。涉及 `core/src/match/match.cpp` 中 `MatchFromRaster` 的扫描线重构和前向模型集成。
- [ ] **CIEDE2000 感知色差精排** — 在颜色匹配的精排阶段用 CIEDE2000（ΔE00）替代 ΔE76 欧氏距离，保持 KD-Tree 粗筛 + Top-K 精排架构不变。需在 `core/include/chromaprint3d/color.h` 新增 `DeltaE2000()` 并修改 `candidate_select.cpp` 评分逻辑。

### P1 — 中期

- [ ] **WebGL/WebGPU 实时叠色预览** — 将 Beer-Lambert 前向模型移植到 Fragment Shader，在浏览器端实时渲染叠色效果预览。用户可交互调整层高、基板颜色、通道选择并即时看到结果，无需等待后端完整处理。
- [ ] **可微前向模型 + 梯度优化配方** — 将 Beer-Lambert 前向模型实现为可微函数（PyTorch / 自动微分），通过梯度下降在连续配方空间中直接优化，突破"只能从预计算候选中选"的限制，找到色域内的最优配方。

## 许可证

[Apache License 2.0](LICENSE)

---

<a id="english"></a>

## English

### Introduction

ChromaPrint3D is a multi-color 3D printing image processing system. It maps input image colors to multi-channel printing recipes and exports 3MF files ready for multi-color 3D printers. The project includes a C++20 core library, command-line tools, an HTTP server, a Vue 3 web frontend, and a Python color modeling pipeline based on Beer-Lambert's law.

### Key Features

- **Color Database (ColorDB)**: Load/save color databases with KD-Tree nearest-neighbor matching in Lab and RGB color spaces
- **Image Preprocessing**: Scaling, denoising, transparency masking
- **Standalone Image Matting**: OpenCV threshold matting and deep learning models (runtime selectable), with dedicated API and web interface
- **Recipe Matching**: ColorDB-based color matching with optional physics-model candidate generation
- **3MF Export**: Convert voxel grids to 3MF multi-color models via lib3mf
- **Calibration Board Generation**: Automatically generate calibration board 3MF and metadata files, supporting 2-8 colors
- **Web Service**: Full HTTP API and Vue 3 frontend interface
- **Python Modeling Pipeline**: Color prediction and parameter fitting based on thin-layer Beer-Lambert stacking model

### Directory Structure

```
ChromaPrint3D/
├── core/               # C++ core library (color matching, voxelization, 3MF export, etc.)
├── infer/              # Deep learning inference engine wrapper (ONNX Runtime backend)
├── apps/               # Command-line tools
├── web/
│   ├── frontend/       # Web frontend (Vue 3 + TypeScript + Naive UI)
│   └── backend/        # HTTP server (C++ Drogon)
├── modeling/           # Python color modeling pipeline
│   ├── core/           #   Core modules (forward model, color space, IO utils)
│   └── pipeline/       #   Pipeline steps (5-step workflow)
├── data/               # Runtime data
│   ├── dbs/            #   ColorDB JSON files
│   ├── model_pack/     #   Model package JSON files
│   ├── models/matting/ #   Matting model files (.onnx) and configs (.json)
│   └── recipes/        #   Pre-computed recipe files (e.g., 8-color calibration)
├── scripts/            # Helper scripts
│   └── download_models.py  # Download ONNX models (cross-platform, Python 3.8+)
├── 3dparty/            # Third-party dependencies (git submodules)
├── Dockerfile          # Runtime container
└── Dockerfile.build    # Build environment container
```

### Getting the Code

```bash
git clone https://github.com/neroued/ChromaPrint3D.git
cd ChromaPrint3D
git submodule update --init --recursive
```

### Building

#### Local Build

**Requirements:**

- CMake >= 3.25
- C++20 compiler (g++-13 or later recommended)
- OpenCV >= 4.5 development libraries
- Node.js >= 22 (for web frontend)
- Python >= 3.10 + NumPy + OpenCV (for modeling pipeline, optional)

**Install dependencies:**

```bash
# Ubuntu / Debian
sudo apt install libopencv-dev

# macOS
brew install opencv
```

**Build C++ core and applications:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

CMake options:

| Option | Default | Description |
|---|---|---|
| `CHROMAPRINT3D_BUILD_APPS` | `ON` | Build executables |
| `CHROMAPRINT3D_BUILD_TESTS` | `OFF` | Build unit tests |
| `CHROMAPRINT3D_BUILD_INFER` | `ON` | Build deep learning inference module (ONNX Runtime auto-downloaded) |

Executables are placed in `build/bin/`:

| Executable | Description |
|---|---|
| `chromaprint3d_server` | HTTP server with Web API |
| `gen_calibration_board` | Generate calibration board 3MF and metadata |
| `build_colordb` | Build ColorDB from calibration board photo |
| `raster_to_3mf` | Convert image to multi-color 3MF model |
| `gen_stage` | Generate stage data for modeling pipeline |
| `gen_representative_board` | Generate representative board from recipe file |

**Build web frontend:**

```bash
cd web/frontend
npm ci
npm run build
```

Output is in `web/frontend/dist/`. Pass it to the server via the `--web` flag to serve static files.

#### Docker Build

The project provides `Dockerfile.build` as a consistent build environment (Ubuntu 22.04 + g++-13 + CMake + Node.js 22).

**1. Build the build image:**

```bash
docker build -f Dockerfile.build -t chromaprint3d-build .
```

**2. Compile C++:**

```bash
docker run --rm -v $(pwd):/src -w /src chromaprint3d-build \
  bash -c "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
```

**3. Build web frontend:**

```bash
docker run --rm -v $(pwd):/src -w /src/web/frontend chromaprint3d-build \
  bash -c "npm ci && npm run build"
```

**4. Build the runtime image:**

After completing the above steps:

```bash
docker build -t chromaprint3d .
```

Start the container:

```bash
docker run -d -p 8080:8080 --name chromaprint3d chromaprint3d
```

To mount custom data or model pack:

```bash
docker run -d -p 8080:8080 \
  -v ./my_data:/app/data \
  -v ./my_model_pack:/app/model_pack \
  --name chromaprint3d chromaprint3d
```

#### Quick Start (Pre-built Image)

If you don't need to build from source, use the pre-built Docker image:

```bash
docker run -d -p 8080:8080 --name chromaprint3d neroued/chromaprint3d:latest
```

Visit `http://localhost:8080` to start using.

### Usage

#### Starting the Server

```bash
build/bin/chromaprint3d_server \
  --data data \
  --web web/frontend/dist \
  --model-pack data/model_pack/model_package.json \
  --port 8080
```

Server options:

| Flag | Required | Default | Description |
|---|---|---|---|
| `--data DIR` | Yes | — | Data root directory (must load at least one ColorDB; scans `<data>/dbs` first, then falls back to `<data>`) |
| `--port PORT` | No | 8080 | HTTP port |
| `--host HOST` | No | 0.0.0.0 | Bind address |
| `--web DIR` | No | — | Static files directory (web frontend) |
| `--model-pack PATH` | No | — | Model package JSON file |
| `--max-upload-mb N` | No | 50 | Max upload size in MB |
| `--http-threads N` | No | 4 | Drogon HTTP worker threads |
| `--max-tasks N` | No | 4 | Async task worker threads (independent from HTTP threads) |
| `--max-queue N` | No | 256 | Max async task queue size |
| `--task-ttl N` | No | 3600 | Task cache TTL in seconds |
| `--session-ttl N` | No | 3600 | Session TTL in seconds |
| `--max-owner-tasks N` | No | 32 | Max in-flight tasks per session |
| `--max-result-mb N` | No | 512 | Max total in-memory task result size (MB) |
| `--max-pixels N` | No | 16777216 | Max decoded pixels per image |
| `--max-session-colordbs N` | No | 10 | Max uploaded ColorDB count per session |
| `--board-cache-ttl N` | No | 600 | Calibration board download cache TTL in seconds |
| `--log-level LEVEL` | No | info | Log level |
| `--cors-origin URL` | No | — | Allowed CORS origin for cross-origin deployment |

Parallel tuning tip: with OpenMP enabled, effective machine-level parallelism is roughly
`--max-tasks × OMP_NUM_THREADS`. If you observe thread oversubscription, lower `--max-tasks` first
or set a smaller `OMP_NUM_THREADS` value (for example, divide logical cores across workers).

Frontend upload pre-validation limits can be overridden via `VITE_UPLOAD_MAX_MB` /
`VITE_UPLOAD_MAX_PIXELS`. Electron can pass through backend limits via
`CHROMAPRINT3D_MAX_UPLOAD_MB` / `CHROMAPRINT3D_MAX_PIXELS`.

Visit `http://localhost:8080` to use the web interface.
API prefix is `/api/v1/*`.
JSON routes use `{ok,data}` / `{ok,error}` envelopes; binary download routes return file bytes directly.
Session token lookup order is: `session` cookie > `X-ChromaPrint3D-Session` header > `?session=` query.
`<data>/recipes/8color_boards.json` is only required by 8-color board endpoints; missing it does not
block server startup, but `/api/v1/calibration/boards/8color` returns `503`.

### Layered Preview for Convert Tasks

For completed `convert/raster` and `convert/vector` tasks, `GET /api/v1/tasks/:id` now includes
`result.layer_previews` summary fields:

- `enabled`
- `layers`
- `width` / `height`
- `layer_order` (`Top2Bottom` or `Bottom2Top`)
- `palette` (`channel_idx/color/material`)
- `artifacts` (per-layer keys such as `layer-preview-0`)

Download each layer PNG via the existing artifact route:

`GET /api/v1/tasks/:id/artifacts/layer-preview-{idx}`

#### Command-Line Tools

```bash
# Generate a 4-color calibration board
build/bin/gen_calibration_board --channels 4 --out board.3mf --meta board.json

# Build ColorDB from a calibration board photo
build/bin/build_colordb --image calib_photo.png --meta board.json --out color_db.json

# Convert image to 3MF
build/bin/raster_to_3mf --image input.png --db color_db.json --out output.3mf --preview preview.png

# Raster image to SVG (shared-boundary vectorization)
build/bin/raster_to_svg --image input.png --out output.svg --colors 16 --contour-simplify 0.45
```

#### Python Modeling Pipeline

The modeling pipeline fits color prediction model parameters. Run from the project root as modules:

```bash
# Step 1: Extract single-color stage data
python -m modeling.pipeline.step1_extract_stages --help

# Step 2: Fit Stage A parameters (E, k)
python -m modeling.pipeline.step2_fit_stage_a --help

# Step 3: Fit Stage B parameters (E, k, γ, δ, C₀)
python -m modeling.pipeline.step3_fit_stage_b --help

# Step 4: Select representative recipes
python -m modeling.pipeline.step4_select_recipes --help

# Step 5: Build runtime model package
python -m modeling.pipeline.step5_build_model_package --help

# Generate 8-color calibration board recipes
python -m modeling.pipeline.gen_8color_board_recipes --help
```

### Web Interface

The web frontend now has four top-level tabs:

1. **Image Conversion** — Upload an image, select ColorDBs and parameters, and convert to multi-color 3MF (includes a shortcut to the ColorDB upload page)
2. **Image Preprocessing Tools** — Two nested pages: Image Matting and Image Vectorization
3. **Calibration Tools** — Two nested pages: Up to 4-color mode and 8-color mode (Beta), for calibration board generation and ColorDB building
4. **Upload ColorDB** — Standalone upload page for existing ColorDB JSON files in the current session

> In **Image Conversion**, SVG input now supports the same model-gate parameters as raster
> (`model_enable` / `model_only` / `model_threshold` / `model_margin`).
> These options are enabled only when **BambooLab + PLA** ColorDBs are selected; they are
> automatically disabled for other material/vendor combinations.
>
> The conversion result panel now shows the main image on the left and layer preview on the right:
> the left side can still switch between Preview/Source Mask, while the right side uses a vertical
> slider to inspect each `layer_previews` layer.
>
> Upload inputs now perform frontend pre-validation against backend default limits:
> only `JPG/PNG/BMP/TIFF` are accepted (plus `SVG` on the conversion page),
> with a default file-size limit of `50MB` and raster pixel limit of `16777216` (~`4096×4096`).

For frontend development:

```bash
cd web/frontend
npm run dev
# Optional quality checks
npm run typecheck
npm run lint
npm run test
```

The dev server runs at `localhost:5173` and proxies `/api` requests to `localhost:8080`.
For frontend layering and Electron-ready runtime abstraction details, see `web/frontend/README.md`.

### Third-Party Dependencies

#### System Dependencies (manual installation required)

| Library | Purpose | Installation |
|---|---|---|
| [OpenCV](https://github.com/opencv/opencv) >= 4.5 | Image processing (core, imgproc, imgcodecs) | `apt install libopencv-dev` |

OpenMP is an optional dependency, auto-detected at build time. When enabled, it accelerates core
hotspots including matching, mesh construction, calibration statistics, and layer preview rendering.

#### Submodule Dependencies (automatically managed)

The following are managed via git submodules and fetched by `git submodule update --init --recursive`:

| Library | Purpose |
|---|---|
| [lib3mf](https://github.com/3MFConsortium/lib3mf) | 3MF file read/write |
| [spdlog](https://github.com/gabime/spdlog) | Structured logging |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON serialization (header-only) |
| [Drogon](https://github.com/drogonframework/drogon) | HTTP server framework |

#### CMake FetchContent Dependencies (auto-downloaded)

The following are automatically downloaded during CMake configuration:

| Library | Purpose |
|---|---|
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | Deep learning inference backend (CPU / CUDA) |

### Releasing

The project uses GitHub Actions for automated releases. Push a version tag to trigger:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The CI pipeline will automatically:

1. Build the C++ backend in parallel for four platforms (Linux x86_64, macOS x86_64, macOS arm64, Windows x86_64) and the web frontend
2. Build and push the Docker image to [Docker Hub](https://hub.docker.com/r/neroued/chromaprint3d) and GitHub Container Registry
3. Build Electron installers (Linux / macOS / Windows) using `electron-builder`
4. Create a GitHub Release with per-platform native backend archives (including ONNX Runtime and runtime shared libraries), the frontend static bundle, and Electron installers

> **Current desktop limitation:** Electron release artifacts are unsigned internal builds; code signing/notarization and auto-update are not included yet.

> **Manual trigger:** You can also trigger a build manually via GitHub Actions → Release → Run workflow (Docker push is skipped by default; check `push_artifacts` to also push).

**First-time setup:** Add the following secrets in Settings → Secrets and variables → Actions:

| Secret | Description |
|---|---|
| `DOCKERHUB_USERNAME` | Docker Hub username |
| `DOCKERHUB_TOKEN` | Docker Hub [Access Token](https://hub.docker.com/settings/security) |

GitHub Container Registry uses the built-in `GITHUB_TOKEN` — no extra setup needed.

### Roadmap

> Planned improvements to the core color-stacking algorithms, ordered by priority.

#### P0 — Near-term

- [ ] **Error Diffusion Dithering** — Extend classic halftoning to the multi-layer recipe space by diffusing Lab color-error vectors to neighboring pixels, enabling richer perceived colors from a limited gamut and eliminating banding in gradient regions. Involves refactoring the scanline loop in `MatchFromRaster` (`core/src/match/match.cpp`) and integrating the forward model for per-candidate color prediction.
- [ ] **CIEDE2000 Perceptual Reranking** — Replace ΔE76 Euclidean distance with CIEDE2000 (ΔE00) in the reranking stage of color matching, while keeping the KD-Tree coarse search + Top-K reranking architecture. Requires adding `DeltaE2000()` to `core/include/chromaprint3d/color.h` and updating the scoring logic in `candidate_select.cpp`.

#### P1 — Mid-term

- [ ] **WebGL/WebGPU Real-time Color Preview** — Port the Beer-Lambert forward model to a Fragment Shader for real-time browser-side rendering of color-stacking previews. Users can interactively adjust layer height, substrate color, and channel selection with instant visual feedback.
- [ ] **Differentiable Forward Model + Gradient-based Recipe Optimization** — Implement the Beer-Lambert forward model as a differentiable function (PyTorch / autodiff) to optimize recipes via gradient descent in continuous recipe space, breaking through the limitation of selecting only from pre-computed candidates.

### License

[Apache License 2.0](LICENSE)
