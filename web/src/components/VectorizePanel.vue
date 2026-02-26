<script setup lang="ts">
import { ref, computed, watch, onUnmounted } from 'vue'
import {
  NCard,
  NSpace,
  NButton,
  NUpload,
  NUploadDragger,
  NText,
  NAlert,
  NSpin,
  NGrid,
  NGridItem,
  NInputNumber,
  NSwitch,
  NCollapse,
  NCollapseItem,
} from 'naive-ui'
import type { UploadFileInfo } from 'naive-ui'
import {
  submitVectorize,
  fetchVectorizeTaskStatus,
  getVectorizeSvgUrl,
} from '../api'
import { useAsyncTask } from '../composables/useAsyncTask'
import type { VectorizeParams, VectorizeTaskStatus } from '../types'

// ── File state ───────────────────────────────────────────────────────────

const file = ref<File | null>(null)
const fileList = ref<UploadFileInfo[]>([])
const originalUrl = ref<string | null>(null)

// ── Parameters ───────────────────────────────────────────────────────────

const params = ref<VectorizeParams>({
  num_colors: 32,
  merge_lambda: 500,
  min_region_area: 10,
  alpha_max: 1.0,
  enable_curve_opt: true,
  opt_tolerance: 0.2,
  morph_kernel_size: 3,
})

// ── Task management ─────────────────────────────────────────────────────

const svgBlobUrl = ref<string | null>(null)
const svgContent = ref<string | null>(null)

const {
  taskId,
  status: taskStatus,
  loading,
  error,
  isCompleted,
  submit: submitTask,
  reset: resetTask,
} = useAsyncTask<VectorizeTaskStatus>(
  () => submitVectorize(file.value!, params.value),
  fetchVectorizeTaskStatus,
  {
    onCompleted() {
      fetchSvgBlob(taskId.value!)
    },
  },
)

const canExecute = computed(() => file.value !== null && !loading.value)

async function handleVectorize() {
  if (!file.value) return
  revokeBlobUrls()
  svgContent.value = null
  resetView()
  await submitTask()
}

async function fetchSvgBlob(id: string) {
  try {
    const res = await fetch(getVectorizeSvgUrl(id), { credentials: 'include' })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    if (taskId.value !== id) return
    const text = await res.text()
    if (taskId.value !== id) return
    svgContent.value = text
    const blob = new Blob([text], { type: 'image/svg+xml' })
    svgBlobUrl.value = URL.createObjectURL(blob)
  } catch (e: unknown) {
    if (taskId.value !== id) return
    error.value = e instanceof Error ? e.message : '获取结果失败'
  }
}

// ── Drag & zoom ──────────────────────────────────────────────────────────

const scale = ref(1)
const translateX = ref(0)
const translateY = ref(0)
const dragging = ref(false)
const lastMouse = ref({ x: 0, y: 0 })

const previewTransform = computed(
  () => `translate(${translateX.value}px, ${translateY.value}px) scale(${scale.value})`,
)

function handleWheel(e: WheelEvent) {
  e.preventDefault()
  const factor = e.deltaY < 0 ? 1.1 : 0.9
  const newScale = Math.max(0.1, Math.min(20, scale.value * factor))
  const ratio = newScale / scale.value

  const rect = (e.currentTarget as HTMLElement).getBoundingClientRect()
  const mx = e.clientX - rect.left
  const my = e.clientY - rect.top

  translateX.value = mx - (mx - translateX.value) * ratio
  translateY.value = my - (my - translateY.value) * ratio
  scale.value = newScale
}

function handleMouseDown(e: MouseEvent) {
  if (e.button !== 0) return
  dragging.value = true
  lastMouse.value = { x: e.clientX, y: e.clientY }
}

function handleMouseMove(e: MouseEvent) {
  if (!dragging.value) return
  translateX.value += e.clientX - lastMouse.value.x
  translateY.value += e.clientY - lastMouse.value.y
  lastMouse.value = { x: e.clientX, y: e.clientY }
}

