<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed, watch } from 'vue'
import {
  NCard,
  NSpace,
  NButton,
  NButtonGroup,
  NSelect,
  NSlider,
  NInputNumber,
  NSwitch,
  NColorPicker,
  NUpload,
  NUploadDragger,
  NText,
  NAlert,
  NSpin,
  NGrid,
  NGridItem,
  NProgress,
} from 'naive-ui'
import type { SelectOption, UploadFileInfo } from 'naive-ui'
import { useAsyncTask } from '../composables/useAsyncTask'
import { usePanZoomLinkage } from '../composables/usePanZoomLinkage'
import { usePanZoomGroups } from '../composables/usePanZoomGroups'
import { useRasterToolUpload } from '../composables/useRasterToolUpload'
import { useObjectUrlLifecycle } from '../composables/useObjectUrlLifecycle'
import { useBlobDownload } from '../composables/useBlobDownload'
import ZoomableImageViewport from './common/ZoomableImageViewport.vue'
import type { MattingMethodInfo, MattingTaskStatus, OutlineMode } from '../types'
import { useAppStore } from '../stores/app'
import { useElectronMattingModels } from '../composables/useElectronMattingModels'
import { toErrorMessage } from '../runtime/error'
import { formatFloat, roundTo } from '../runtime/number'
import { fetchBlobWithSession } from '../runtime/protectedRequest'
import {
  fetchMattingMethods,
  fetchMattingTaskStatus,
  getMattingAlphaPath,
  getMattingForegroundPath,
  getMattingMaskPath,
  getMattingOutlinePath,
  getMattingProcessedForegroundPath,
  getMattingProcessedMaskPath,
  postprocessMatting,
  submitMatting,
} from '../services/mattingService'

// ── File state ───────────────────────────────────────────────────────────

const foregroundBlobUrl = ref<string | null>(null)
const foregroundBlob = ref<Blob | null>(null)
const { createUrl, revokeUrl } = useObjectUrlLifecycle()
const appStore = useAppStore()
const upload = useRasterToolUpload({
  onReset: () => {
    resetTask()
    revokeBlobUrls()
    panZoomGroups.resetAll()
  },
  onError: (message) => {
    error.value = message
  },
})
const {
  backendMaxUploadMb,
  clearFile,
  file,
  fileList,
  handleUploadChange,
  imageInfo,
  maxPixelText,
  originalUrl,
  rasterImageAccept,
  rasterImageFormatsText,
} = upload

// ── Postprocess state ────────────────────────────────────────────────────

const threshold = ref(0.5)
const morphCloseSize = ref(0)
const morphCloseIterations = ref(1)
const minRegionArea = ref(0)
const outlineEnabled = ref(false)
const outlineWidth = ref(2)
const outlineColor = ref('#FFFFFF')
const outlineMode = ref<OutlineMode>('center')
const formatThresholdTooltip = (value: number) => formatFloat(value, 2)

const outlineModeOptions: SelectOption[] = [
  { label: '居中描边', value: 'center' },
  { label: '内描边', value: 'inner' },
  { label: '外描边', value: 'outer' },
]

const postprocessing = ref(false)
const processedFgBlobUrl = ref<string | null>(null)
const processedFgBlob = ref<Blob | null>(null)
const compositedFgBlobUrl = ref<string | null>(null)
const compositedFgBlob = ref<Blob | null>(null)
const alphaBlobUrl = ref<string | null>(null)
const outlineBlobUrl = ref<string | null>(null)
const thresholdPreviewUrl = ref<string | null>(null)
const thresholdPreviewBlob = ref<Blob | null>(null)
const hasAlpha = computed(() => taskStatus.value?.has_alpha ?? false)
const needsBackendPostprocess = computed(
  () => morphCloseSize.value > 0 || minRegionArea.value > 0 || outlineEnabled.value,
)
const postprocessPending = computed(
  () => postprocessing.value || (needsBackendPostprocess.value && !processedFgBlob.value),
)
const leftViewMode = ref<'original' | 'alpha'>('original')
const leftPreviewUrl = computed(() =>
  leftViewMode.value === 'alpha' && alphaBlobUrl.value ? alphaBlobUrl.value : originalUrl.value,
)
const currentForegroundUrl = computed(
  () => processedFgBlobUrl.value ?? thresholdPreviewUrl.value ?? foregroundBlobUrl.value,
)
const currentForegroundBlob = computed(
  () => processedFgBlob.value ?? thresholdPreviewBlob.value ?? foregroundBlob.value,
)
const exportForegroundBlob = computed(
  () => compositedFgBlob.value ?? processedFgBlob.value ?? thresholdPreviewBlob.value ?? foregroundBlob.value,
)
const exportForegroundUrl = computed(
  () => compositedFgBlobUrl.value ?? processedFgBlobUrl.value ?? thresholdPreviewUrl.value ?? foregroundBlobUrl.value,
)
const OUTLINE_REFERENCE_SHORT_SIDE = 1024
const OUTLINE_MAX_EFFECTIVE_WIDTH = 96
const outlinePreviewShortSide = computed(() => {
  const width = taskStatus.value?.width ?? 0
  const height = taskStatus.value?.height ?? 0
  if (width <= 0 || height <= 0) return 0
  return Math.min(width, height)
})
const outlinePreviewScale = computed(() =>
  Math.max(1, outlinePreviewShortSide.value / OUTLINE_REFERENCE_SHORT_SIDE),
)
const outlineEffectiveWidthPreview = computed(() => {
  const scaled = Math.round(outlineWidth.value * outlinePreviewScale.value)
  return Math.min(
    OUTLINE_MAX_EFFECTIVE_WIDTH,
    Math.max(outlineWidth.value, scaled),
  )
})
const outlineAdaptiveHint = computed(() => {
  if (outlinePreviewShortSide.value <= 0) {
    return '会按图像短边自动放大，超大图可保持可见描边。'
  }
  return `当前短边 ${outlinePreviewShortSide.value}px，预估实际线宽约 ${outlineEffectiveWidthPreview.value}px（基准 ${outlineWidth.value}px）`
})

