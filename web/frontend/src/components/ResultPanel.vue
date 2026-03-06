<script setup lang="ts">
import { computed, onUnmounted, ref, watch } from 'vue'
import { storeToRefs } from 'pinia'
import {
  NCard,
  NButton,
  NButtonGroup,
  NSelect,
  NDescriptions,
  NDescriptionsItem,
  NSlider,
  NSpace,
  NText,
} from 'naive-ui'
import { useBlobDownload } from '../composables/useBlobDownload'
import { usePanZoomLinkage } from '../composables/usePanZoomLinkage'
import { useObjectUrlLifecycle } from '../composables/useObjectUrlLifecycle'
import { usePanZoomGroups } from '../composables/usePanZoomGroups'
import ZoomableImageViewport from './common/ZoomableImageViewport.vue'
import {
  getAvailableLayerCount,
  getLayerArtifactKeyFromTop,
  getTopLayerPosition,
} from '../domain/result/layerPreview'
import { fetchBlobWithSession } from '../runtime/protectedRequest'
import { getLayerPreviewPath, getPreviewPath, getResultPath, getSourceMaskPath } from '../services/resultService'
import { useAppStore } from '../stores/app'

const appStore = useAppStore()
const { completedTask, selectedFile } = storeToRefs(appStore)
const isCompleted = computed(() => completedTask.value?.status === 'completed')
const result = computed(() => completedTask.value?.result ?? null)
const taskId = computed(() => completedTask.value?.id ?? '')
const { downloadByUrl } = useBlobDownload()

type ResultImageView = 'preview' | 'source-mask'

const activeImageView = ref<ResultImageView>('preview')
const sliderTopLayerPosition = ref(1)
const visibleLayerArtifactKey = ref<string | null>(null)
const pendingLayerArtifactKey = ref<string | null>(null)
const layerArtifactUrlCache = ref<Record<string, string>>({})
const layerArtifactLoadPromises = new Map<string, Promise<string | null>>()
const { createUrl, revokeUrl } = useObjectUrlLifecycle()
const currentImageBlobUrl = ref('')
const currentImageLoading = ref(false)
let currentImageRequestId = 0

const hasPreview = computed(() => Boolean(result.value?.has_preview))
const hasSourceMask = computed(() => Boolean(result.value?.has_source_mask))
const canToggleResultImage = computed(() => hasPreview.value && hasSourceMask.value)
const layerPreviews = computed(() => result.value?.layer_previews ?? null)
const availableLayerCount = computed(() => getAvailableLayerCount(layerPreviews.value))
const hasLayerPreviews = computed(() => Boolean(taskId.value) && availableLayerCount.value > 0)
const hasCurrentImage = computed(() => currentImageView.value !== null)
const useSingleImageLayout = computed(() => !hasLayerPreviews.value || !hasCurrentImage.value)

const currentImageView = computed<ResultImageView | null>(() => {
  if (activeImageView.value === 'preview') {
    if (hasPreview.value) return 'preview'
    if (hasSourceMask.value) return 'source-mask'
    return null
  }
  if (hasSourceMask.value) return 'source-mask'
  if (hasPreview.value) return 'preview'
  return null
})

const currentImageTitle = computed(() =>
  currentImageView.value === 'source-mask' ? '颜色源掩码' : '预览图',
)

const currentImageUrl = computed(() => {
  return currentImageBlobUrl.value
})

const showImageSection = computed(() => hasCurrentImage.value || hasLayerPreviews.value)

const currentTopLayerPosition = computed(() =>
  getTopLayerPosition(layerPreviews.value, sliderTopLayerPosition.value),
)

const currentLayerArtifactKey = computed(() =>
  getLayerArtifactKeyFromTop(layerPreviews.value, currentTopLayerPosition.value),
)

const currentLayerImageUrl = computed(() => {
  if (!visibleLayerArtifactKey.value) return ''
  return layerArtifactUrlCache.value[visibleLayerArtifactKey.value] ?? ''
})

const currentLayerTitle = computed(() => {
  if (!hasLayerPreviews.value) return ''
  return `Top ${currentTopLayerPosition.value} / ${availableLayerCount.value}`
})

const currentLayerMeta = computed(() => {
  if (!layerPreviews.value) return ''
  const orderText =
    layerPreviews.value.layer_order === 'Bottom2Top' ? '层序：底部到顶部' : '层序：顶部到底部'
  return `${layerPreviews.value.width}×${layerPreviews.value.height} px | ${orderText}`
})

