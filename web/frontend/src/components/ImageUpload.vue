<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { storeToRefs } from 'pinia'
import { NUpload, NUploadDragger, NCard, NText, NSpace, NButton, NTag, NAlert } from 'naive-ui'
import type { UploadFileInfo } from 'naive-ui'
import { useObjectUrlLifecycle } from '../composables/useObjectUrlLifecycle'
import { usePanZoom } from '../composables/usePanZoom'
import type { InputType } from '../types'
import { useAppStore } from '../stores/app'
import {
  CONVERT_IMAGE_ACCEPT,
  CONVERT_IMAGE_FORMATS_TEXT,
  readRasterImageDimensions,
  validateImageUploadFile,
} from '../domain/upload/imageUploadValidation'
import { getUploadMaxMb, getUploadMaxPixels } from '../runtime/env'

defineProps<{
  disabled?: boolean
}>()

const fileList = ref<UploadFileInfo[]>([])
const previewUrl = ref<string | null>(null)
const dimensions = ref<{ width: number; height: number } | null>(null)
const detectedType = ref<InputType>('raster')
const uploadError = ref<string | null>(null)
let dimensionsTaskVersion = 0
const { createUrl, revokeUrl } = useObjectUrlLifecycle()
const appStore = useAppStore()
const { selectedFile } = storeToRefs(appStore)

const {
  previewTransform,
  handleWheel,
  handleMouseDown,
  handleMouseMove,
  handleMouseUp,
  resetView,
} = usePanZoom()

function isSvgFile(file: File): boolean {
  return file.type === 'image/svg+xml' || file.name.toLowerCase().endsWith('.svg')
}

const backendMaxUploadMb = getUploadMaxMb()
const maxPixelText = getUploadMaxPixels().toLocaleString('zh-CN')

const fileInfo = computed(() => {
  const file = selectedFile.value
  if (!file) return null
  const sizeMB = (file.size / 1024 / 1024).toFixed(2)
  const dimStr = dimensions.value ? ` | ${dimensions.value.width}×${dimensions.value.height}` : ''
  return `${file.name} (${sizeMB} MB${dimStr})`
})

async function handleChange(options: { fileList: UploadFileInfo[] }) {
  const files = options.fileList
  if (files.length === 0) {
    appStore.setSelectedFile(null)
    appStore.setImageDimensions(null)
    appStore.setInputType('raster')
    dimensions.value = null
    detectedType.value = 'raster'
    uploadError.value = null
    revokeUrl(previewUrl.value)
    previewUrl.value = null
    return
  }
  const latest = files[files.length - 1]
  if (!latest) return
  const rawFile = latest.file
  if (rawFile) {
    const validation = await validateImageUploadFile(rawFile, 'convert')
    if (!validation.ok) {
      clearFile()
      uploadError.value = validation.message
      return
    }
    uploadError.value = null
    appStore.setSelectedFile(rawFile)
  }
}

function clearFile() {
  dimensionsTaskVersion += 1
  fileList.value = []
  dimensions.value = null
  detectedType.value = 'raster'
  uploadError.value = null
  appStore.setSelectedFile(null)
  appStore.setImageDimensions(null)
  appStore.setInputType('raster')
  revokeUrl(previewUrl.value)
  previewUrl.value = null
  resetView()
}

async function syncImageDimensions(file: File) {
  const currentVersion = ++dimensionsTaskVersion
  const dims = await readRasterImageDimensions(file)
  if (currentVersion !== dimensionsTaskVersion) return
  dimensions.value = dims
  appStore.setImageDimensions(dims)
}

watch(
  () => selectedFile.value,
  (newFile) => {
    dimensionsTaskVersion += 1
    revokeUrl(previewUrl.value)
    previewUrl.value = null
    resetView()
    if (newFile) {
      uploadError.value = null
      const type: InputType = isSvgFile(newFile) ? 'vector' : 'raster'
      detectedType.value = type
      appStore.setInputType(type)

      previewUrl.value = createUrl(newFile)
      if (type === 'raster') {
        void syncImageDimensions(newFile)
      } else {
        dimensions.value = null
        appStore.setImageDimensions(null)
      }
    } else {
      fileList.value = []
      detectedType.value = 'raster'
      appStore.setImageDimensions(null)
      appStore.setInputType('raster')
    }
  },
)
</script>

<template>
  <NCard title="输入文件" size="small">
    <NUpload
      v-if="!selectedFile"
      :accept="CONVERT_IMAGE_ACCEPT"
      :max="1"
      :default-upload="false"
      :disabled="disabled"
      :file-list="fileList"
      :show-file-list="false"
      @update:file-list="
        (v: UploadFileInfo[]) => {
          fileList = v
        }
      "
      @change="handleChange"
    >
      <NUploadDragger>
        <NSpace vertical align="center" justify="center" style="padding: 32px 16px">
          <NText depth="3" style="font-size: 14px"> 点击或拖拽文件到此处上传 </NText>
          <NText depth="3" style="font-size: 12px"> 支持 {{ CONVERT_IMAGE_FORMATS_TEXT }} 格式 </NText>
          <NText depth="3" style="font-size: 11px">
            后端限制：文件最大 {{ backendMaxUploadMb }}MB，位图最大 {{ maxPixelText }} 像素
          </NText>
        </NSpace>
      </NUploadDragger>
    </NUpload>
    <NAlert v-if="uploadError" type="error" style="margin-bottom: 8px">
      {{ uploadError }}
    </NAlert>

    <div v-if="selectedFile">
      <div class="upload-preview-header">
        <NSpace align="center" :size="8">
          <NText depth="3" style="font-size: 12px">
            {{ fileInfo }}
          </NText>
          <NTag
            size="tiny"
            :type="detectedType === 'vector' ? 'success' : 'info'"
            :bordered="false"
          >
            {{ detectedType === 'vector' ? '矢量图' : '位图' }}
          </NTag>
        </NSpace>
        <NSpace :size="4" align="center">
          <NButton size="tiny" quaternary @click="resetView"> 重置视图 </NButton>
          <NButton size="tiny" quaternary type="error" :disabled="disabled" @click="clearFile">
            移除文件
          </NButton>
        </NSpace>
      </div>
      <NText depth="3" style="font-size: 11px; display: block; margin-bottom: 4px">
        滚轮缩放，拖拽移动
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
          :src="previewUrl ?? undefined"
          class="preview-img"
          :style="{ transform: previewTransform }"
          draggable="false"
          alt="preview"
        />
      </div>
    </div>
  </NCard>
</template>

<style scoped>
.upload-preview-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 4px;
}

.preview-viewport {
  position: relative;
  width: 100%;
  height: 300px;
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
</style>
