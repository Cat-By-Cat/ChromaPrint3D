# ChromaPrint3D 本地开发与调试指南

本文档用于日常开发、联调与排障。  
所有构建步骤（依赖、编译、Docker 构建链路）统一维护在 [docs/build.md](build.md)。

## AGENTS 协作入口

如果你在和 AI agent 协作开发，建议先看：

- [AGENTS.md](../AGENTS.md)（仓库级规则与导航）
- [docs/agents/README.md](agents/README.md)（模块索引）
- [docs/agents/tasks/README.md](agents/tasks/README.md)（高频任务手册）

## 文档边界

- [docs/build.md](build.md)：构建与打包（唯一事实源）
- [docs/development.md](development.md)：开发运行、调试、联调、开发命令
- [docs/deployment.md](deployment.md)：生产部署、网络拓扑、安全与上线流程

## 项目结构概览

```text
ChromaPrint3D/
├── core/                  # C++ 核心库（颜色匹配、图像处理、3MF 导出）
├── infer/                 # 深度学习推理封装（ONNX Runtime 后端）
├── apps/                  # CLI 工具
├── web/
│   ├── frontend/          # Vue 3 前端
│   └── backend/           # C++ Drogon HTTP 服务
├── modeling/              # Python 建模管线
├── data/                  # 运行时数据（db/model_pack/models 等）
└── docs/                  # 文档
```

## 1. 开发前置

在开始开发前，请先完成：

1. 按 [docs/build.md](build.md) 完成一次可用构建
2. 确认 `build/bin/chromaprint3d_server` 可执行
3. 下载模型文件（需要抠图功能时）

模型下载命令：

```bash
python scripts/download_models.py
```

## 2. 启动本地开发环境（推荐双终端）

### 终端 A：后端服务

```bash
./build/bin/chromaprint3d_server \
  --data ./data \
  --model-pack ./data/model_pack/model_package.json \
  --port 8080 \
  --http-threads 4 \
  --max-tasks 4 \
  --log-level debug
```

说明：

- 开发模式通常不传 `--web`，静态资源由 Vite 提供。
- `--http-threads` 控制 HTTP 线程；`--max-tasks` 控制异步任务线程。
- 生产跨域部署建议额外传入 `--cors-origin <https-origin>` 与 `--require-cors-origin 1`，避免误开放来源。

### 终端 B：前端开发服务

```bash
cd web/frontend
npm run dev
```

默认地址：`http://localhost:5173`  
Vite 会把 `/api` 代理到 `http://localhost:8080`。

## 3. 日常开发命令

### 3.1 前端质量检查

```bash
cd web/frontend
npm run typecheck
npm run lint
npm run test
```

### 3.2 后端/API 冒烟检查

```bash
curl -s http://127.0.0.1:8080/api/v1/health
curl -s http://127.0.0.1:8080/api/v1/convert/defaults
curl -s http://127.0.0.1:8080/api/v1/vectorize/defaults
```

### 3.3 常用 CLI 调试命令

```bash
# 生成 4 色校准板
./build/bin/gen_calibration_board --channels 4 --out board.3mf --meta board.json

# 从校准板照片构建 ColorDB
./build/bin/build_colordb --image calib_photo.png --meta board.json --out color_db.json

# 图像转 3MF
./build/bin/raster_to_3mf --image input.png --db color_db.json --out output.3mf --preview preview.png

# 栅格图转 SVG
./build/bin/raster_to_svg --image input.png --out output.svg --colors 16 --contour-simplify 0.45
```

### 3.4 测试运行

如已在构建阶段启用 `CHROMAPRINT3D_BUILD_TESTS=ON`，可执行：

```bash
ctest --test-dir build --output-on-failure
```

## 4. Electron 本地开发

Electron 主进程会自动拉起后端（要求后端二进制已构建完成）。

```bash
cd web/electron
npm install
npm run dev
```

常用环境变量：

- `CHROMAPRINT3D_BACKEND_PATH`：指定后端二进制路径
- `CHROMAPRINT3D_DATA_DIR`：指定后端 `--data` 目录（打包态默认使用 `<userData>/data`）
- `CHROMAPRINT3D_MODEL_PACK_PATH`：指定后端 `--model-pack` 路径
- `CHROMAPRINT3D_BACKEND_PORT`：指定后端优先端口
- `CHROMAPRINT3D_RENDERER_URL`：指定前端地址（默认 `http://127.0.0.1:5173`）
- `VITE_UPLOAD_MAX_MB` / `VITE_UPLOAD_MAX_PIXELS`：前端上传预校验上限
- `CHROMAPRINT3D_MODEL_DOWNLOAD_RETRIES` / `CHROMAPRINT3D_MODEL_DOWNLOAD_TIMEOUT_MS` / `CHROMAPRINT3D_MODEL_DOWNLOAD_BACKOFF_MS`：Electron 模型下载重试与超时控制