const isLayerImageLoading = computed(() => {
  const targetArtifact = currentLayerArtifactKey.value
  if (!targetArtifact || !hasLayerPreviews.value) return false
  return visibleLayerArtifactKey.value !== targetArtifact
})

const panZoomGroups = usePanZoomGroups({
  current: 'compare',
  layer: 'compare',
})

const canConfigureLinkage = computed(() => hasCurrentImage.value && hasLayerPreviews.value)
const { groupValueFor, linkageGroupOptions, linkageMode, linkageModeOptions, setViewGroup } =
  usePanZoomLinkage({
    panZoomGroups,
    linkedGroups: {
      current: 'A',
      layer: 'A',
    },
    independentGroups: {
      current: 'self:current',
      layer: 'self:layer',
    },
  })

async function loadCurrentImage(view: ResultImageView | null): Promise<void> {
  currentImageRequestId += 1
  const requestId = currentImageRequestId
  currentImageLoading.value = Boolean(view && taskId.value)
  revokeUrl(currentImageBlobUrl.value)
  currentImageBlobUrl.value = ''
  if (!view || !taskId.value) {
    currentImageLoading.value = false
    return
  }

  const requestTaskId = taskId.value
  const imageUrl = view === 'source-mask' ? getSourceMaskPath(requestTaskId) : getPreviewPath(requestTaskId)
  try {
    const blob = await fetchBlobWithSession(imageUrl)
    if (currentImageRequestId !== requestId || taskId.value !== requestTaskId) return
    currentImageBlobUrl.value = createUrl(blob)
  } catch {
    // keep the viewport empty when the preview endpoint is not reachable
  } finally {
    if (currentImageRequestId === requestId) {
      currentImageLoading.value = false
    }
  }
}

function clearLayerImageCache() {
  const cachedUrls = Object.values(layerArtifactUrlCache.value)
  for (const url of cachedUrls) revokeUrl(url)
  layerArtifactUrlCache.value = {}
  layerArtifactLoadPromises.clear()
  visibleLayerArtifactKey.value = null
  pendingLayerArtifactKey.value = null
}

async function loadLayerArtifactToCache(artifactKey: string): Promise<string | null> {
  if (!artifactKey || !taskId.value) return null
  const cached = layerArtifactUrlCache.value[artifactKey]
  if (cached) return cached

  const loading = layerArtifactLoadPromises.get(artifactKey)
  if (loading) return loading

  const requestTaskId = taskId.value
  const promise = (async () => {
    try {
      const blob = await fetchBlobWithSession(getLayerPreviewPath(requestTaskId, artifactKey))
      if (taskId.value !== requestTaskId) return null

      const objectUrl = createUrl(blob)
      const current = layerArtifactUrlCache.value[artifactKey]
      if (current) {
        revokeUrl(objectUrl)
        return current
      }
      layerArtifactUrlCache.value = {
        ...layerArtifactUrlCache.value,
        [artifactKey]: objectUrl,
      }
      return objectUrl
    } catch {
      return null
    } finally {
      layerArtifactLoadPromises.delete(artifactKey)
    }
  })()
  layerArtifactLoadPromises.set(artifactKey, promise)
  return promise
}

function prefetchNeighborLayerArtifacts(centerArtifactKey: string | null) {
  const summary = layerPreviews.value
  if (!summary || !centerArtifactKey) return

  const centerIndex = summary.artifacts.indexOf(centerArtifactKey)
  if (centerIndex < 0) return

  const candidates = [summary.artifacts[centerIndex - 1], summary.artifacts[centerIndex + 1]].filter(
    (item): item is string => Boolean(item),
  )
  for (const candidate of candidates) {
    void loadLayerArtifactToCache(candidate)
  }
}

async function ensureVisibleLayerArtifact(artifactKey: string | null) {
  pendingLayerArtifactKey.value = artifactKey
  if (!artifactKey) {
    visibleLayerArtifactKey.value = null
    return
  }

  const cached = layerArtifactUrlCache.value[artifactKey]
  if (cached) {
    visibleLayerArtifactKey.value = artifactKey
    prefetchNeighborLayerArtifacts(artifactKey)
    return
  }

  const loaded = await loadLayerArtifactToCache(artifactKey)
  if (pendingLayerArtifactKey.value === artifactKey && loaded) {
    visibleLayerArtifactKey.value = artifactKey
  }
  if (pendingLayerArtifactKey.value === artifactKey) {
    prefetchNeighborLayerArtifacts(artifactKey)
  }
}

function handleLayerSliderUpdate(value: number) {
  sliderTopLayerPosition.value = getTopLayerPosition(layerPreviews.value, value)
}

