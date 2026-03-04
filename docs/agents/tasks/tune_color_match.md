# 调整颜色匹配策略

## 适用场景

修改颜色匹配效果相关逻辑，包括候选检索、评分排序、聚类策略、模型门控参数或抖动路径。

## 影响文件（按优先级）

- `core/src/match/match.cpp`
- `core/src/match/candidate_select.cpp`
- `core/src/match/recipe_map.cpp`
- `core/include/chromaprint3d/recipe_map.h`
- `core/include/chromaprint3d/color.h`（若涉及色差公式）
- `modeling/pipeline/step5_build_model_package.py`（若涉及模型包阈值/余量）
- [README.md](../../../README.md)
- [docs/development.md](../../development.md)

## 实施步骤

1. 明确本次调整目标（精度优先、速度优先、稳定性优先）。
2. 在 `match.cpp` / `candidate_select.cpp` 调整候选筛选与评分逻辑。
3. 如增加配置项，补充公共头文件与调用链参数透传。
4. 若涉及模型门控（threshold/margin），核对 modeling 侧输出与运行时读取一致。
5. 用固定样例做前后对比，记录关键指标（色差、耗时、失败率）。
6. 同步文档中的参数解释与推荐值。

## 风险点

- 精度提升但计算成本显著增加。
- 参数组合导致回归（例如某些材质/调色板下退化）。
- 调整后统计字段含义变化但外层未同步。
- C++ 与 Python 侧门控参数语义不一致。

## 回归检查

- 构建检查：
  - `cmake --build build -j$(nproc)`
- 功能检查：
  - `build/bin/raster_to_3mf --image <img> --db <db> --out /tmp/out.3mf --preview /tmp/preview.png`
- 对比检查：
  - 固定输入下记录至少一组“改前/改后”耗时和可视化结果
- 文档检查：
  - 参数与行为变化已同步 [README.md](../../../README.md) / [docs/development.md](../../development.md)