// ── Canvas threshold preview ─────────────────────────────────────────────

let alphaImageData: ImageData | null = null
let originalImageData: ImageData | null = null
let thresholdRafId: number | null = null

function loadImageData(url: string): Promise<ImageData> {
  return new Promise((resolve, reject) => {
    const img = new Image()
    img.onload = () => {
      const c = document.createElement('canvas')
      c.width = img.naturalWidth
      c.height = img.naturalHeight
      const ctx = c.getContext('2d')!
      ctx.drawImage(img, 0, 0)
      resolve(ctx.getImageData(0, 0, c.width, c.height))
    }
    img.onerror = reject
    img.src = url
  })
}

function loadImage(url: string): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const img = new Image()
    img.onload = () => resolve(img)
    img.onerror = reject
    img.src = url
  })
}

function compositeOnCanvas(fgUrl: string, overlayUrl: string): Promise<Blob> {
  return Promise.all([loadImage(fgUrl), loadImage(overlayUrl)]).then(([fg, ol]) => {
    const c = document.createElement('canvas')
    c.width = fg.naturalWidth
    c.height = fg.naturalHeight
    const ctx = c.getContext('2d')!
    ctx.drawImage(fg, 0, 0)
    ctx.drawImage(ol, 0, 0)
    return new Promise<Blob>((resolve, reject) => {
      c.toBlob((blob) => (blob ? resolve(blob) : reject(new Error('toBlob failed'))), 'image/png')
    })
  })
}

function applyCanvasThreshold(t: number) {
  if (!alphaImageData || !originalImageData) return
  const aw = alphaImageData.width
  const ah = alphaImageData.height
  if (aw !== originalImageData.width || ah !== originalImageData.height) return

  const c = document.createElement('canvas')
  c.width = aw
  c.height = ah
  const ctx = c.getContext('2d')!
  const output = ctx.createImageData(aw, ah)
  const out = output.data
  const alpha = alphaImageData.data
  const orig = originalImageData.data
  const threshByte = Math.round(t * 255)

  for (let i = 0, len = aw * ah * 4; i < len; i += 4) {
    if ((alpha[i] ?? 0) >= threshByte) {
      out[i] = orig[i] ?? 0
      out[i + 1] = orig[i + 1] ?? 0
      out[i + 2] = orig[i + 2] ?? 0
      out[i + 3] = 255
    }
  }

  ctx.putImageData(output, 0, 0)
  c.toBlob((blob) => {
    if (!blob) return
    revokeUrl(thresholdPreviewUrl.value)
    thresholdPreviewBlob.value = blob
    thresholdPreviewUrl.value = createUrl(blob)
  }, 'image/png')
}

function scheduleCanvasThreshold() {
  if (thresholdRafId !== null) cancelAnimationFrame(thresholdRafId)
  thresholdRafId = requestAnimationFrame(() => {
    thresholdRafId = null
    applyCanvasThreshold(threshold.value)
  })
}

function hexToBgr(hex: string): [number, number, number] {
  const r = parseInt(hex.slice(1, 3), 16) || 0
  const g = parseInt(hex.slice(3, 5), 16) || 0
  const b = parseInt(hex.slice(5, 7), 16) || 0
  return [b, g, r]
}

const hasEyeDropper = typeof window !== 'undefined' && 'EyeDropper' in window

