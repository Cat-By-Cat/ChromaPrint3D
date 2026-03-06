import { ref, watch } from 'vue'
import type { SelectOption } from 'naive-ui'
import type { PanZoomGroupMap } from './usePanZoomGroups'

export type LinkageMode = 'linked' | 'independent' | 'custom'

export const linkageModeOptions: SelectOption[] = [
  { label: '全部联动', value: 'linked' },
  { label: '全部独立', value: 'independent' },
  { label: '自定义分组', value: 'custom' },
]

export const linkageGroupOptions: SelectOption[] = [
  { label: 'A 组', value: 'A' },
  { label: 'B 组', value: 'B' },
  { label: '独立', value: '__self__' },
]

type PanZoomGroupsController = {
  setGroups: (groups: PanZoomGroupMap) => void
  getViewGroup: (viewId: string) => string
  setViewGroup: (viewId: string, groupId: string) => void
  resetAll: () => void
}

type UsePanZoomLinkageOptions<TViewId extends string> = {
  panZoomGroups: PanZoomGroupsController
  linkedGroups: Record<TViewId, string>
  independentGroups: Record<TViewId, string>
}

export function usePanZoomLinkage<TViewId extends string>({
  panZoomGroups,
  linkedGroups,
  independentGroups,
}: UsePanZoomLinkageOptions<TViewId>) {
  const linkageMode = ref<LinkageMode>('linked')

  function applyLinkageMode(mode: LinkageMode): void {
    if (mode === 'custom') return
    panZoomGroups.setGroups(mode === 'linked' ? linkedGroups : independentGroups)
    panZoomGroups.resetAll()
  }

  function groupValueFor(viewId: TViewId): string {
    const group = panZoomGroups.getViewGroup(viewId)
    return group.startsWith('self:') ? '__self__' : group
  }

  function setViewGroup(viewId: TViewId, value: string): void {
    panZoomGroups.setViewGroup(viewId, value === '__self__' ? `self:${viewId}` : value)
    panZoomGroups.resetAll()
  }

  watch(
    () => linkageMode.value,
    (mode) => {
      applyLinkageMode(mode)
    },
    { immediate: true },
  )

  return {
    linkageMode,
    linkageModeOptions,
    linkageGroupOptions,
    applyLinkageMode,
    groupValueFor,
    setViewGroup,
  }
}
