import { computed, reactive } from 'vue'
import { usePanZoom } from './usePanZoom'
import type { PanZoomController } from './usePanZoom'

export type PanZoomGroupId = string
export type PanZoomViewId = string
export type PanZoomGroupMap = Record<PanZoomViewId, PanZoomGroupId>

function normalizeGroupId(viewId: string, groupId: string | undefined): string {
  const normalized = groupId?.trim() ?? ''
  return normalized.length > 0 ? normalized : viewId
}

export function usePanZoomGroups(initialGroups: PanZoomGroupMap) {
  const viewGroups = reactive<PanZoomGroupMap>({ ...initialGroups })
  const groupControllers = new Map<PanZoomGroupId, PanZoomController>()

  function getViewGroup(viewId: PanZoomViewId): PanZoomGroupId {
    return normalizeGroupId(viewId, viewGroups[viewId])
  }

  function ensureGroupController(groupId: PanZoomGroupId): PanZoomController {
    const normalized = normalizeGroupId(groupId, groupId)
    const existing = groupControllers.get(normalized)
    if (existing) return existing
    const created = usePanZoom()
    groupControllers.set(normalized, created)
    return created
  }

  function controllerFor(viewId: PanZoomViewId): PanZoomController {
    return ensureGroupController(getViewGroup(viewId))
  }

  function setViewGroup(viewId: PanZoomViewId, groupId: PanZoomGroupId): void {
    viewGroups[viewId] = normalizeGroupId(viewId, groupId)
  }

  function setGroups(patch: PanZoomGroupMap): void {
    for (const [viewId, groupId] of Object.entries(patch)) {
      setViewGroup(viewId, groupId)
    }
  }

  function resetGroup(groupId: PanZoomGroupId): void {
    ensureGroupController(groupId).resetView()
  }

  function resetView(viewId: PanZoomViewId): void {
    controllerFor(viewId).resetView()
  }

  function resetAll(): void {
    const allGroupIds = new Set<PanZoomGroupId>([
      ...Object.values(viewGroups),
      ...groupControllers.keys(),
    ])
    for (const groupId of allGroupIds) {
      resetGroup(groupId)
    }
  }

  const activeGroupIds = computed(() => Array.from(new Set(Object.values(viewGroups))))

  return {
    viewGroups,
    activeGroupIds,
    controllerFor,
    getViewGroup,
    setViewGroup,
    setGroups,
    resetGroup,
    resetView,
    resetAll,
  }
}