function handleMouseUp() {
  dragging.value = false
}

function resetView() {
  scale.value = 1
  translateX.value = 0
  translateY.value = 0
}

// ── File management ──────────────────────────────────────────────────────

function handleUploadChange(options: { fileList: UploadFileInfo[] }) {
  const files = options.fileList
  if (files.length === 0) {
    clearFile()
    return
  }
  const latest = files[files.length - 1]
  if (latest?.file) {
    file.value = latest.file
  }
}

function revokeBlobUrls() {
  if (svgBlobUrl.value) {
    URL.revokeObjectURL(svgBlobUrl.value)
    svgBlobUrl.value = null
  }
}

function clearFile() {
  file.value = null
  fileList.value = []
  resetTask()
  revokeBlobUrls()
  svgContent.value = null
  if (originalUrl.value) {
    URL.revokeObjectURL(originalUrl.value)
    originalUrl.value = null
  }
  resetView()
}

watch(file, (f) => {
  if (originalUrl.value) {
    URL.revokeObjectURL(originalUrl.value)
    originalUrl.value = null
  }
  if (f) {
    originalUrl.value = URL.createObjectURL(f)
  }
  resetTask()
  revokeBlobUrls()
  svgContent.value = null
  resetView()
})

// ── Downloads ────────────────────────────────────────────────────────────

function handleDownloadSvg() {
  if (!svgBlobUrl.value) return
  const a = document.createElement('a')
  a.href = svgBlobUrl.value
  a.download = `${fileBaseName.value}.svg`
  a.click()
}

// ── Computed helpers ─────────────────────────────────────────────────────

const imageInfo = computed(() => {
  if (!file.value) return null
  const sizeMB = (file.value.size / 1024 / 1024).toFixed(2)
  return `${file.value.name} (${sizeMB} MB)`
})

const fileBaseName = computed(() => {
  if (!file.value) return 'image'
  const name = file.value.name
  const dot = name.lastIndexOf('.')
  return dot > 0 ? name.substring(0, dot) : name
})

const timingText = computed(() => {
  const t = taskStatus.value?.timing
  if (!t) return null
  const parts: string[] = []
  if (t.decode_ms > 0) parts.push(`解码 ${t.decode_ms.toFixed(0)}ms`)
  if (t.vectorize_ms > 0) parts.push(`矢量化 ${t.vectorize_ms.toFixed(0)}ms`)
  parts.push(`总计 ${t.pipeline_ms.toFixed(0)}ms`)
  return parts.join(' | ')
})

