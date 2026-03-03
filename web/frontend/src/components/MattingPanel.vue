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
} from 'naive-ui'
import type { UploadFileInfo, SelectOption } from 'naive-ui'
import {
  fetchMattingMethods,
  submitMatting,
  fetchMattingTaskStatus,
  postprocessMatting,
  getMattingMaskUrl,
  getMattingForegroundUrl,
  getMattingAlphaUrl,
  getMattingProcessedForegroundUrl,
  getMattingProcessedMaskUrl,
  getMattingOutlineUrl,
} from '../api'
import { useAsyncTask } from '../composables/useAsyncTask'
import { usePanZoom } from '../composables/usePanZoom'
import { useObjectUrlLifecycle } from '../composables/useObjectUrlLifecycle'
import { useBlobDownload } from '../composables/useBlobDownload'
import type { MattingMethodInfo, MattingTaskStatus, OutlineMode } from '../types'
import { useAppStore } from '../stores/app'
import {
  RASTER_IMAGE_ACCEPT,
  RASTER_IMAGE_FORMATS_TEXT,
  validateImageUploadFile,
} from '../domain/upload/imageUploadValidation'
import { getUploadMaxMb, getUploadMaxPixels } from '../runtime/env'

// ── File state ───────────────────────────────────────────────────────────

const file = ref<File | null>(null)
const fileList = ref<UploadFileInfo[]>([])
const originalUrl = ref<string | null>(null)
const foregroundBlobUrl = ref<string | null>(null)
const foregroundBlob = ref<Blob | null>(null)
const { createUrl, revokeUrl } = useObjectUrlLifecycle()
const appStore = useAppStore()
const backendMaxUploadMb = getUploadMaxMb()
const maxPixelText = getUploadMaxPixels().toLocaleString('zh-CN')

// ── Postprocess state ────────────────────────────────────────────────────

const threshold = ref(0.5)
const morphCloseSize = ref(0)
const morphCloseIterations = ref(1)
const minRegionArea = ref(0)
const outlineEnabled = ref(false)
const outlineWidth = ref(2)
const outlineColor = ref('#FFFFFF')
const outlineMode = ref<OutlineMode>('center')

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
  resetView()
  await submitTask()
}

