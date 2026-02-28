# ChromaPrint3D 本地开发指南

本文档讲解如何在本地搭建 ChromaPrint3D 的开发环境，涵盖**本地编译**和**Docker 编译**两种方式。

## 项目结构概览

```
ChromaPrint3D/
├── core/                  # C++ 核心库（颜色匹配、图像处理、3MF 导出）
│   ├── include/           # 公共头文件
│   ├── src/               # 源文件
│   └── tests/             # 单元测试（GoogleTest）
├── infer/                 # 深度学习推理引擎封装（ONNX Runtime 后端）
│   ├── include/           # 公共头文件
│   ├── src/               # 源文件
│   └── tests/             # 推理模块单元测试
├── apps/                  # 命令行工具
├── web/
│   ├── frontend/          # Vue 3 前端
│   │   └── src/
│   └── backend/           # HTTP 服务器（C++ Drogon）
│       ├── src/           # 四层结构（config/infrastructure/application/presentation）
│       └── server_main.cpp
├── 3dparty/               # 第三方依赖（git submodules）
│   ├── opencv/            # OpenCV（core/imgproc/imgcodecs）
│   ├── lib3mf/            # 3MF 文件读写
│   └── spdlog/            # 日志库
├── data/                  # 运行时数据
│   ├── dbs/               # 颜色数据库（JSON）
│   ├── recipes/           # 预计算配方（JSON）
│   ├── models/matting/    # 抠图模型文件（.onnx）与配置（.json）
│   └── model_pack/        # 模型包（JSON，~21MB）
├── Dockerfile             # 运行时镜像
└── Dockerfile.build       # 编译环境镜像
```

## 方式一：本地编译

适合需要频繁修改代码、使用 IDE 调试、有较好开发环境的场景。

### 1.1 环境要求

| 依赖 | 最低版本 | 说明 |
|------|---------|------|
| CMake | 3.25 | 构建系统 |
| GCC | 13 | 需要 C++20 支持 |
| Node.js | 22 | 前端构建 |
| Git | 任意 | 子模块管理 |

> ONNX Runtime 由 CMake FetchContent 在配置阶段自动下载，无需手动安装。如需禁用推理模块，添加 `-DCHROMAPRINT3D_BUILD_INFER=OFF`。

**加速构建**：第三方库 lib3mf 体积较大，首次全量编译耗时较长。建议安装 ccache（`apt install ccache`），CMake 会优先使用，重复/增量编译会显著加快。CI Release 流程已对 `build/` 与 ccache 做缓存，后续发布构建会复用已编译的 lib3mf。

**Ubuntu 24.04 一键安装：**

```bash
sudo apt update
sudo apt install -y build-essential g++-13 cmake ninja-build git pkg-config
```