async function pickColorFromScreen() {
  try {
    const eyeDropper = new (window as unknown as { EyeDropper: new () => { open(): Promise<{ sRGBHex: string }> } }).EyeDropper()
    const result = await eyeDropper.open()
    outlineColor.value = result.sRGBHex
  } catch {
    // user cancelled
  }
}

// ── Method selection ─────────────────────────────────────────────────────

const methods = ref<MattingMethodInfo[]>([])
const selectedMethod = ref<string | null>(null)
const methodOptions = computed<SelectOption[]>(() =>
  methods.value.map((m) => ({
    label: m.name,
    value: m.key,
    description: m.description,
  })),
)
const selectedMethodDescription = computed(() => {
  const m = methods.value.find((m) => m.key === selectedMethod.value)
  return m?.description || ''
})
const hasOnlyOpenCvMethod = computed(
  () => methods.value.length > 0 && methods.value.every((m) => m.key === 'opencv'),
)
const {
  bindModelProgressListener,
  checkModelConnectivity,
  clearModelProgressListener,
  currentDownloadSpeedBytesPerSec,
  downloadedSessionBytes,
  effectiveDownloadTotalBytes,
  formatBytes,
  formatConnectivityCheckedAt,
  formatSpeed,
  handleCancelModelDownload,
  handleRestartApp,
  handleStartModelDownload,
  modelActionLoading,
  modelConnectivity,
  modelConnectivityLoading,
  modelConnectivitySummary,
  modelError,
  modelProgress,
  modelProgressPercent,
  modelRunning,
  modelStatus,
  modelStatusLoading,
  pendingModelCount,
  refreshModelStatus,
  showModelCard,
  showRestartHint,
} = useElectronMattingModels(hasOnlyOpenCvMethod)

// ── Task management (composable) ─────────────────────────────────────────

const {
  taskId,
  status: taskStatus,
  loading,
  error,
  isCompleted,
  submit: submitTask,
  reset: resetTask,
} = useAsyncTask<MattingTaskStatus>(
  () => submitMatting(file.value!, selectedMethod.value!),
  fetchMattingTaskStatus,
  {
    onCompleted(status) {
      fetchForegroundBlob(taskId.value!)
      if (status.has_alpha) {
        fetchAlphaBlob(taskId.value!)
      }
    },
  },
)
const { downloadByUrl, downloadByObjectUrl } = useBlobDownload((message) => {
  error.value = message
})

const canExecute = computed(
  () => file.value !== null && selectedMethod.value !== null && !loading.value,
)

async function handleMatting() {
  if (!file.value || !selectedMethod.value) return
  revokeBlobUrls()
  panZoomGroups.resetAll()
  await submitTask()
}

async function fetchForegroundBlob(id: string) {
  try {
    const blob = await fetchBlobWithSession(getMattingForegroundPath(id))
    if (taskId.value !== id) return
    revokeUrl(foregroundBlobUrl.value)
    foregroundBlob.value = blob
    foregroundBlobUrl.value = createUrl(blob)
  } catch (e: unknown) {
    if (taskId.value !== id) return
    error.value = e instanceof Error ? e.message : '获取结果失败'
  }
}

async function fetchAlphaBlob(id: string) {
  try {
    const blob = await fetchBlobWithSession(getMattingAlphaPath(id))
    if (taskId.value !== id) return
    revokeUrl(alphaBlobUrl.value)
    alphaBlobUrl.value = createUrl(blob)
    const [aData, oData] = await Promise.all([
      loadImageData(alphaBlobUrl.value),
      originalUrl.value ? loadImageData(originalUrl.value) : Promise.resolve(null),
    ])
    if (taskId.value !== id) return
    alphaImageData = aData
    originalImageData = oData
  } catch {
    // alpha is optional, ignore errors
  }
}

// ── Postprocess logic ────────────────────────────────────────────────────

let postprocessTimer: ReturnType<typeof setTimeout> | null = null

function cancelPendingPostprocess() {
  if (postprocessTimer !== null) {
    clearTimeout(postprocessTimer)
    postprocessTimer = null
  }
}

function clearBackendResults() {
  revokeUrl(compositedFgBlobUrl.value)
  compositedFgBlobUrl.value = null
  compositedFgBlob.value = null
  revokeUrl(processedFgBlobUrl.value)
  processedFgBlobUrl.value = null
  processedFgBlob.value = null
  revokeUrl(outlineBlobUrl.value)
  outlineBlobUrl.value = null
}

