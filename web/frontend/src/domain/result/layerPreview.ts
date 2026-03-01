import type { LayerPreviewsSummary } from '../../types'

function clampInt(value: number, min: number, max: number): number {
  if (!Number.isFinite(value)) return min
  const rounded = Math.round(value)
  return Math.min(max, Math.max(min, rounded))
}

export function getAvailableLayerCount(summary: LayerPreviewsSummary | null | undefined): number {
  if (!summary?.enabled) return 0
  return Math.max(0, Math.min(summary.layers, summary.artifacts.length))
}

export function getTopLayerPosition(
  summary: LayerPreviewsSummary | null | undefined,
  rawPosition: number,
): number {
  const layers = getAvailableLayerCount(summary)
  if (layers <= 0) return 1
  return clampInt(rawPosition, 1, layers)
}

export function getLayerArtifactIndexFromTop(
  summary: LayerPreviewsSummary,
  topLayerPosition: number,
): number {
  const layers = getAvailableLayerCount(summary)
  if (layers <= 0) return 0

  const topPos = getTopLayerPosition(summary, topLayerPosition)
  if (summary.layer_order === 'Bottom2Top') {
    return layers - topPos
  }
  return topPos - 1
}

export function getLayerArtifactKeyFromTop(
  summary: LayerPreviewsSummary | null | undefined,
  topLayerPosition: number,
): string | null {
  if (!summary) return null
  const layers = getAvailableLayerCount(summary)
  if (layers <= 0) return null

  const idx = getLayerArtifactIndexFromTop(summary, topLayerPosition)
  return summary.artifacts[idx] ?? null
}