Node.js 推荐通过 [nvm](https://github.com/nvm-sh/nvm) 或 NodeSource 安装：

```bash
curl -fsSL https://deb.nodesource.com/setup_22.x | sudo bash -
sudo apt install -y nodejs
```

### 1.2 获取源码

```bash
git clone https://github.com/neroued/ChromaPrint3D.git
cd ChromaPrint3D
git submodule update --init --recursive
```

### 1.3 下载模型文件

抠图所需的 ONNX 模型托管在 HuggingFace Hub（[neroued/ChromaPrint3D-models](https://huggingface.co/neroued/ChromaPrint3D-models)），需要在编译前下载到 `data/models/` 目录：

```bash
./scripts/download_models.sh
```

脚本会根据 `data/models/models.json` 清单自动下载并校验 SHA256。已存在且校验通过的文件会被跳过。

> 如果不需要抠图功能，可以跳过此步骤，或在 CMake 中添加 `-DCHROMAPRINT3D_BUILD_INFER=OFF` 禁用推理模块。

### 1.4 编译 C++ 后端

```bash
# Release 模式（推荐日常开发，编译优化后运行速度快）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13

# 或 Debug 模式（支持调试器断点、地址检测等）
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13

# 编译（自动利用所有 CPU 核心）
cmake --build build -j$(nproc)
```

编译完成后，可执行文件在 `build/bin/` 目录下：

```
build/bin/
├── chromaprint3d_server       # HTTP 服务器
├── raster_to_3mf               # 命令行转换工具
├── gen_calibration_board      # 校准板生成
├── build_colordb              # 颜色数据库构建
├── gen_stage                  # 阶梯数据生成
└── gen_representative_board   # 代表性校准板生成
```

**CMake 构建选项：**

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `CHROMAPRINT3D_BUILD_APPS` | `ON` | 构建可执行文件 |
| `CHROMAPRINT3D_BUILD_TESTS` | `OFF` | 构建单元测试 |
| `CHROMAPRINT3D_BUILD_INFER` | `ON` | 构建深度学习推理模块（ONNX Runtime 自动下载） |

**启用单元测试：**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13 \
  -DCHROMAPRINT3D_BUILD_TESTS=ON

cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

### 1.5 安装前端依赖

```bash
cd web/frontend
npm ci
cd ../..
```

### 1.6 启动开发环境

开发时需要**同时运行后端服务和前端开发服务器**，打开两个终端：

**终端 1 — 启动后端：**

```bash
./build/bin/chromaprint3d_server \
  --data ./data \
  --model-pack ./data/model_pack/model_package.json \
  --port 8080 \
  --http-threads 4 \
  --max-tasks 4 \
  --log-level debug
```

> 注意：开发模式不传 `--web` 参数，静态文件由 Vite 开发服务器提供。
> `--http-threads` 仅控制 Drogon HTTP 线程，`--max-tasks` 仅控制异步任务执行线程（两者语义已拆分）。
> 抠图模型自动从 `data/models/matting/` 目录加载（需有对应的 `.onnx` 模型文件和 `.json` 配置），在 Web 界面"图像抠图"标签页中选择使用。

**终端 2 — 启动前端开发服务器：**

```bash
cd web/frontend
npm run dev
```

打开浏览器访问 **http://localhost:5173**。

Vite 开发服务器的工作方式：
- 前端页面由 Vite 在 5173 端口提供，支持**热更新**（修改 `.vue`/`.ts` 文件后自动刷新）
- 所有 `/api/*`（当前主要为 `/api/v1/*`）请求通过 `vite.config.ts` 中的 proxy 配置**自动代理**到 `http://localhost:8080`（后端）
- 无需配置 CORS，前后端在开发时等效于同源

```
浏览器 → localhost:5173（Vite）
                ├── 页面/JS/CSS → Vite 直接返回（热更新）
                └── /api/*（含 /api/v1/*） → 代理到 localhost:8080（C++ 后端）
```

### 1.7 日常开发流程

**修改 C++ 代码后：**

```bash
# 增量编译（只重新编译修改的文件，通常几秒完成）
cmake --build build -j$(nproc)

# 重启后端（Ctrl+C 停止旧进程后重新启动）
./build/bin/chromaprint3d_server --data ./data --model-pack ./data/model_pack/model_package.json --port 8080
```

**修改前端代码后：**

无需任何操作，Vite 热更新会自动生效。

**同时修改前后端：**

先增量编译并重启后端，前端自动生效。

### 1.8 构建生产版本

```bash
# 编译 C++（Release）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13
cmake --build build -j$(nproc)

# 构建前端
cd web/frontend
npm run build    # 产物在 web/frontend/dist/
cd ../..

# 单机运行（前后端同进程）
./build/bin/chromaprint3d_server \
  --data ./data \
  --web ./web/frontend/dist \
  --model-pack ./data/model_pack/model_package.json \
  --port 8080
```

访问 http://localhost:8080 即可使用完整功能。

---

## 方式二：Docker 编译

适合没有本地开发环境、环境不兼容、或需要确保构建产物一致性的场景。项目提供了专用的编译环境镜像 `Dockerfile.build`。

### 2.1 环境要求

仅需安装 Docker（20.10+）。

### 2.2 构建编译环境镜像

```bash
docker build -f Dockerfile.build -t chromaprint3d-build .
```

此镜像包含 g++-13、CMake、Ninja、Node.js 22 等完整工具链，约 1.5GB。**只需构建一次。**

### 2.3 编译 C++ 后端

```bash
docker run --rm -v $(pwd):/src -w /src chromaprint3d-build \
  bash -c "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13 \
    && cmake --build build -j\$(nproc)"
```

编译产物写回宿主机的 `build/` 目录（通过 volume mount）。

### 2.4 构建前端

```bash
docker run --rm -v $(pwd):/src -w /src/web/frontend chromaprint3d-build \
  bash -c "npm ci && npm run build"
```

构建产物写回 `web/frontend/dist/`。

### 2.5 下载模型文件

与本地编译相同，需要下载抠图模型（Docker 编译环境镜像中已包含 curl 和 python3）：

```bash
./scripts/download_models.sh
```

### 2.6 构建运行时 Docker 镜像

编译完成后，打包为轻量运行时镜像：

```bash
docker build -t chromaprint3d .
```

此镜像仅包含运行时依赖（约 80MB），不含编译工具链。

### 2.7 运行

```bash
docker run --rm -p 8080:8080 chromaprint3d
```

访问 http://localhost:8080。

### 2.8 Docker 环境下的开发调试

用 Docker 编译不代表必须在容器内开发。推荐的混合工作流：

**前端开发（宿主机 Vite + 容器后端）：**

```bash
# 终端 1：用 Docker 运行后端
docker run --rm -p 8080:8080 chromaprint3d

# 终端 2：宿主机运行前端开发服务器（需要本地 Node.js）
cd web/frontend
npm run dev
```

**C++ 开发（容器内编译 + 宿主机运行）：**

```bash
# 增量编译（容器内执行，产物在宿主机 build/ 目录）
docker run --rm -v $(pwd):/src -w /src chromaprint3d-build \
  bash -c "cmake --build build -j\$(nproc)"

# 宿主机直接运行（如果平台兼容，即宿主机也是 Ubuntu/Linux x86_64）
./build/bin/chromaprint3d_server --data ./data --port 8080
```

**完全容器化开发（交互模式）：**

```bash
docker run --rm -it -v $(pwd):/src -w /src -p 8080:8080 chromaprint3d-build bash
```

进入容器后，可以自由执行 cmake、npm 等命令，端口映射到宿主机。

---

## 常用命令速查

| 操作 | 命令 |
|------|------|
| 克隆并初始化子模块 | `git clone --recurse-submodules <url>` |
| 下载抠图模型 | `./scripts/download_models.sh` |
| 编译后端（Release） | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13 && cmake --build build -j$(nproc)` |
| 增量编译 | `cmake --build build -j$(nproc)` |
| 启动后端 | `./build/bin/chromaprint3d_server --data ./data --model-pack ./data/model_pack/model_package.json --port 8080` |
| 栅格图转 SVG（Potrace 管线） | `./build/bin/raster_to_svg --image input.png --out output.svg --colors 16 --contour-simplify 0.45` |
| 安装前端依赖 | `cd web/frontend && npm ci` |
| 启动前端开发服务器 | `cd web/frontend && npm run dev` |
| 构建前端生产版本 | `cd web/frontend && npm run build` |
| 运行测试 | `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure` |
| Docker 编译 C++ | `docker run --rm -v $(pwd):/src -w /src chromaprint3d-build bash -c "cmake --build build -j\$(nproc)"` |
| Docker 构建镜像 | `docker build -t chromaprint3d .` |
| Docker 运行 | `docker run --rm -p 8080:8080 chromaprint3d` |

### `raster_to_svg` 参数详解与调参指南（Potrace 管线）

`raster_to_svg`（CLI）与 `/api/v1/vectorize/tasks`（Web/API）共用同一套 `VectorizerConfig`。  
该链路依赖 **Potrace（必选）**，构建环境需安装 `libpotrace-dev`。  
建议按“**先保覆盖 -> 再保细节 -> 最后简化**”的顺序调参。

#### CLI / API 参数映射

| CLI 参数 | API / 前端字段 | 默认值 | 备注 |
|---|---|---:|---|
| `--colors N` | `num_colors` | `16` | 量化颜色数量 |
| `--min-region N` | `min_region_area` | `10` | 最小区域面积（像素²） |
| `--min-contour F` | `min_contour_area` | `10` | 最小轮廓面积（像素²） |
| `--min-hole-area F` | `min_hole_area` | `4.0` | 最小保留孔洞面积（像素²） |
| `--contour-simplify F` | `contour_simplify` | `0.45` | 轮廓简化强度（越大节点越少） |
| `--topology-cleanup F` | `topology_cleanup` | `0.15` | 拓扑清理简化强度 |
| `--disable-coverage-fix` | `enable_coverage_fix=false` | `true` | 关闭覆盖修复 |
| `--min-coverage-ratio F` | `min_coverage_ratio` | `0.998` | 覆盖率低于该值触发修复 |
| `--no-svg-stroke` | `svg_enable_stroke=false` | `true` | 关闭 SVG 调试描边 |
| `--svg-stroke-w F` | `svg_stroke_width` | `0.5` | 描边宽度（像素） |

#### 参数分组建议

- **基础参数（优先调整）**
  - `num_colors`：颜色层次与体积的主旋钮。
  - `contour_simplify`：控制节点数量与边界贴合度。
  - `topology_cleanup`：控制拓扑修复后的几何简化力度。

- **覆盖与细节保护**
  - `min_contour_area`：过小会噪点多，过大易漏小结构。
  - `min_hole_area`：控制小孔洞是否保留。
  - `enable_coverage_fix` + `min_coverage_ratio`：建议质量优先场景保持开启。

- **区域清理**
  - `min_region_area`：抑制小碎块的主要参数；增大可去噪，减小可保细节。

- **调试显示**
  - `svg_enable_stroke` / `svg_stroke_width`：仅用于观测边界，正式导出通常关闭描边。

#### 推荐调参流程（实战）

1. **先保覆盖**：开启 `enable_coverage_fix`，`min_coverage_ratio` 设为 `0.995~0.999`。  
2. **再保细节**：若细线或孔洞缺失，先降低 `min_contour_area` / `min_hole_area`。  
3. **最后控体积**：逐步增大 `contour_simplify`，再小步调 `topology_cleanup`。  
4. **一次只改 1~2 个参数**，保留对比 SVG，便于定位收益与副作用。

## 抠图功能架构

### 异步任务模式

抠图 API 采用与 convert 相同的异步任务模式（统一到 `/api/v1` 契约）：

1. `POST /api/v1/matting/tasks` — 提交图片和抠图方法，立即返回 `202 Accepted` + `task_id`
2. `GET /api/v1/tasks/:id` — 轮询任务状态（pending / running / completed / failed），完成后包含耗时信息
3. `GET /api/v1/tasks/:id/artifacts/foreground` — 下载抠图前景 PNG（仅 completed 状态可用）
4. `GET /api/v1/tasks/:id/artifacts/mask` — 下载抠图 mask PNG

后端通过固定线程池 + 有界队列管理任务提交、并发控制与生命周期。推理任务在独立工作线程中执行，不阻塞 HTTP 线程；队列超限会直接拒绝（429）。

### 模型加载与切换

所有抠图模型（OpenCV 阈值算法 + 深度学习模型）在服务启动时一次性加载到 `MattingRegistry` 中。切换模型只需在请求中指定不同的 `method` 参数，从 registry 中获取对应的 provider，无需重新加载模型。

ONNX Runtime 的 `Session::Run()` 是线程安全的，同一模型可以并发处理多个请求。

### 耗时日志

每次推理完成后，后端会输出分阶段耗时日志：

```
DLMatting 'u2netp': preprocess=5.2ms, inference=1234.5ms, postprocess=8.3ms, total=1248.0ms
Matting task a1b2c3d4: decode=12.1ms, provider=1248.0ms, encode=45.6ms, total=1305.7ms
```

前端完成后也会展示耗时信息（解码、预处理、推理、后处理、编码、总计）。

## 常见问题

### 编译报错 "CMake 3.25 or higher is required"

Ubuntu 22.04 自带的 CMake 版本较低。通过 Kitware APT 源安装最新版：

```bash
sudo apt install -y gpg wget
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc | sudo gpg --dearmor -o /usr/share/keyrings/kitware-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main" | sudo tee /etc/apt/sources.list.d/kitware.list
sudo apt update && sudo apt install -y cmake
```

或者直接使用 Docker 编译环境（已内置 CMake 3.28+）。

### 编译报错 C++20 相关

确保使用 g++-13 或更高版本。CMake 命令中明确指定编译器：

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13
```

### 子模块为空目录

```bash
git submodule update --init --recursive
```

### 前端 `npm run dev` 后 API 请求失败

确保 C++ 后端已在 8080 端口运行。Vite 的 proxy 配置将 `/api` 代理到 `http://localhost:8080`，后端不运行则代理失败。

### Docker 编译后宿主机无法直接运行二进制文件

Docker 编译环境为 Ubuntu 24.04 x86_64。如果宿主机系统不同（如 macOS、ARM Linux），编译产物无法直接运行，需通过 Docker 容器执行，或在本地搭建原生编译环境。

## 模型管理

抠图模型文件（`.onnx`）托管在 [HuggingFace Hub](https://huggingface.co/neroued/ChromaPrint3D-models)，不随 Git 仓库分发。模型的元信息（名称、SHA256、下载地址）记录在 `data/models/models.json` 清单文件中。

### 下载模型

```bash
./scripts/download_models.sh
```

脚本自动从 HuggingFace 下载所有模型到 `data/models/` 并校验完整性。已存在且校验通过的文件会跳过。也可以通过 `--target-dir` 指定输出目录。

### 上传 / 更新模型

当需要添加新模型或更新现有模型时：

1. 将新的 `.onnx` 文件放入 `data/models/` 对应目录
2. 在 `data/models/models.json` 中添加对应条目（`sha256` 字段可留空）
3. 运行上传脚本：

```bash
pip install huggingface_hub
huggingface-cli login
python scripts/upload_models.py --create-repo
```

脚本会自动上传文件到 HuggingFace 并回写 SHA256 到 `models.json`。上传完成后，将更新后的 `models.json` 提交到 Git 仓库。