async function runPostprocess() {
  const id = taskId.value
  if (!id || !isCompleted.value) return
  postprocessing.value = true
  try {
    const result = await postprocessMatting(id, {
      threshold: threshold.value,
      morph_close_size: morphCloseSize.value,
      morph_close_iterations: morphCloseIterations.value,
      min_region_area: minRegionArea.value,
      outline: {
        enabled: outlineEnabled.value,
        width: outlineWidth.value,
        color: hexToBgr(outlineColor.value),
        mode: outlineMode.value,
      },
    })
    if (taskId.value !== id) return

    const fgBlob = await fetchBlobWithSession(getMattingProcessedForegroundPath(id))
    if (taskId.value !== id) return
    revokeUrl(processedFgBlobUrl.value)
    processedFgBlob.value = fgBlob
    processedFgBlobUrl.value = createUrl(fgBlob)

    revokeUrl(compositedFgBlobUrl.value)
    compositedFgBlobUrl.value = null
    compositedFgBlob.value = null

    if (result.artifacts.includes('outline')) {
      try {
        const olBlob = await fetchBlobWithSession(getMattingOutlinePath(id))
        if (taskId.value === id) {
          revokeUrl(outlineBlobUrl.value)
          outlineBlobUrl.value = createUrl(olBlob)

          const cBlob = await compositeOnCanvas(processedFgBlobUrl.value!, outlineBlobUrl.value)
          if (taskId.value !== id) return
          compositedFgBlob.value = cBlob
          compositedFgBlobUrl.value = createUrl(cBlob)
        }
      } catch {
        // outline is optional, ignore fetch failures to keep foreground result usable
      }
    } else {
      revokeUrl(outlineBlobUrl.value)
      outlineBlobUrl.value = null
    }
  } catch (e: unknown) {
    if (taskId.value !== id) return
    error.value = e instanceof Error ? e.message : '后处理失败'
  } finally {
    postprocessing.value = false
  }
}

function schedulePostprocess() {
  cancelPendingPostprocess()
  postprocessTimer = setTimeout(runPostprocess, 300)
}

watch(threshold, (next) => {
  const rounded = roundTo(next, 2)
  if (rounded !== next) {
    threshold.value = rounded
    return
  }
  if (!taskId.value || !isCompleted.value || !hasAlpha.value) return
  clearBackendResults()
  scheduleCanvasThreshold()
  if (needsBackendPostprocess.value) {
    schedulePostprocess()
  }
})

watch(
  [morphCloseSize, morphCloseIterations, minRegionArea, outlineEnabled, outlineWidth, outlineColor, outlineMode],
  () => {
    if (!taskId.value || !isCompleted.value) return
    clearBackendResults()
    schedulePostprocess()
  },
)

onUnmounted(() => {
  cancelPendingPostprocess()
  if (thresholdRafId !== null) cancelAnimationFrame(thresholdRafId)
  clearModelProgressListener()
})

function resetPostprocessState() {
  cancelPendingPostprocess()
  if (thresholdRafId !== null) {
    cancelAnimationFrame(thresholdRafId)
    thresholdRafId = null
  }
  threshold.value = 0.5
  morphCloseSize.value = 0
  morphCloseIterations.value = 1
  minRegionArea.value = 0
  outlineEnabled.value = false
  outlineWidth.value = 2
  outlineColor.value = '#FFFFFF'
  outlineMode.value = 'center'
  revokeUrl(compositedFgBlobUrl.value)
  compositedFgBlobUrl.value = null
  compositedFgBlob.value = null
  revokeUrl(processedFgBlobUrl.value)
  processedFgBlobUrl.value = null
  processedFgBlob.value = null
  revokeUrl(alphaBlobUrl.value)
  alphaBlobUrl.value = null
  revokeUrl(outlineBlobUrl.value)
  outlineBlobUrl.value = null
  revokeUrl(thresholdPreviewUrl.value)
  thresholdPreviewUrl.value = null
  thresholdPreviewBlob.value = null
  alphaImageData = null
  originalImageData = null
  leftViewMode.value = 'original'
}

const panZoomGroups = usePanZoomGroups({
  upload: 'upload',
  left: 'compare',
  right: 'compare',
})

const { groupValueFor, linkageGroupOptions, linkageMode, linkageModeOptions, setViewGroup } =
  usePanZoomLinkage({
    panZoomGroups,
    linkedGroups: {
      left: 'A',
      right: 'A',
    },
    independentGroups: {
      left: 'self:left',
      right: 'self:right',
    },
  })

// ── File management ──────────────────────────────────────────────────────

function revokeBlobUrls() {
  revokeUrl(foregroundBlobUrl.value)
  foregroundBlobUrl.value = null
  foregroundBlob.value = null
  resetPostprocessState()
}

// ── Downloads ────────────────────────────────────────────────────────────

async function downloadBlob(url: string, filename: string) {
  try {
    await downloadByUrl(url, filename)
  } catch (downloadError: unknown) {
    error.value = toErrorMessage(downloadError, '下载失败，请重试')
  }
}

