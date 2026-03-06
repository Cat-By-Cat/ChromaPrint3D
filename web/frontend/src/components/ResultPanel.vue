<script setup lang="ts">
import { computed, onUnmounted, ref, watch } from 'vue'
import { storeToRefs } from 'pinia'
import {
  NCard,
  NButton,
  NButtonGroup,
  NDescriptions,
  NDescriptionsItem,
  NSlider,
  NSpace,
  NText,
} from 'naive-ui'
import { getLayerPreviewUrl, getPreviewUrl, getSourceMaskUrl, getResultUrl } from '../api'
import { useBlobDownload } from '../composables/useBlobDownload'
import { useObjectUrlLifecycle } from '../composables/useObjectUrlLifecycle'
import { usePanZoom } from '../composables/usePanZoom'
import {
  getAvailableLayerCount,
  getLayerArtifactKeyFromTop,
  getTopLayerPosition,
} from '../domain/result/layerPreview'
import { mergeSessionHeader } from '../runtime/session'
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
  if (!currentImageView.value || !taskId.value) return ''
  return currentImageView.value === 'source-mask' ? getSourceMaskUrl(taskId.value) : getPreviewUrl(taskId.value)
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

const {
  previewTransform,
  handleWheel,
  handleMouseDown,
  handleMouseMove,
  handleMouseUp,
  resetView,
} = usePanZoom()

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
      const response = await fetch(getLayerPreviewUrl(requestTaskId, artifactKey), {
        credentials: 'include',
        headers: mergeSessionHeader(),
      })
      if (!response.ok) return null
      const blob = await response.blob()
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
  clearLayerImageCache()
})

watch(
  () => taskId.value,
  () => {
    // 每次新任务结果默认回到预览图，层位重置到顶部层。
    activeImageView.value = 'preview'
    sliderTopLayerPosition.value = 1
    clearLayerImageCache()
    resetView()
  },
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
  const url = getResultUrl(taskId.value)
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
              <NButton size="tiny" quaternary @click="resetView">重置视图</NButton>
            </NSpace>
          </template>
          <div
            class="preview-viewport"
            @wheel="handleWheel"
            @mousedown="handleMouseDown"
            @mousemove="handleMouseMove"
            @mouseup="handleMouseUp"
            @mouseleave="handleMouseUp"
          >
            <img
              :src="currentImageUrl"
              class="preview-img"
              :style="{ transform: previewTransform }"
              draggable="false"
              alt="result preview"
            />
          </div>
        </NCard>

        <NCard v-if="hasLayerPreviews" title="分层预览" size="small" embedded>
          <template #header-extra>
            <NText depth="3" class="layer-preview-title">{{ currentLayerTitle }}</NText>
          </template>

          <div class="layer-preview-body">
            <div class="layer-preview-body__image">
              <div
                class="preview-viewport"
                @wheel="handleWheel"
                @mousedown="handleMouseDown"
                @mousemove="handleMouseMove"
                @mouseup="handleMouseUp"
                @mouseleave="handleMouseUp"
              >
                <img
                  :src="currentLayerImageUrl"
                  class="preview-img"
                  :style="{ transform: previewTransform }"
                  draggable="false"
                  alt="layer preview"
                />
              </div>
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

.preview-viewport {
  position: relative;
  width: 100%;
  height: 420px;
  overflow: hidden;
  border: 1px solid var(--n-border-color);
  border-radius: 4px;
  cursor: grab;
  user-select: none;
  background-color: #fff;
  background-image:
    linear-gradient(45deg, #e8e8e8 25%, transparent 25%, transparent 75%, #e8e8e8 75%),
    linear-gradient(45deg, #e8e8e8 25%, transparent 25%, transparent 75%, #e8e8e8 75%);
  background-size: 16px 16px;
  background-position: 0 0, 8px 8px;
}

.preview-viewport:active {
  cursor: grabbing;
}

.preview-img {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  object-fit: contain;
  transform-origin: 0 0;
  pointer-events: none;
}

@media (max-width: 960px) {
  .result-images-layout {
    grid-template-columns: 1fr;
  }
}
</style>
