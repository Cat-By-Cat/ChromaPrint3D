# ChromaPrint3D 本地开发指南

本文档讲解如何在本地搭建 ChromaPrint3D 的开发环境，涵盖**本地编译**和**Docker 编译**两种方式。

## 项目结构概览

```
ChromaPrint3D/
├── core/                  # C++ 核心库（颜色匹配、图像处理、3MF 导出）
│   ├── include/           # 公共头文件
│   ├── src/               # 源文件
│   └── tests/             # 单元测试（GoogleTest）
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

### 1.3 编译 C++ 后端

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
├── image_to_3mf               # 命令行转换工具
├── gen_calibration_board      # 校准板生成
├── build_colordb              # 颜色数据库构建
├── gen_stage                  # 阶梯数据生成
└── gen_representative_board   # 代表性校准板生成
```

**启用单元测试：**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13 \
  -DCHROMAPRINT3D_BUILD_TESTS=ON

cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

### 1.4 安装前端依赖

```bash
cd web
npm ci
cd ..
```

### 1.5 启动开发环境

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

### 1.6 日常开发流程

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

### 1.7 构建生产版本

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

### 2.5 构建运行时 Docker 镜像

编译完成后，打包为轻量运行时镜像：

```bash
docker build -t chromaprint3d .
```

此镜像仅包含运行时依赖（约 80MB），不含编译工具链。

### 2.6 运行

```bash
docker run --rm -p 8080:8080 chromaprint3d
```

访问 http://localhost:8080。

### 2.7 Docker 环境下的开发调试

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
| 编译后端（Release） | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13 && cmake --build build -j$(nproc)` |
| 增量编译 | `cmake --build build -j$(nproc)` |
| 启动后端 | `./build/bin/chromaprint3d_server --data ./data --model-pack ./data/model_pack/model_package.json --port 8080` |
| 安装前端依赖 | `cd web && npm ci` |
| 启动前端开发服务器 | `cd web && npm run dev` |
| 构建前端生产版本 | `cd web && npm run build` |
| 运行测试 | `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure` |
| Docker 编译 C++ | `docker run --rm -v $(pwd):/src -w /src chromaprint3d-build bash -c "cmake --build build -j\$(nproc)"` |
| Docker 构建镜像 | `docker build -t chromaprint3d .` |
| Docker 运行 | `docker run --rm -p 8080:8080 chromaprint3d` |

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