const fileBaseName = computed(() => {
  if (!file.value) return 'image'
  const name = file.value.name
  const dot = name.lastIndexOf('.')
  return dot > 0 ? name.substring(0, dot) : name
})

function handleDownloadMask() {
  if (!taskId.value || !isCompleted.value) return
  const url = processedFgBlobUrl.value
    ? getMattingProcessedMaskPath(taskId.value)
    : getMattingMaskPath(taskId.value)
  downloadBlob(url, `${fileBaseName.value}_mask.png`)
}

async function handleDownloadForeground() {
  const url = exportForegroundUrl.value
  if (!url) return
  try {
    await downloadByObjectUrl(url, `${fileBaseName.value}_foreground.png`)
  } catch {
    // handled by useBlobDownload callback
  }
}

function handleUseForegroundForConvert() {
  const blob = exportForegroundBlob.value
  if (!blob) return
  const resultFile = new File([blob], `${fileBaseName.value}_foreground.png`, {
    type: blob.type || 'image/png',
  })
  appStore.setSelectedFile(resultFile)
  appStore.activeTab = 'convert'
}

// ── Computed helpers ─────────────────────────────────────────────────────

const timingText = computed(() => {
  const t = taskStatus.value?.timing
  if (!t) return null
  const parts: string[] = []
  if (t.decode_ms > 0) parts.push(`解码 ${t.decode_ms.toFixed(0)}ms`)
  if (t.preprocess_ms > 0) parts.push(`预处理 ${t.preprocess_ms.toFixed(0)}ms`)
  if (t.inference_ms > 0) parts.push(`推理 ${t.inference_ms.toFixed(0)}ms`)
  if (t.postprocess_ms > 0) parts.push(`后处理 ${t.postprocess_ms.toFixed(0)}ms`)
  if (t.encode_ms > 0) parts.push(`编码 ${t.encode_ms.toFixed(0)}ms`)
  parts.push(`总计 ${t.pipeline_ms.toFixed(0)}ms`)
  return parts.join(' | ')
})

// ── Lifecycle ────────────────────────────────────────────────────────────

onMounted(async () => {
  bindModelProgressListener()
  await refreshModelStatus()
  try {
    methods.value = await fetchMattingMethods()
    const first = methods.value[0]
    if (first) {
      selectedMethod.value = first.key
    }
  } catch {
    error.value = '无法获取抠图方法列表'
  }
})
</script>