onUnmounted(() => {
  currentImageRequestId += 1
  currentImageLoading.value = false
  revokeUrl(currentImageBlobUrl.value)
  currentImageBlobUrl.value = ''
  clearLayerImageCache()
})

watch(
  () => taskId.value,
  () => {
    // 每次新任务结果默认回到预览图，层位重置到顶部层。
    activeImageView.value = 'preview'
    sliderTopLayerPosition.value = 1
    clearLayerImageCache()
    panZoomGroups.resetAll()
  },
)

watch(
  () => [currentImageView.value, taskId.value] as const,
  ([view]) => {
    void loadCurrentImage(view)
  },
  { immediate: true },
)

watch(
  () => [
    layerPreviews.value?.layers ?? 0,
    layerPreviews.value?.layer_order ?? '',
    layerPreviews.value?.artifacts.length ?? 0,
  ],
  () => {
    sliderTopLayerPosition.value = 1
    clearLayerImageCache()
  },
)

watch(
  () => currentLayerArtifactKey.value,
  (artifactKey) => {
    void ensureVisibleLayerArtifact(artifactKey)
  },
  { immediate: true },
)

function handleSetImageView(view: ResultImageView) {
  if (view === 'preview' && !hasPreview.value) return
  if (view === 'source-mask' && !hasSourceMask.value) return
  activeImageView.value = view
}

const download3mfFilename = computed(() => {
  const rawName = selectedFile.value?.name?.trim() ?? ''
  if (rawName.length > 0) {
    const dot = rawName.lastIndexOf('.')
    const baseName = dot > 0 ? rawName.slice(0, dot) : rawName
    return `${baseName || 'result'}.3mf`
  }
  return taskId.value ? `${taskId.value.substring(0, 8)}.3mf` : 'result.3mf'
})

async function handleDownload3MF() {
  if (!taskId.value) return
  const url = getResultPath(taskId.value)
  try {
    await downloadByUrl(url, download3mfFilename.value)
  } catch {
    // error is already handled by runtime abstraction caller when needed
  }
}
</script>

