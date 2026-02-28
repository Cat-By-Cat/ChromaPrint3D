# ChromaPrint3D 应用程序指南

本目录包含 ChromaPrint3D 的全部可执行程序：5 个命令行工具和 1 个 HTTP 服务器。

## 构建

在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

构建产物位于 `build/bin/`：

| 可执行文件 | 功能 |
|---|---|
| `gen_calibration_board` | 生成校准板 3MF 模型与元数据 |
| `build_colordb` | 从校准板照片构建 ColorDB |
| `raster_to_3mf` | 将图像转换为多色 3MF 模型 |
| `gen_representative_board` | 从配方集生成代表性校准板 |
| `gen_stage` | 生成阶梯校准模型 |
| `chromaprint3d_server` | HTTP 服务器，提供 Web API |

## 典型工作流程

```mermaid
flowchart TD
    A[gen_calibration_board] -->|calibration_board.3mf| B[打印校准板]
    A -->|calibration_board.json| D[build_colordb]
    B -->|拍摄照片 calib.png| D
    D -->|color_db.json| E[raster_to_3mf]
    F[输入图像 photo.png] --> E
    E -->|output.3mf| G[切片打印]
    E -->|preview.png| H[预览效果]
```

> 所有工具均支持 `--help` / `-h` 查看帮助，`--log-level` 设置日志级别（trace / debug / info / warn / error / off，默认 info）。

---

## 命令行工具

### gen_calibration_board

生成用于颜色校准的 3MF 校准板模型及其元数据 JSON。元数据包含色块布局、配方索引等信息，供后续 `build_colordb` 使用。

**参数**

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `--channels N` | int | `4` | 颜色通道数（2–8） |
| `--out PATH` | string | `calibration_board.3mf` | 输出 3MF 文件路径 |
| `--meta PATH` | string | `calibration_board.json` | 输出元数据 JSON 路径 |
| `--scale N` | int | 配置默认 | 分辨率缩放倍数 |
| `--log-level LEVEL` | string | `info` | 日志级别 |

默认调色板（按通道数自动分配）：

- 4 通道：White, Yellow, Red, Blue
- 3 通道：White, Gray, Black
- 2 通道：Color A, Color B
- 其他：Channel 0, Channel 1, ...

材质统一为 `PLA Basic`。

**示例**

```bash
# 生成 4 通道校准板
gen_calibration_board --channels 4 --out board.3mf --meta board.json

# 生成 5 通道校准板，2 倍分辨率
gen_calibration_board --channels 5 --scale 2
```

---

### build_colordb

从打印并拍摄的校准板照片与元数据 JSON 构建 ColorDB。程序会自动定位照片中的校准板区域、提取各色块颜色，并与配方对应生成颜色数据库。

**参数**

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `--image PATH` | string | *必填* | 校准板照片文件 |
| `--meta PATH` | string | *必填* | 校准板元数据 JSON |
| `--out PATH` | string | `<image_stem>_colordb.json` | 输出 ColorDB JSON 路径 |
| `--log-level LEVEL` | string | `info` | 日志级别 |

**示例**

```bash
# 从照片和元数据生成 ColorDB
build_colordb --image calib_photo.png --meta board.json --out my_colordb.json

# 使用默认输出路径（calib_photo_colordb.json）
build_colordb --image calib_photo.png --meta board.json
```

---

### raster_to_3mf

将输入图像转换为多色 3MF 模型。使用 ColorDB 进行颜色匹配，可选使用模型包（model pack）进行 AI 辅助匹配。同时输出预览图和源 mask 图。

**参数**

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `--image PATH` | string | *必填* | 输入图像文件 |
| `--db PATH` | string | *必填* | ColorDB 文件或目录（可重复指定多个） |
| `--out PATH` | string | `<image_stem>.3mf` | 输出 3MF 文件路径 |
| `--preview PATH` | string | `<out_stem>_preview.png` | 预览图输出路径 |
| `--source-mask PATH` | string | `<out_stem>_source_mask.png` | 源 mask 图输出路径 |

