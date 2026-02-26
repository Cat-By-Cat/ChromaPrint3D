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
├── apps/                  # 可执行文件
│   ├── server/            # HTTP 服务器相关头文件
│   └── chromaprint3d_server.cpp
├── web/                   # Vue 3 前端
│   └── src/
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
cd web
npm ci
cd ..
```

### 1.6 启动开发环境

开发时需要**同时运行后端服务和前端开发服务器**，打开两个终端：

**终端 1 — 启动后端：**

```bash
./build/bin/chromaprint3d_server \
  --data ./data \
  --model-pack ./data/model_pack/model_package.json \
  --port 8080 \
  --log-level debug
```

> 注意：开发模式不传 `--web` 参数，静态文件由 Vite 开发服务器提供。
> 抠图模型自动从 `data/models/matting/` 目录加载（需有对应的 `.onnx` 模型文件和 `.json` 配置），在 Web 界面"图像抠图"标签页中选择使用。

**终端 2 — 启动前端开发服务器：**

```bash
cd web
npm run dev
```

打开浏览器访问 **http://localhost:5173**。

Vite 开发服务器的工作方式：
- 前端页面由 Vite 在 5173 端口提供，支持**热更新**（修改 `.vue`/`.ts` 文件后自动刷新）
- 所有 `/api/*` 请求通过 `vite.config.ts` 中的 proxy 配置**自动代理**到 `http://localhost:8080`（后端）
- 无需配置 CORS，前后端在开发时等效于同源

```
浏览器 → localhost:5173（Vite）
                ├── 页面/JS/CSS → Vite 直接返回（热更新）
                └── /api/* → 代理到 localhost:8080（C++ 后端）
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
cd web
npm run build    # 产物在 web/dist/
cd ..

# 单机运行（前后端同进程）
./build/bin/chromaprint3d_server \
  --data ./data \
  --web ./web/dist \
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
docker run --rm -v $(pwd):/src -w /src/web chromaprint3d-build \
  bash -c "npm ci && npm run build"
```

构建产物写回 `web/dist/`。

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
cd web
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
| 栅格图转 SVG（共享边界向量化） | `./build/bin/raster_to_svg --image input.png --out output.svg --colors 16 --min-boundary 2` |
| 安装前端依赖 | `cd web && npm ci` |
| 启动前端开发服务器 | `cd web && npm run dev` |
| 构建前端生产版本 | `cd web && npm run build` |
| 运行测试 | `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure` |
| Docker 编译 C++ | `docker run --rm -v $(pwd):/src -w /src chromaprint3d-build bash -c "cmake --build build -j\$(nproc)"` |
| Docker 构建镜像 | `docker build -t chromaprint3d .` |
| Docker 运行 | `docker run --rm -p 8080:8080 chromaprint3d` |

### `raster_to_svg` 参数详解与调参指南（共享边界向量化）

`raster_to_svg`（CLI）与 `/api/vectorize`（Web/API）共用同一套 `VectorizerConfig`。  
建议按“**先覆盖完整 -> 再保细节 -> 最后做简化**”的顺序调参，避免一次改太多参数导致难以定位问题。

#### CLI / API 参数映射

| CLI 参数 | API / 前端字段 | 默认值 | 备注 |
|---|---|---:|---|
| `--colors N` | `num_colors` | `16` | 量化颜色数量 |
| `--merge-lambda F` | `merge_lambda` | `25` | 区域合并阈值（Lab 距离平方） |
| `--min-region N` | `min_region_area` | `10` | 最小区域面积（像素²） |
| `--morph-kernel N` | `morph_kernel_size` | `3` | 标签平滑（中值滤波核） |
| `--min-contour F` | `min_contour_area` | `10` | 最小轮廓面积（像素²） |
| `--min-boundary F` | `min_boundary_perimeter` | `2` | 最小边界长度（像素） |
| `--alpha-max F` | `alpha_max` | `1.0` | 角点/平滑倾向控制 |
| `--opt-tolerance F` | `opt_tolerance` | `0.2` | 曲线优化容差 |
| `--no-curve-opt` | `enable_curve_opt=false` | `true` | 关闭曲线优化阶段 |
| `--curve-tolerance F` | `curve_tolerance` | `2.0` | 备用曲线拟合误差容差 |
| `--corner-thresh F` | `corner_threshold` | `135` | 备用拟合角点阈值（度） |
| `--svg-stroke` | `svg_enable_stroke` | `false` | SVG 结果是否加描边 |
| `--svg-stroke-w F` | `svg_stroke_width` | `0.5` | 描边宽度（像素） |
| （CLI 暂未暴露） | `color_space` | `lab` | API 可选 `lab` / `rgb` |

#### 参数含义与调整方法

**1) 颜色分割与区域合并**

- `num_colors`（`--colors`）
  - 含义：初始颜色聚类数量。
  - 调大：保留更多颜色层次与细节，但区域数量增多、SVG 更复杂。
  - 调小：图像更“块状化”，文件更小，但容易丢失细节和细线色阶。
  - 调法：先按 2 倍步长粗调（`8 -> 16 -> 32`），再小步微调（±4）。

- `merge_lambda`（`--merge-lambda`）
  - 含义：相邻区域的合并阈值（Lab 距离平方）；越大越容易合并。
  - 调大：可减少碎片，但会吞掉细小对比结构（尤其深色细线）。
  - 调小：边界更保守，细节保留更好，但噪声和小碎块会增多。
  - 调法：围绕默认值分档测试（如 `15 / 25 / 40`），优先看细线连续性与噪声。

- `min_region_area`（`--min-region`）
  - 含义：小于该面积的区域会被并入邻域。
  - 调大：去噪更强，但会丢小元素/细线端点。
  - 调小：保细节更好，但噪点和锯齿碎片可能变多。
  - 调法：图标/线稿一般 `2~12`；照片类可到 `10~40`。

- `color_space`（API 字段）
  - 含义：颜色距离度量空间。
  - `lab`：更符合人眼感知，通常是首选。
  - `rgb`：在某些高饱和边缘图可能更“硬”，可用于对比试验。
  - 调法：先固定其他参数，仅切换 `lab/rgb` 对比边界稳定性与覆盖率。

**2) 边界提取与小轮廓过滤**

- `morph_kernel_size`（`--morph-kernel`）
  - 含义：标签图平滑核大小（`0` 为关闭）。
  - 调大：可抑制毛刺，但可能抹平细线或窄缝。
  - 调小：保留原始细节，但可能有噪声边界。
  - 调法：优先用奇数（`0/3/5`），细线断裂时先降到 `0~3`。

- `min_contour_area`（`--min-contour`）
  - 含义：小于该面积的轮廓直接丢弃。
  - 调大：减少微小孤岛与噪点，可能导致“缺块”。
  - 调小：覆盖更完整，文件体积可能上升。
  - 调法：出现空洞/漏填时先下调（如 `10 -> 5 -> 2`）。

- `min_boundary_perimeter`（`--min-boundary`）
  - 含义：小于该周长的边界环丢弃。
  - 调大：有助去除短小毛刺，但细线和窄条最易受影响。
  - 调小：能保住细线连续性，噪声轮廓可能增多。
  - 调法：黑色细线断裂时优先调小到 `0~2`。

**3) 曲线拟合与优化**

- `alpha_max`（`--alpha-max`）
  - 含义：角点判定阈值；值越大越倾向平滑，越小越保角。
  - 调大：曲线更圆润，锐角可能被磨平。
  - 调小：角点更硬朗，路径节点可能增多。
  - 调法：几何图形建议 `0.7~1.0`；自然图像可尝试 `1.0~1.3`。

- `opt_tolerance`（`--opt-tolerance`）
  - 含义：曲线优化容差（段合并/简化尺度）。
  - 调大：路径更简洁、节点更少，但偏离原边界风险上升。
  - 调小：轮廓更贴合原图，体积和复杂度增加。
  - 调法：先固定在 `0.1~0.3`；若边缘“缩水”则减小。

- `enable_curve_opt`（`--no-curve-opt` 的反向开关）
  - 含义：是否启用曲线优化阶段。
  - 关闭后：形状更接近原始折线，通常更稳定但不够平滑。
  - 调法：定位问题时建议先关闭做 A/B 对比，再决定是否开启。

- `curve_tolerance`（`--curve-tolerance`）
  - 含义：备用拟合路径允许的最大误差。
  - 调大：拟合更松，曲线段更少但偏差可能更大。
  - 调小：拟合更紧，形状更忠实，节点数量上升。
  - 调法：目标是保细节时取 `0.8~2.0`，追求简化可升到 `2~4`。

- `corner_threshold`（`--corner-thresh`）
  - 含义：备用拟合的角点阈值（度）。
  - 调大：更容易识别为“角”，折线感更强。
  - 调小：更倾向平滑连接。
  - 调法：Logo/字体可试 `120~150`；自然图可试 `90~130`。

**4) SVG 输出观察参数（调试用）**

- `svg_enable_stroke`（`--svg-stroke`）
  - 含义：是否给每个形状加描边。
  - 用途：便于观察边界闭合、重叠关系和断线问题；正式输出通常关闭。

- `svg_stroke_width`（`--svg-stroke-w`）
  - 含义：描边宽度（像素）。
  - 调法：调试时常用 `0.3~0.8`；值过大容易误判覆盖问题。

#### 推荐调参流程（实战）

1. **先保覆盖**：`min_contour_area`、`min_boundary_perimeter` 先降到较小值（如 `2~5`、`0~2`）。  
2. **再保细线**：降低 `merge_lambda`，必要时降低 `morph_kernel_size`。  
3. **最后简化**：在结果稳定后，逐步增大 `opt_tolerance` 或 `curve_tolerance` 控制体积。  
4. **每次只改 1~2 个参数**，并保存 SVG 对照，避免多变量耦合。  

#### 常见目标与起始配置

- **细线/线稿不断裂**：`merge_lambda=15~25`, `min_boundary_perimeter=0~2`, `morph_kernel_size=0~3`  
- **减少噪点小碎块**：`min_region_area=10~40`, `min_contour_area=10~30`, `min_boundary_perimeter=2~6`  
- **降低 SVG 复杂度**：`num_colors=8~16`, `opt_tolerance=0.2~0.6`, `curve_tolerance=2~4`  
- **几何边缘更硬朗**：`alpha_max=0.7~1.0`, `corner_threshold=130~150`, 视情况关闭 `enable_curve_opt`

## 抠图功能架构

### 异步任务模式

抠图 API 采用与 convert 相同的异步任务模式：

1. `POST /api/matting` — 提交图片和抠图方法，立即返回 `202 Accepted` + `task_id`
2. `GET /api/matting/tasks/:id` — 轮询任务状态（pending / running / completed / failed），完成后包含耗时信息
3. `GET /api/matting/tasks/:id/foreground` — 下载抠图前景 PNG（仅 completed 状态可用）
4. `GET /api/matting/tasks/:id/mask` — 下载抠图 mask PNG

后端通过 `MattingTaskManager` 管理任务的提交、并发控制和生命周期。推理任务在独立的工作线程中执行，不阻塞 HTTP 线程。并发推理数量通过 `max_concurrent` 参数控制（默认值与 `--max-tasks` 相同）。

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