## 5. Web/API 契约（开发关注）

### 5.1 统一响应格式

- JSON 成功：`{ "ok": true, "data": ... }`
- JSON 失败：`{ "ok": false, "error": { "code": "...", "message": "..." } }`
- 二进制下载：直接返回 bytes，不走 envelope

### 5.2 会话 token 识别优先级

1. `session` cookie
2. `X-ChromaPrint3D-Session` header

### 5.3 关键接口分组

| 分组 | 代表接口 |
|---|---|
| 健康与默认参数 | `GET /api/v1/health`、`GET /api/v1/convert/defaults`、`GET /api/v1/vectorize/defaults` |
| 数据库管理 | `GET /api/v1/databases`、`GET /api/v1/session/databases`、`POST /api/v1/session/databases/upload` |
| 异步任务提交 | `POST /api/v1/convert/raster`、`POST /api/v1/convert/vector`、`POST /api/v1/matting/tasks`、`POST /api/v1/vectorize/tasks` |
| 任务查询与产物 | `GET /api/v1/tasks`、`GET /api/v1/tasks/{id}`、`GET /api/v1/tasks/{id}/artifacts/{artifact}` |
| 校准链路 | `POST /api/v1/calibration/boards`、`POST /api/v1/calibration/boards/8color`、`POST /api/v1/calibration/colordb` |

### 5.4 Bambu Studio 预设参数

以下参数适用于 Raster/Vector 转换端点和校准板生成端点：

| 参数 | 类型 | 默认值 | 有效值 | 说明 |
|---|---|---|---|---|
| `nozzle_size` | string | `"n04"` | `"n02"`, `"n04"`, `"0.2"`, `"0.4"` | 喷嘴尺寸 |
| `face_orientation` | string | `"faceup"` | `"faceup"`, `"facedown"` | 观赏面朝向；`facedown` 时模型导出几何绕 Y 轴旋转 180° |

组合后会先选择 `data/presets/` 下 4 个预设之一（如 `bambu_p2s_0.08mm_n04_faceup.json`）。预设中的耗材丝配置（颜色、类型、温度等）完整写入 3MF，Bambu Studio 打开时优先加载文件内配置。模型颜色自动匹配到预设中最接近的耗材丝槽位（RGB 欧氏距离）。此外，`face_orientation=facedown` 会在导出阶段将模型几何整体绕 Y 轴旋转 180°，`faceup` 则保持原方向。

注意：`face_orientation` 与 `flip_y` 语义不同。`flip_y` 用于图像/坐标系方向适配（预处理与体素构建阶段），`face_orientation` 用于最终导出几何朝向控制。

### 5.5 Raster 匹配参数说明

- `cluster_method`：`kmeans`（默认）或 `slic`
- `kmeans` 路径参数：`cluster_count`
- `slic` 路径参数：`slic_target_superpixels`、`slic_compactness`、`slic_iterations`、`slic_min_region_ratio`
- 互斥规则：当 `cluster_method=slic` 时，后端会强制 `dither=none`

### 5.6 Vectorize 透明 PNG 行为

- 输入为带 alpha 的 PNG 时，`alpha==0` 的像素不会参与矢量化，导出的 SVG 默认保持透明背景。

### 5.7 Vectorize 参数说明

以下参数用于 `POST /api/v1/vectorize/tasks` 的 `params` JSON，同时 `GET /api/v1/vectorize/defaults`
会返回对应默认值。