图像处理参数：

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `--scale S` | float | `1.0` | 图像预处理缩放比例 |
| `--max-width N` | int | `512` | 最大宽度（0 = 不限制） |
| `--max-height N` | int | `512` | 最大高度（0 = 不限制） |
| `--flip-y 0\|1` | bool | `1` | 构建模型时翻转 Y 轴 |
| `--pixel-mm X` | float | ColorDB 值 | 像素尺寸（mm） |
| `--layer-mm X` | float | ColorDB 值 | 层高（mm） |

颜色匹配参数：

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `--mode 0.08x5\|0.04x10` | string | `0.08x5` | 打印模式（层高×颜色层数） |
| `--color-space lab\|rgb` | string | `lab` | 颜色匹配空间 |
| `--k N` | int | `1` | Top-K 候选数 |
| `--clusters N` | int | `64` | 聚类数（≤1 禁用聚类） |

模型匹配参数（需提供 `--model-pack`）：

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `--model-pack PATH` | string | 无 | 模型包 JSON 路径 |
| `--model-enable 0\|1` | bool | `1` | 启用模型回退 |
| `--model-only 0\|1` | bool | `0` | 仅使用模型匹配 |
| `--model-threshold X` | float | 模型包默认 | 模型回退阈值（DeltaE76） |
| `--model-margin X` | float | 模型包默认 | 模型回退边界（DeltaE76） |

**示例**

```bash
# 基本转换
raster_to_3mf --image photo.png --db colordb.json

# 使用多个 ColorDB + 模型包，自定义输出
raster_to_3mf --image photo.png \
  --db colordb1.json --db colordb2.json \
  --model-pack model_package.json \
  --out result.3mf \
  --mode 0.04x10 --color-space lab --clusters 128

# 高分辨率转换，禁用聚类
raster_to_3mf --image photo.png --db ./dbs/ \
  --max-width 1024 --max-height 1024 --clusters 0
```

---

### gen_representative_board

从一组配方数据生成代表性校准板。用于将已有配方集合可视化为 32×32 的校准板模型，便于验证配方覆盖情况。

**参数**

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `--recipes PATH` | string | *必填* | 配方 JSON 文件（需 ≥1024 条） |
| `--ref-db PATH` | string | 无 | 参考 ColorDB JSON（提供调色板/层高等默认值） |
| `--out PATH` | string | `representative_board.3mf` | 输出 3MF 文件路径 |
| `--meta PATH` | string | `<out_stem>.json` | 输出元数据 JSON 路径 |
| `--scale N` | int | 配置默认 | 分辨率缩放倍数 |
| `--log-level LEVEL` | string | `info` | 日志级别 |

配方 JSON 格式支持两种形式：

```json
// 形式 1：纯数组
[[0,1,0,1,0], [1,0,1,0,1], ...]

// 形式 2：带元数据的对象
{
  "palette": [{"color": "White", "material": "PLA Basic"}, ...],
  "layer_height_mm": 0.08,
  "line_width_mm": 0.42,
  "base_layers": 10,
  "base_channel_idx": 0,
  "layer_order": "Top2Bottom",
  "recipes": [[0,1,0,1,0], ...]
}
```

若使用形式 2 且包含完整元数据，则无需 `--ref-db`。

**示例**

```bash
# 从配方文件和参考数据库生成
gen_representative_board --recipes recipes.json --ref-db colordb.json

# 配方文件已包含完整元数据
gen_representative_board --recipes full_recipes.json --out board.3mf
```

---

### gen_stage

生成阶梯模型 3MF，用于校准打印机的多层精度。模型包含 6×3 = 18 个方块，每个方块的层数不同（0–16 层 + 25 层），用于验证不同厚度的打印效果。

**参数**

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `--out PATH` | string | `stage.3mf` | 输出 3MF 文件路径 |
| `--log-level LEVEL` | string | `info` | 日志级别 |