<template>
  <NCard v-if="isCompleted && result" title="转换结果" size="small">
    <template #header-extra>
      <NText v-if="showImageSection" depth="3" class="result-header-tip">
        滚轮缩放，拖拽移动；右侧滑块实时切换分层
      </NText>
    </template>
    <NSpace vertical :size="16">
      <NSpace v-if="showImageSection" align="center" :size="8" class="linkage-toolbar">
        <NText depth="3" class="linkage-toolbar__label">联动模式</NText>
        <NSelect
          v-model:value="linkageMode"
          size="small"
          :options="linkageModeOptions"
          style="width: 140px"
          :disabled="!canConfigureLinkage"
        />
      </NSpace>
      <div v-if="showImageSection && linkageMode === 'custom' && canConfigureLinkage" class="linkage-custom-grid">
        <NText depth="3" class="linkage-custom-grid__label">主图</NText>
        <NSelect
          :value="groupValueFor('current')"
          size="small"
          :options="linkageGroupOptions"
          @update:value="(value) => setViewGroup('current', String(value ?? 'A'))"
        />
        <NText depth="3" class="linkage-custom-grid__label">分层图</NText>
        <NSelect
          :value="groupValueFor('layer')"
          size="small"
          :options="linkageGroupOptions"
          @update:value="(value) => setViewGroup('layer', String(value ?? 'A'))"
        />
      </div>
      <div
        v-if="showImageSection"
        class="result-images-layout"
        :class="{ 'result-images-layout--single': useSingleImageLayout }"
      >
        <NCard v-if="currentImageView" :title="currentImageTitle" size="small" embedded>
          <template #header-extra>
            <NSpace :size="8" align="center">
              <NButtonGroup v-if="canToggleResultImage">
                <NButton
                  size="small"
                  :type="currentImageView === 'preview' ? 'primary' : 'default'"
                  :secondary="currentImageView !== 'preview'"
                  :strong="currentImageView === 'preview'"
                  @click="handleSetImageView('preview')"
                >
                  预览图
                </NButton>
                <NButton
                  size="small"
                  :type="currentImageView === 'source-mask' ? 'primary' : 'default'"
                  :secondary="currentImageView !== 'source-mask'"
                  :strong="currentImageView === 'source-mask'"
                  @click="handleSetImageView('source-mask')"
                >
                  颜色源掩码
                </NButton>
              </NButtonGroup>
              <NButton size="tiny" quaternary @click="panZoomGroups.resetAll">重置视图</NButton>
            </NSpace>
          </template>
          <ZoomableImageViewport
            :src="currentImageUrl"
            alt="result preview"
            :height="420"
            :controller="panZoomGroups.controllerFor('current')"
          />
          <NText v-if="currentImageLoading" depth="3" class="layer-preview-loading">
            图像加载中...
          </NText>
        </NCard>

        <NCard v-if="hasLayerPreviews" title="分层预览" size="small" embedded>
          <template #header-extra>
            <NText depth="3" class="layer-preview-title">{{ currentLayerTitle }}</NText>
          </template>

          <div class="layer-preview-body">
            <div class="layer-preview-body__image">
              <ZoomableImageViewport
                :src="currentLayerImageUrl"
                alt="layer preview"
                :height="420"
                :controller="panZoomGroups.controllerFor('layer')"
              />
            </div>

            <div class="layer-preview-body__slider">
              <NText depth="3" class="layer-preview-slider-label">Top</NText>
              <NSlider
                :value="currentTopLayerPosition"
                vertical
                :reverse="true"
                :min="1"
                :max="availableLayerCount"
                :step="1"
                class="layer-preview-slider"
                @update:value="handleLayerSliderUpdate"
              />
              <NText depth="3" class="layer-preview-slider-label">Bottom</NText>
            </div>
          </div>

          <NText v-if="isLayerImageLoading" depth="3" class="layer-preview-loading">
            图层加载中...
          </NText>
          <NText depth="3" class="layer-preview-meta">{{ currentLayerMeta }}</NText>
        </NCard>
      </div>

      <!-- Download button & dimensions info -->
      <NSpace v-if="result.has_3mf" align="center">
        <NButton type="primary" @click="handleDownload3MF"> 下载 3MF 文件 </NButton>
        <NText depth="3" style="font-size: 12px">
          {{ result.input_width }}×{{ result.input_height }} px
          <template v-if="result.physical_width_mm > 0">
            | {{ result.physical_width_mm.toFixed(1) }}×{{ result.physical_height_mm.toFixed(1) }}
            mm
          </template>
          <template v-if="result.resolved_pixel_mm > 0">
            | 像素 {{ result.resolved_pixel_mm.toFixed(2) }} mm
          </template>
        </NText>
      </NSpace>

      <!-- Match statistics -->
      <NDescriptions label-placement="left" bordered :column="2" size="small" title="匹配统计">
        <NDescriptionsItem label="聚类总数">
          {{ result.stats.clusters_total }}
        </NDescriptionsItem>
        <NDescriptionsItem label="数据库匹配">
          {{ result.stats.db_only }}
        </NDescriptionsItem>
        <NDescriptionsItem label="模型回退">
          {{ result.stats.model_fallback }}
        </NDescriptionsItem>
        <NDescriptionsItem label="模型查询">
          {{ result.stats.model_queries }}
        </NDescriptionsItem>
        <NDescriptionsItem label="数据库平均色差">
          {{ result.stats.avg_db_de.toFixed(2) }}
        </NDescriptionsItem>
        <NDescriptionsItem label="模型平均色差">
          {{ result.stats.avg_model_de.toFixed(2) }}
        </NDescriptionsItem>
      </NDescriptions>
    </NSpace>
  </NCard>
</template>

<style scoped>
.result-header-tip {
  font-size: 11px;
}

.linkage-toolbar__label {
  font-size: 12px;
}

.linkage-custom-grid {
  display: grid;
  grid-template-columns: auto minmax(0, 180px);
  gap: 8px 10px;
  align-items: center;
}

.linkage-custom-grid__label {
  font-size: 12px;
}

.result-images-layout {
  display: grid;
  gap: 12px;
  grid-template-columns: repeat(2, minmax(0, 1fr));
}

.result-images-layout--single {
  grid-template-columns: 1fr;
}

.layer-preview-title {
  font-size: 12px;
}

.layer-preview-body {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 60px;
  gap: 12px;
  align-items: stretch;
}

.layer-preview-body__image {
  min-width: 0;
}

.layer-preview-body__slider {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: space-between;
}

.layer-preview-slider {
  height: 330px;
}

.layer-preview-slider-label {
  font-size: 12px;
}

.layer-preview-loading {
  display: block;
  margin-top: 8px;
  font-size: 12px;
}

.layer-preview-meta {
  display: block;
  margin-top: 10px;
  font-size: 12px;
}

@media (max-width: 960px) {
  .result-images-layout {
    grid-template-columns: 1fr;
  }
}
</style>