const svgSizeText = computed(() => {
  const size = taskStatus.value?.svg_size
  if (!size) return null
  if (size < 1024) return `${size} B`
  if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KB`
  return `${(size / 1024 / 1024).toFixed(2)} MB`
})

// ── Lifecycle ────────────────────────────────────────────────────────────

onUnmounted(() => {
  if (originalUrl.value) {
    URL.revokeObjectURL(originalUrl.value)
  }
  revokeBlobUrls()
})
</script>

<template>
  <NSpace vertical :size="16">
    <NGrid :cols="2" :x-gap="16" responsive="screen" item-responsive>
      <NGridItem span="2 m:1">
        <NCard title="图片上传" size="small">
          <NUpload
            v-if="!file"
            accept="image/*"
            :max="1"
            :default-upload="false"
            :file-list="fileList"
            :show-file-list="false"
            @update:file-list="(v: UploadFileInfo[]) => { fileList = v }"
            @change="handleUploadChange"
          >
            <NUploadDragger>
              <NSpace vertical align="center" justify="center" style="padding: 32px 16px">
                <NText depth="3" style="font-size: 14px">
                  点击或拖拽图片到此处上传
                </NText>
                <NText depth="3" style="font-size: 12px">
                  支持 JPG / PNG / BMP / TIFF 格式
                </NText>
              </NSpace>
            </NUploadDragger>
          </NUpload>
          <div v-else class="upload-preview">
            <div class="upload-preview-header">
              <NText depth="3" style="font-size: 12px">{{ imageInfo }}</NText>
              <NButton size="tiny" quaternary type="error" @click="clearFile">
                移除图片
              </NButton>
            </div>
            <img
              :src="originalUrl ?? undefined"
              class="upload-thumbnail"
              alt="original"
            />
          </div>
        </NCard>
      </NGridItem>

      <NGridItem span="2 m:1">
        <NCard title="矢量化设置" size="small">
          <NSpace vertical :size="12">
            <div>
              <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                颜色数量
              </NText>
              <NInputNumber
                v-model:value="params.num_colors"
                :min="2"
                :max="256"
                :disabled="loading"
                size="small"
                style="width: 100%"
              />
            </div>
            <div>
              <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                合并强度（值越大合并越激进）
              </NText>
              <NInputNumber
                v-model:value="params.merge_lambda"
                :min="0"
                :max="10000"
                :step="100"
                :disabled="loading"
                size="small"
                style="width: 100%"
              />
            </div>
            <NCollapse>
              <NCollapseItem title="高级参数" name="advanced">
                <NSpace vertical :size="10">
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      最小区域面积（像素²）
                    </NText>
                    <NInputNumber
                      v-model:value="params.min_region_area"
                      :min="1"
                      :max="1000"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      拐角阈值 alpha_max（0=全角，>1.33=全平滑）
                    </NText>
                    <NInputNumber
                      v-model:value="params.alpha_max"
                      :min="0"
                      :max="2"
                      :step="0.1"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      形态学核大小（0=禁用）
                    </NText>
                    <NInputNumber
                      v-model:value="params.morph_kernel_size"
                      :min="0"
                      :max="11"
                      :step="2"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      曲线优化容差（像素）
                    </NText>
                    <NInputNumber
                      v-model:value="params.opt_tolerance"
                      :min="0"
                      :max="5"
                      :step="0.1"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                  </div>
                  <div style="display: flex; align-items: center; gap: 8px">
                    <NText depth="3" style="font-size: 12px">启用曲线优化</NText>
                    <NSwitch v-model:value="params.enable_curve_opt" :disabled="loading" />
                  </div>
                </NSpace>
              </NCollapseItem>
            </NCollapse>
            <NButton
              type="primary"
              block
              :loading="loading"
              :disabled="!canExecute"
              @click="handleVectorize"
            >
              {{ loading ? '处理中...' : '开始矢量化' }}
            </NButton>
            <NButton
              v-if="isCompleted && svgBlobUrl"
              block
              @click="handleDownloadSvg"
            >
              下载 SVG{{ svgSizeText ? ` (${svgSizeText})` : '' }}
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
        {{ taskStatus?.status === 'pending' ? '排队等待中...' : '正在进行矢量化处理...' }}
      </NText>
    </div>

    <NCard v-if="isCompleted && svgBlobUrl" title="矢量化结果" size="small">
      <template #header-extra>
        <NSpace :size="8" align="center">
          <NText depth="3" style="font-size: 12px">
            {{ taskStatus!.width }} x {{ taskStatus!.height }} |
            {{ taskStatus!.num_shapes }} 个形状 |
            SVG {{ svgSizeText }}
          </NText>
          <NButton size="tiny" quaternary @click="resetView">
            重置视图
          </NButton>
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
        滚轮缩放，拖拽移动，可对比矢量化效果
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
            SVG 结果
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
              :src="svgBlobUrl ?? undefined"
              class="preview-img"
              :style="{ transform: previewTransform }"
              draggable="false"
              alt="svg result"
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
  border: 1px solid #e0e0e6;
  border-radius: 4px;
  cursor: grab;
  user-select: none;
  background: #fafafa;
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

@media (max-width: 768px) {
  .preview-row {
    flex-direction: column;
  }
}
</style>