内置配置（不可通过命令行修改）：

| 属性 | 值 |
|---|---|
| 网格布局 | 6 列 × 3 行（18 个方块） |
| 方块尺寸 | 10 mm |
| 间距 | 1 mm |
| 像素尺寸 | 1.0 mm |
| 层高 | 0.04 mm |
| 底座厚度 | 1.0 mm |
| 阶梯层数 | 0, 1, 2, ..., 16, 25 |

**示例**

```bash
gen_stage --out my_stage.3mf
```

---

## HTTP 服务器

### chromaprint3d_server

提供 RESTful API 的 HTTP 服务器，支持校准板生成、ColorDB 管理、图像转换等功能。可选挂载 Web 前端静态文件。

**启动参数**

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `--data DIR` | string | *必填* | ColorDB 数据目录 |
| `--port PORT` | int | `8080` | 监听端口 |
| `--host HOST` | string | `0.0.0.0` | 绑定地址 |
| `--model-pack PATH` | string | 无 | 模型包 JSON 路径 |
| `--web DIR` | string | 无 | Web 前端静态文件目录 |
| `--max-upload-mb N` | int | `50` | 最大上传大小（MB） |
| `--http-threads N` | int | `4` | Drogon HTTP 工作线程数 |
| `--max-tasks N` | int | `4` | 异步任务工作线程数（不影响 HTTP 线程） |
| `--max-queue N` | int | `256` | 异步任务队列上限 |
| `--task-ttl N` | int | `3600` | 任务 TTL（秒） |
| `--session-ttl N` | int | `3600` | 会话 TTL（秒） |
| `--max-owner-tasks N` | int | `32` | 单会话 in-flight 任务上限 |
| `--max-result-mb N` | int | `512` | 任务结果内存总上限（MB） |
| `--max-pixels N` | int | `16777216` | 单图解码像素上限 |
| `--max-session-colordbs N` | int | `10` | 单会话 ColorDB 上限 |
| `--log-level LEVEL` | string | `info` | 日志级别 |

**启动示例**

```bash
# 最小启动
chromaprint3d_server --data ./data/dbs

# 完整配置
chromaprint3d_server \
  --data ./data/dbs \
  --model-pack ./data/model_pack/model_package.json \
  --web ./web/dist \
  --port 8080 \
  --http-threads 4 \
  --max-tasks 8 \
  --max-upload-mb 100 \
  --log-level info
```

启动后访问 `http://localhost:8080` 即可使用 Web 界面（需指定 `--web`）。

---

### API 参考（Drogon v1）

当前服务仅保留 `/api/v1/*` 契约，旧 `/api/*` 路径已移除。

常用端点：

- `GET /api/v1/health`
- `GET /api/v1/databases`
- `GET /api/v1/convert/defaults`
- `POST /api/v1/convert/raster`
- `POST /api/v1/convert/vector`
- `GET /api/v1/tasks`
- `GET /api/v1/tasks/{id}`
- `DELETE /api/v1/tasks/{id}`
- `GET /api/v1/tasks/{id}/artifacts/{artifact}`
- `GET /api/v1/matting/methods`
- `POST /api/v1/matting/tasks`
- `POST /api/v1/vectorize/tasks`
- `GET /api/v1/vectorize/defaults`
- `POST /api/v1/calibration/boards`
- `POST /api/v1/calibration/boards/8color`
- `GET /api/v1/calibration/boards/{id}/model`
- `GET /api/v1/calibration/boards/{id}/meta`
- `POST /api/v1/calibration/colordb`
- `GET /api/v1/session/databases`
- `POST /api/v1/session/databases/upload`
- `GET /api/v1/session/databases/{name}/download`
- `DELETE /api/v1/session/databases/{name}`

详细请求/响应字段请参考：

- `README.md`
- `docs/development.md`
- `docs/deployment.md`