<template>
  <NSpace vertical :size="16">
    <NCard v-if="showModelCard" title="抠图模型下载（Electron）" size="small">
      <NSpace vertical :size="8">
        <NText depth="3" style="font-size: 12px">
          已安装 {{ modelStatus?.installedModels ?? 0 }} / {{ modelStatus?.totalModels ?? 0 }} 个模型
          <template v-if="pendingModelCount > 0">
            （待处理 {{ pendingModelCount }} 个，缺失 {{ modelStatus?.missingModels ?? 0 }} 个）
          </template>
        </NText>
        <NText v-if="modelConnectivitySummary" depth="3" style="font-size: 12px">
          {{ modelConnectivitySummary }}
        </NText>
        <NText v-if="modelConnectivity?.checkedAtMs" depth="3" style="font-size: 11px">
          最近检测：{{ formatConnectivityCheckedAt(modelConnectivity.checkedAtMs) }}
        </NText>
        <div v-if="modelConnectivity" class="model-connectivity-list">
          <NText
            v-for="source in modelConnectivity.sources"
            :key="`${source.name}-${source.baseUrl}`"
            depth="3"
            style="font-size: 11px; display: block"
          >
            [{{ source.ok ? '可用' : '不可用' }}] {{ source.name }} ({{ source.reachableModels }}/{{
              source.checkedModels
            }}) | {{ source.responseTimeMs }}ms | {{ source.message }}
          </NText>
        </div>
        <NText depth="3" style="font-size: 12px">
          需下载：{{ formatBytes(effectiveDownloadTotalBytes) }} | 已下载：{{
            formatBytes(downloadedSessionBytes)
          }}
          <template v-if="currentDownloadSpeedBytesPerSec">
            | 网速：{{ formatSpeed(currentDownloadSpeedBytesPerSec) }}
          </template>
        </NText>
        <NProgress
          type="line"
          :percentage="modelProgressPercent"
          :processing="modelRunning"
          :show-indicator="true"
        />
        <NText v-if="modelProgress?.message" depth="3" style="font-size: 12px">
          {{ modelProgress.message }}
        </NText>
        <NAlert v-if="showRestartHint" type="warning" :bordered="false">
          模型已下载完成，请重启应用后再使用深度学习抠图模型。
        </NAlert>
        <NAlert v-if="modelError" type="error" closable @close="modelError = null">
          {{ modelError }}
        </NAlert>
        <NSpace :size="8">
          <NButton
            type="primary"
            :loading="modelActionLoading"
            :disabled="
              modelRunning || modelStatusLoading || modelConnectivityLoading || pendingModelCount <= 0
            "
            @click="handleStartModelDownload"
          >
            下载模型
          </NButton>
          <NButton
            secondary
            :loading="modelConnectivityLoading"
            :disabled="modelRunning || modelActionLoading || modelStatusLoading"
            @click="checkModelConnectivity(true)"
          >
            下载前检查
          </NButton>
          <NButton
            v-if="modelRunning"
            type="warning"
            secondary
            :disabled="!modelRunning"
            @click="handleCancelModelDownload"
          >
            取消下载
          </NButton>
          <NButton v-if="showRestartHint" type="success" @click="handleRestartApp"> 立即重启 </NButton>
          <NButton quaternary :disabled="modelStatusLoading || modelRunning" @click="refreshModelStatus">
            刷新状态
          </NButton>
        </NSpace>
        <NText depth="3" style="font-size: 11px">
          模型文件会下载到当前用户数据目录，不随安装包分发。
        </NText>
      </NSpace>
    </NCard>

    <NGrid :cols="2" :x-gap="16" responsive="screen" item-responsive>
      <NGridItem span="2 m:1">
        <NCard title="图片上传" size="small">
          <NUpload
            v-if="!file"
            :accept="rasterImageAccept"
            :max="1"
            :default-upload="false"
            :file-list="fileList"
            :show-file-list="false"
            @update:file-list="
              (v: UploadFileInfo[]) => {
                fileList = v
              }
            "
            @change="handleUploadChange"
          >
            <NUploadDragger>
              <NSpace vertical align="center" justify="center" style="padding: 32px 16px">
                <NText depth="3" style="font-size: 14px"> 点击或拖拽图片到此处上传 </NText>
                <NText depth="3" style="font-size: 12px">
                  支持 {{ rasterImageFormatsText }} 格式
                </NText>
                <NText depth="3" style="font-size: 11px">
                  文件最大 {{ backendMaxUploadMb }}MB，位图最大 {{ maxPixelText }} 像素
                </NText>
              </NSpace>
            </NUploadDragger>
          </NUpload>
          <div v-else class="upload-preview">
            <div class="upload-preview-header">
              <NText depth="3" style="font-size: 12px">{{ imageInfo }}</NText>
              <NButton size="tiny" quaternary type="error" @click="clearFile"> 移除图片 </NButton>
            </div>
            <ZoomableImageViewport
              :src="originalUrl ?? undefined"
              alt="original"
              :height="260"
              :controller="panZoomGroups.controllerFor('upload')"
            />
          </div>
        </NCard>
      </NGridItem>

      <NGridItem span="2 m:1">
        <NCard title="抠图设置" size="small">
          <NSpace vertical :size="12">
            <div>
              <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                抠图方法
              </NText>
              <NSelect
                v-model:value="selectedMethod"
                :options="methodOptions"
                placeholder="选择抠图方法"
                :disabled="loading"
              />
              <NText
                v-if="selectedMethodDescription"
                depth="3"
                style="font-size: 11px; display: block; margin-top: 4px; line-height: 1.5"
              >
                {{ selectedMethodDescription }}
              </NText>
            </div>
            <NButton
              type="primary"
              block
              :loading="loading"
              :disabled="!canExecute"
              @click="handleMatting"
            >
              {{ loading ? '处理中...' : '开始抠图' }}
            </NButton>
            <NButtonGroup v-if="isCompleted && currentForegroundUrl" style="width: 100%">
              <NButton
                style="flex: 1"
                :disabled="postprocessPending"
                @click="handleDownloadMask"
              >
                下载 Mask
              </NButton>
              <NButton
                style="flex: 1"
                :disabled="postprocessPending"
                @click="handleDownloadForeground"
              >
                下载前景
              </NButton>
            </NButtonGroup>
            <NButton
              v-if="isCompleted && currentForegroundBlob"
              type="success"
              block
              :disabled="postprocessPending"
              @click="handleUseForegroundForConvert"
            >
              使用前景结果进行图像转换
            </NButton>
          </NSpace>
        </NCard>
      </NGridItem>
    </NGrid>

    <!-- Postprocess controls -->
    <NCard v-if="isCompleted" title="后处理参数" size="small">
      <template #header-extra>
        <NSpin v-if="postprocessing" :size="16" />
      </template>
      <NSpace vertical :size="12">
        <div v-if="hasAlpha">
          <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
            阈值
          </NText>
          <div class="slider-input-row">
            <NSlider
              v-model:value="threshold"
              :min="0"
              :max="1"
              :step="0.01"
              :tooltip="true"
              :format-tooltip="formatThresholdTooltip"
              class="slider-input-row__slider"
            />
            <NInputNumber
              v-model:value="threshold"
              :min="0"
              :max="1"
              :step="0.01"
              :precision="2"
              :show-button="false"
              class="slider-input-row__input"
            />
          </div>
        </div>
        <div>
          <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
            闭运算核大小
            <NText depth="3" style="font-size: 11px">（0 = 不执行，用于连接分离区域）</NText>
          </NText>
          <div class="slider-input-row">
            <NSlider
              v-model:value="morphCloseSize"
              :min="0"
              :max="51"
              :step="1"
              class="slider-input-row__slider"
            />
            <NInputNumber
              v-model:value="morphCloseSize"
              :min="0"
              :max="51"
              :step="1"
              :show-button="false"
              class="slider-input-row__input"
            />
          </div>
        </div>
        <div v-if="morphCloseSize > 0">
          <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
            闭运算迭代次数
            <NText depth="3" style="font-size: 11px">（多次小核迭代比单次大核更平滑）</NText>
          </NText>
          <div class="slider-input-row">
            <NSlider
              v-model:value="morphCloseIterations"
              :min="1"
              :max="10"
              :step="1"
              class="slider-input-row__slider"
            />
            <NInputNumber
              v-model:value="morphCloseIterations"
              :min="1"
              :max="10"
              :step="1"
              :show-button="false"
              class="slider-input-row__input"
            />
          </div>
        </div>
        <div>
          <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
            过滤孤立区域
            <NText depth="3" style="font-size: 11px">（0 = 不过滤，移除面积小于该值的区域）</NText>
          </NText>
          <div class="slider-input-row">
            <NSlider
              v-model:value="minRegionArea"
              :min="0"
              :max="10000"
              :step="100"
              class="slider-input-row__slider"
            />
            <NInputNumber
              v-model:value="minRegionArea"
              :min="0"
              :max="10000"
              :step="100"
              :show-button="false"
              class="slider-input-row__input"
            />
          </div>
        </div>
        <div>
          <div style="display: flex; align-items: center; gap: 8px; margin-bottom: 4px">
            <NSwitch v-model:value="outlineEnabled" size="small" />
            <NText depth="3" style="font-size: 12px"> 描边 </NText>
          </div>
          <template v-if="outlineEnabled">
            <div class="slider-input-row" style="margin-bottom: 8px">
              <NText depth="3" style="font-size: 12px; white-space: nowrap"> 模式 </NText>
              <NSelect
                v-model:value="outlineMode"
                :options="outlineModeOptions"
                size="small"
                style="width: 140px"
              />
            </div>
            <div class="slider-input-row">
              <NText depth="3" style="font-size: 12px; white-space: nowrap"> 基准线宽 </NText>
              <NSlider
                v-model:value="outlineWidth"
                :min="1"
                :max="20"
                :step="1"
                class="slider-input-row__slider"
              />
              <NInputNumber
                v-model:value="outlineWidth"
                :min="1"
                :max="20"
                :step="1"
                :show-button="false"
                class="slider-input-row__input"
              />
            </div>
            <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
              {{ outlineAdaptiveHint }}
            </NText>
            <div class="slider-input-row">
              <NText depth="3" style="font-size: 12px; white-space: nowrap"> 颜色 </NText>
              <NColorPicker
                v-model:value="outlineColor"
                :modes="['hex']"
                :show-alpha="false"
                size="small"
                style="width: 120px"
              />
              <NButton
                v-if="hasEyeDropper"
                size="small"
                quaternary
                title="从屏幕取色"
                @click="pickColorFromScreen"
              >
                <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m2 22 1-1h3l9-9"/><path d="M3 21v-3l9-9"/><path d="m15 6 3.4-3.4a2.1 2.1 0 1 1 3 3L18 9l.4.4a2.1 2.1 0 1 1-3 3l-3.8-3.8a2.1 2.1 0 1 1 3-3l.4.4Z"/></svg>
              </NButton>
            </div>
          </template>
        </div>
      </NSpace>
    </NCard>

    <NAlert v-if="error" type="error" closable @close="error = null">
      {{ error }}
    </NAlert>

    <div v-if="loading" style="text-align: center; padding: 40px 0">
      <NSpin size="large" />
      <NText depth="3" style="display: block; margin-top: 12px">
        {{ taskStatus?.status === 'pending' ? '排队等待中...' : '正在进行抠图处理...' }}
      </NText>
    </div>

    <NCard v-if="isCompleted && currentForegroundUrl" title="抠图结果" size="small">
      <template #header-extra>
        <NSpace :size="8" align="center">
          <NText depth="3" style="font-size: 12px">
            {{ taskStatus!.width }} x {{ taskStatus!.height }} | {{ taskStatus!.method }}
          </NText>
          <NButton size="tiny" quaternary @click="panZoomGroups.resetAll"> 重置视图 </NButton>
        </NSpace>
      </template>
      <NText
        v-if="timingText"
        depth="3"
        style="font-size: 11px; display: block; margin-bottom: 8px; font-family: monospace"
      >
        {{ timingText }}
      </NText>
      <NText depth="3" style="font-size: 11px; display: block; margin-bottom: 8px">
        滚轮缩放，拖拽移动，可观察抠图细节
      </NText>
      <NSpace align="center" :size="8" class="linkage-toolbar">
        <NText depth="3" class="linkage-toolbar__label">联动模式</NText>
        <NSelect
          v-model:value="linkageMode"
          size="small"
          :options="linkageModeOptions"
          style="width: 140px"
        />
      </NSpace>
      <div v-if="linkageMode === 'custom'" class="linkage-custom-grid">
        <NText depth="3" class="linkage-custom-grid__label">左侧视图</NText>
        <NSelect
          :value="groupValueFor('left')"
          size="small"
          :options="linkageGroupOptions"
          @update:value="(value) => setViewGroup('left', String(value ?? 'A'))"
        />
        <NText depth="3" class="linkage-custom-grid__label">右侧视图</NText>
        <NSelect
          :value="groupValueFor('right')"
          size="small"
          :options="linkageGroupOptions"
          @update:value="(value) => setViewGroup('right', String(value ?? 'A'))"
        />
      </div>
      <div class="preview-row">
        <div class="preview-col">
          <div style="display: flex; align-items: center; gap: 8px; margin-bottom: 4px">
            <NText depth="3" style="font-size: 12px">
              {{ leftViewMode === 'alpha' ? 'Alpha' : '原图' }}
            </NText>
            <NButtonGroup v-if="hasAlpha" size="tiny">
              <NButton
                :type="leftViewMode === 'original' ? 'primary' : 'default'"
                @click="leftViewMode = 'original'"
              >
                原图
              </NButton>
              <NButton
                :type="leftViewMode === 'alpha' ? 'primary' : 'default'"
                @click="leftViewMode = 'alpha'"
              >
                Alpha
              </NButton>
            </NButtonGroup>
          </div>
          <ZoomableImageViewport
            :src="leftPreviewUrl ?? undefined"
            :alt="leftViewMode"
            :height="400"
            :controller="panZoomGroups.controllerFor('left')"
          />
        </div>
        <div class="preview-col">
          <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
            {{ processedFgBlobUrl ? '后处理结果' : thresholdPreviewUrl ? '阈值预览' : '抠图结果' }}
          </NText>
          <ZoomableImageViewport
            :src="currentForegroundUrl ?? undefined"
            alt="foreground"
            :height="400"
            :controller="panZoomGroups.controllerFor('right')"
          >
            <template #default="{ transform }">
              <img
                v-if="outlineBlobUrl"
                :src="outlineBlobUrl"
                class="zoomable-image-viewport__layer"
                :style="{ transform }"
                draggable="false"
                alt="outline"
              />
              <div v-if="postprocessing" class="preview-loading">
                <NSpin :size="20" />
              </div>
            </template>
          </ZoomableImageViewport>
        </div>
      </div>
    </NCard>
  </NSpace>