| 参数 | 类型 | 默认值 | 有效范围 | 说明 | 建议暴露层级 |
|---|---|---:|---|---|---|
| `num_colors` | int | 16 | `[2,256]` | 颜色量化数量，越大越接近原图但更复杂 | 前端主参数 |
| `curve_fit_error` | float | 0.8 | `[0.1,5]` | Bezier 拟合误差阈值（像素），越小越贴边界 | 前端主参数 |
| `contour_simplify` | float | 0.45 | `[0,10]` | 轮廓简化强度，越大节点越少 | 前端主参数 |
| `svg_enable_stroke` | bool | true | `true/false` | 启用细线增强描边输出 | 前端主参数 |
| `enable_coverage_fix` | bool | true | `true/false` | 启用覆盖修复补洞 | 前端主参数 |
| `smoothing_spatial` | float | 15 | `[0,50]` | Mean Shift 空间窗口（sp） | 前端高级参数 |
| `smoothing_color` | float | 25 | `[0,80]` | Mean Shift 颜色窗口（sr） | 前端高级参数 |
| `thin_line_max_radius` | float | 2.5 | `[0.5,10]` | 细线检测距离阈值（像素） | 前端高级参数 |
| `topology_cleanup` | float | 0.15 | `[0,10]` | 拓扑清理强度（更偏二值路径） | 前端高级参数 |
| `min_region_area` | int | 10 | `>=0` | 小区域合并阈值（像素²） | 前端高级参数 |
| `min_contour_area` | float | 10.0 | `>=0` | 最小保留轮廓面积（像素²） | 前端高级参数 |
| `min_hole_area` | float | 4.0 | `>=0` | 最小保留孔洞面积（像素²） | 前端高级参数 |
| `min_coverage_ratio` | float | 0.998 | `[0,1]` | 覆盖率低于该值时触发 coverage fix | 前端高级参数 |
| `svg_stroke_width` | float | 0.5 | `[0,20]` | SVG 描边宽度（像素） | 前端高级参数 |
| `slic_region_size` | int | 20 | `[0,100]` | SLIC 超像素尺寸，`0` 表示自动 | 仅 CLI / 调试 |
| `corner_angle_threshold` | float | 135 | `[90,175]` | 角点判定阈值（度） | 仅 CLI / 调试 |
| `upscale_short_edge` | int | 600 | `[0,2000]` | 自动放大触发短边阈值，`0` 禁用放大 | 仅 CLI / 调试 |
| `max_working_pixels` | int | 3000000 | `[0,100000000]` | 大图预处理像素上限；超出时先按面积缩小以控制 SVG 复杂度，`0` 禁用 | 仅 API / 调试 |

说明：

- 前端默认选择性暴露：主参数 + 高级参数；技术性参数优先在 CLI 调试。
- 参数越多不一定越好，建议先调整 `num_colors`、`curve_fit_error`、`contour_simplify`。
- 当源图分辨率很高（如 4K+）且导出 SVG 过大时，优先保留 `max_working_pixels` 默认值或适当下调。

### 5.8 任务系统生命周期

- 任务状态：`pending -> running -> completed/failed`
- 队列上限：`--max-queue`
- 单会话并发上限：`--max-owner-tasks`
- 结果缓存 TTL：`--task-ttl`
- 结果总内存预算：`--max-result-mb`

## 6. 常见开发问题

### 6.1 前端启动了但 API 全失败

- 检查后端是否监听在 `8080`
- 检查 `chromaprint3d_server` 启动日志是否报错

### 6.2 Electron 启动时报后端缺失

- 先按 [docs/build.md](build.md) 完成后端二进制构建
- 或通过 `CHROMAPRINT3D_BACKEND_PATH` 显式指定路径

### 6.3 上传图片时报大小/像素限制

- 后端限制：`--max-upload-mb`、`--max-pixels`
- 前端预检限制：`VITE_UPLOAD_MAX_MB`、`VITE_UPLOAD_MAX_PIXELS`

### 6.4 子模块目录为空

```bash
git submodule update --init --recursive
```

## 7. 模型管理

模型文件（`.onnx`）不随仓库直接分发，按清单动态下载。

### 7.1 下载模型

```bash
python scripts/download_models.py
```

说明：

- 开发脚本默认从 `data/models/models.json` 的 `base_url` 下载。
- Electron 应用内下载优先使用 `models.json` 的 `sources`（可配置多源回退），并下载到用户可写目录（默认 `<userData>/data/models`）。
- Electron 下载前支持连通性检查（按源探测可达性），建议先检查再下载。
- Electron 下载完成后需要重启应用，后端会在启动阶段重新发现并加载模型。

### 7.2 上传/更新模型（维护者）

```bash
pip install huggingface_hub
huggingface-cli login
python scripts/upload_models.py --create-repo
```

### 7.3 同步到国内源（ModelScope，维护者）

1) 在 ModelScope 创建仓库（建议与 HF 同名），并保持目录结构与 `models.json` 的 `models[].path` 一致。  
2) 使用 Git LFS 推送模型文件：

```bash
git lfs install
git clone "https://www.modelscope.cn/models/<org>/<repo>.git"
cd <repo>
mkdir -p matting
cp /path/to/*.onnx matting/
git add matting/*.onnx
git commit -m "update model files"
git push
```

3) 更新本仓库 `data/models/models.json` 的 `sources`（优先国内源），并同步 `sha256/size_bytes`。