async function fetchForegroundBlob(id: string) {
  try {
    const res = await fetch(getMattingForegroundUrl(id), { credentials: 'include' })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    if (taskId.value !== id) return
    const blob = await res.blob()
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
    const res = await fetch(getMattingAlphaUrl(id), { credentials: 'include' })
    if (!res.ok) return
    if (taskId.value !== id) return
    const blob = await res.blob()
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

    const fgRes = await fetch(getMattingProcessedForegroundUrl(id), { credentials: 'include' })
    if (!fgRes.ok) throw new Error(`HTTP ${fgRes.status}`)
    if (taskId.value !== id) return
    const fgBlob = await fgRes.blob()
    if (taskId.value !== id) return
    revokeUrl(processedFgBlobUrl.value)
    processedFgBlob.value = fgBlob
    processedFgBlobUrl.value = createUrl(fgBlob)

    revokeUrl(compositedFgBlobUrl.value)
    compositedFgBlobUrl.value = null
    compositedFgBlob.value = null

    if (result.artifacts.includes('outline')) {
      const olRes = await fetch(getMattingOutlineUrl(id), { credentials: 'include' })
      if (olRes.ok && taskId.value === id) {
        const olBlob = await olRes.blob()
        if (taskId.value === id) {
          revokeUrl(outlineBlobUrl.value)
          outlineBlobUrl.value = createUrl(olBlob)

          const cBlob = await compositeOnCanvas(processedFgBlobUrl.value!, outlineBlobUrl.value)
          if (taskId.value !== id) return
          compositedFgBlob.value = cBlob
          compositedFgBlobUrl.value = createUrl(cBlob)
        }
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

watch(threshold, () => {
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

// ── Drag & zoom ──────────────────────────────────────────────────────────

const {
  previewTransform,
  handleWheel,
  handleMouseDown,
  handleMouseMove,
  handleMouseUp,
  resetView,
} = usePanZoom()

// ── File management ──────────────────────────────────────────────────────

async function handleUploadChange(options: { fileList: UploadFileInfo[] }) {
  const files = options.fileList
  if (files.length === 0) {
    clearFile()
    return
  }
  const latest = files[files.length - 1]
  if (latest?.file) {
    const validation = await validateImageUploadFile(latest.file, 'raster-tool')
    if (!validation.ok) {
      clearFile()
      error.value = validation.message
      return
    }
    error.value = null
    file.value = latest.file
  }
}

function revokeBlobUrls() {
  revokeUrl(foregroundBlobUrl.value)
  foregroundBlobUrl.value = null
  foregroundBlob.value = null
  resetPostprocessState()
}

function clearFile() {
  file.value = null
  fileList.value = []
  resetTask()
  revokeBlobUrls()
  revokeUrl(originalUrl.value)
  originalUrl.value = null
  resetView()
}

watch(file, (f) => {
  revokeUrl(originalUrl.value)
  originalUrl.value = null
  if (f) {
    originalUrl.value = createUrl(f)
  }
  resetTask()
  revokeBlobUrls()
  resetView()
})

// ── Downloads ────────────────────────────────────────────────────────────

async function downloadBlob(url: string, filename: string) {
  try {
    await downloadByUrl(url, filename)
  } catch {
    error.value = '下载失败，请重试'
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
    ? getMattingProcessedMaskUrl(taskId.value)
    : getMattingMaskUrl(taskId.value)
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

const imageInfo = computed(() => {
  if (!file.value) return null
  const sizeMB = (file.value.size / 1024 / 1024).toFixed(2)
  return `${file.value.name} (${sizeMB} MB)`
})

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
    <NGrid :cols="2" :x-gap="16" responsive="screen" item-responsive>
      <NGridItem span="2 m:1">
        <NCard title="图片上传" size="small">
          <NUpload
            v-if="!file"
            :accept="RASTER_IMAGE_ACCEPT"
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
                  支持 {{ RASTER_IMAGE_FORMATS_TEXT }} 格式
                </NText>
                <NText depth="3" style="font-size: 11px">
                  后端限制：文件最大 {{ backendMaxUploadMb }}MB，位图最大 {{ maxPixelText }} 像素
                </NText>
              </NSpace>
            </NUploadDragger>
          </NUpload>
          <div v-else class="upload-preview">
            <div class="upload-preview-header">
              <NText depth="3" style="font-size: 12px">{{ imageInfo }}</NText>
              <NButton size="tiny" quaternary type="error" @click="clearFile"> 移除图片 </NButton>
            </div>
            <img :src="originalUrl ?? undefined" class="upload-thumbnail" alt="original" />
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
              class="slider-input-row__slider"
            />
            <NInputNumber
              v-model:value="threshold"
              :min="0"
              :max="1"
              :step="0.01"
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
              <NText depth="3" style="font-size: 12px; white-space: nowrap"> 线宽 </NText>
              <NSlider
                v-model:value="outlineWidth"
                :min="1"
                :max="10"
                :step="1"
                class="slider-input-row__slider"
              />
              <NInputNumber
                v-model:value="outlineWidth"
                :min="1"
                :max="10"
                :step="1"
                :show-button="false"
                class="slider-input-row__input"
              />
            </div>
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
          <NButton size="tiny" quaternary @click="resetView"> 重置视图 </NButton>
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
          <div
            class="preview-viewport"
            @wheel="handleWheel"
            @mousedown="handleMouseDown"
            @mousemove="handleMouseMove"
            @mouseup="handleMouseUp"
            @mouseleave="handleMouseUp"
          >
            <img
              :src="leftPreviewUrl ?? undefined"
              class="preview-img"
              :style="{ transform: previewTransform }"
              draggable="false"
              :alt="leftViewMode"
            />
          </div>
        </div>
        <div class="preview-col">
          <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
            {{ processedFgBlobUrl ? '后处理结果' : thresholdPreviewUrl ? '阈值预览' : '抠图结果' }}
          </NText>
          <div
            class="preview-viewport checkerboard"
            @wheel="handleWheel"
            @mousedown="handleMouseDown"
            @mousemove="handleMouseMove"
            @mouseup="handleMouseUp"
            @mouseleave="handleMouseUp"
          >
            <img
              :src="currentForegroundUrl ?? undefined"
              class="preview-img"
              :style="{ transform: previewTransform }"
              draggable="false"
              alt="foreground"
            />
            <img
              v-if="outlineBlobUrl"
              :src="outlineBlobUrl"
              class="preview-img"
              :style="{ transform: previewTransform }"
              draggable="false"
              alt="outline"
            />
            <div v-if="postprocessing" class="preview-loading">
              <NSpin :size="20" />
            </div>
          </div>
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

.upload-thumbnail {
  max-width: 100%;
  max-height: 300px;
  object-fit: contain;
  border-radius: 4px;
}

.preview-row {
  display: flex;
  gap: 12px;
}

.preview-col {
  flex: 1;
  min-width: 0;
}

.preview-viewport {
  position: relative;
  width: 100%;
  height: 400px;
  overflow: hidden;
  border: 1px solid var(--n-border-color);
  border-radius: 4px;
  cursor: grab;
  user-select: none;
  background: var(--n-body-color);
}

.preview-viewport:active {
  cursor: grabbing;
}

.checkerboard {
  background-image: repeating-conic-gradient(
    var(--n-border-color) 0% 25%,
    var(--n-card-color) 0% 50%
  );
  background-size: 16px 16px;
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