</template>

<style scoped>
.upload-preview {
  text-align: center;
}

.upload-preview-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 8px;
}

.model-connectivity-list {
  max-height: 120px;
  overflow: auto;
  border: 1px dashed var(--n-border-color);
  border-radius: 4px;
  padding: 6px 8px;
}

.linkage-toolbar {
  margin-bottom: 8px;
}

.linkage-toolbar__label {
  font-size: 12px;
}

.linkage-custom-grid {
  display: grid;
  grid-template-columns: auto minmax(0, 180px);
  gap: 8px 10px;
  align-items: center;
  margin-bottom: 8px;
}

.linkage-custom-grid__label {
  font-size: 12px;
}

.preview-row {
  display: flex;
  gap: 12px;
}

.preview-col {
  flex: 1;
  min-width: 0;
}

.slider-input-row {
  display: flex;
  align-items: center;
  gap: 12px;
}

.slider-input-row__slider {
  flex: 1;
  min-width: 0;
}

.slider-input-row__input {
  width: 80px;
  flex-shrink: 0;
}

.preview-loading {
  position: absolute;
  top: 8px;
  right: 8px;
  background: rgba(0, 0, 0, 0.45);
  border-radius: 4px;
  padding: 4px 8px;
  z-index: 1;
}

@media (max-width: 768px) {
  .preview-row {
    flex-direction: column;
  }
}
</style>
