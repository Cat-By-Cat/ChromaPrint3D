<script setup lang="ts">
import { ref, onMounted, computed, watch } from 'vue'
import {
  NCard,
  NSpace,
  NButton,
  NButtonGroup,
  NSelect,
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
  getMattingMaskUrl,
  getMattingForegroundUrl,
} from '../api'
import { useAsyncTask } from '../composables/useAsyncTask'
import { usePanZoom } from '../composables/usePanZoom'
import { useObjectUrlLifecycle } from '../composables/useObjectUrlLifecycle'
import { useBlobDownload } from '../composables/useBlobDownload'
import type { MattingMethodInfo, MattingTaskStatus } from '../types'
import { useAppStore } from '../stores/app'
import {
  BACKEND_MAX_IMAGE_PIXELS,
  BACKEND_MAX_UPLOAD_MB,
  RASTER_IMAGE_ACCEPT,
  RASTER_IMAGE_FORMATS_TEXT,
  validateImageUploadFile,
} from '../domain/upload/imageUploadValidation'

// ── File state ───────────────────────────────────────────────────────────

const file = ref<File | null>(null)
const fileList = ref<UploadFileInfo[]>([])
const originalUrl = ref<string | null>(null)
const foregroundBlobUrl = ref<string | null>(null)
const foregroundBlob = ref<Blob | null>(null)
const { createUrl, revokeUrl } = useObjectUrlLifecycle()
const appStore = useAppStore()
const maxPixelText = BACKEND_MAX_IMAGE_PIXELS.toLocaleString('zh-CN')

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
    onCompleted() {
      fetchForegroundBlob(taskId.value!)
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
  downloadBlob(getMattingMaskUrl(taskId.value), `${fileBaseName.value}_mask.png`)
}

async function handleDownloadForeground() {
  if (!foregroundBlobUrl.value) return
  try {
    await downloadByObjectUrl(foregroundBlobUrl.value, `${fileBaseName.value}_foreground.png`)
  } catch {
    // handled by useBlobDownload callback
  }
}

function handleUseForegroundForConvert() {
  if (!foregroundBlob.value) return
  const resultFile = new File([foregroundBlob.value], `${fileBaseName.value}_foreground.png`, {
    type: foregroundBlob.value.type || 'image/png',
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
                  后端限制：文件最大 {{ BACKEND_MAX_UPLOAD_MB }}MB，位图最大 {{ maxPixelText }} 像素
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
            <NButtonGroup v-if="isCompleted && foregroundBlobUrl" style="width: 100%">
              <NButton style="flex: 1" @click="handleDownloadMask"> 下载 Mask </NButton>
              <NButton style="flex: 1" @click="handleDownloadForeground"> 下载前景 </NButton>
            </NButtonGroup>
            <NButton v-if="isCompleted && foregroundBlob" type="success" block @click="handleUseForegroundForConvert">
              使用前景结果进行图像转换
            </NButton>
          </NSpace>
        </NCard>
      </NGridItem>
    </NGrid>

    <NAlert v-if="error" type="error" closable @close="error = null">
      {{ error }}
    </NAlert>

    <div v-if="loading" style="text-align: center; padding: 40px 0">
      <NSpin size="large" />
      <NText depth="3" style="display: block; margin-top: 12px">
        {{ taskStatus?.status === 'pending' ? '排队等待中...' : '正在进行抠图处理...' }}
      </NText>
    </div>

    <NCard v-if="isCompleted && foregroundBlobUrl" title="抠图结果" size="small">
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
          <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
            原图
          </NText>
          <div
            class="preview-viewport"
            @wheel="handleWheel"
            @mousedown="handleMouseDown"
            @mousemove="handleMouseMove"
            @mouseup="handleMouseUp"
            @mouseleave="handleMouseUp"
          >
            <img
              :src="originalUrl ?? undefined"
              class="preview-img"
              :style="{ transform: previewTransform }"
              draggable="false"
              alt="original"
            />
          </div>
        </div>
        <div class="preview-col">
          <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
            抠图结果
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
              :src="foregroundBlobUrl ?? undefined"
              class="preview-img"
              :style="{ transform: previewTransform }"
              draggable="false"
              alt="foreground"
            />
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

@media (max-width: 768px) {
  .preview-row {
    flex-direction: column;
  }
}
</style>
