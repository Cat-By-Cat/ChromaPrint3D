<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { NUpload, NUploadDragger, NCard, NImage, NText, NSpace, NButton, NTag } from 'naive-ui'
import type { UploadFileInfo } from 'naive-ui'
import type { InputType } from '../types'

export interface ImageDimensions {
  width: number
  height: number
}

const props = defineProps<{
  modelValue: File | null
  disabled?: boolean
}>()

const emit = defineEmits<{
  'update:modelValue': [file: File | null]
  'update:dimensions': [dims: ImageDimensions | null]
  'update:inputType': [type: InputType]
}>()

const fileList = ref<UploadFileInfo[]>([])
const previewUrl = ref<string | null>(null)
const dimensions = ref<ImageDimensions | null>(null)
const detectedType = ref<InputType>('raster')

function isSvgFile(file: File): boolean {
  return file.type === 'image/svg+xml' || file.name.toLowerCase().endsWith('.svg')
}

const fileInfo = computed(() => {
  const file = props.modelValue
  if (!file) return null
  const sizeMB = (file.size / 1024 / 1024).toFixed(2)
  const dimStr = dimensions.value
    ? ` | ${dimensions.value.width}×${dimensions.value.height}`
    : ''
  return `${file.name} (${sizeMB} MB${dimStr})`
})

function handleChange(options: { fileList: UploadFileInfo[] }) {
  const files = options.fileList
  if (files.length === 0) {
    emit('update:modelValue', null)
    if (previewUrl.value) {
      URL.revokeObjectURL(previewUrl.value)
      previewUrl.value = null
    }
    return
  }
  const latest = files[files.length - 1]
  if (!latest) return
  const rawFile = latest.file
  if (rawFile) {
    emit('update:modelValue', rawFile)
  }
}

function clearFile() {
  fileList.value = []
  dimensions.value = null
  detectedType.value = 'raster'
  emit('update:modelValue', null)
  emit('update:dimensions', null)
  emit('update:inputType', 'raster')
  if (previewUrl.value) {
    URL.revokeObjectURL(previewUrl.value)
    previewUrl.value = null
  }
}

function readImageDimensions(file: File) {
  const url = URL.createObjectURL(file)
  const img = new Image()
  img.onload = () => {
    const dims = { width: img.naturalWidth, height: img.naturalHeight }
    dimensions.value = dims
    emit('update:dimensions', dims)
    URL.revokeObjectURL(url)
  }
  img.onerror = () => {
    dimensions.value = null
    emit('update:dimensions', null)
    URL.revokeObjectURL(url)
  }
  img.src = url
}

watch(
  () => props.modelValue,
  (newFile) => {
    if (previewUrl.value) {
      URL.revokeObjectURL(previewUrl.value)
      previewUrl.value = null
    }
    if (newFile) {
      const type: InputType = isSvgFile(newFile) ? 'vector' : 'raster'
      detectedType.value = type
      emit('update:inputType', type)

      previewUrl.value = URL.createObjectURL(newFile)
      if (type === 'raster') {
        readImageDimensions(newFile)
      } else {
        dimensions.value = null
        emit('update:dimensions', null)
      }
    } else {
      fileList.value = []
      detectedType.value = 'raster'
      emit('update:dimensions', null)
      emit('update:inputType', 'raster')
    }
  },
)
</script>

<template>
  <NCard title="输入文件" size="small">
    <NUpload
      v-if="!modelValue"
      accept="image/*,.svg"
      :max="1"
      :default-upload="false"
      :disabled="disabled"
      :file-list="fileList"
      :show-file-list="false"
      @update:file-list="(v: UploadFileInfo[]) => { fileList = v }"
      @change="handleChange"
    >
      <NUploadDragger>
        <NSpace vertical align="center" justify="center" style="padding: 32px 16px">
          <NText depth="3" style="font-size: 14px">
            点击或拖拽文件到此处上传
          </NText>
          <NText depth="3" style="font-size: 12px">
            支持 JPG / PNG / BMP / TIFF / SVG 格式
          </NText>
        </NSpace>
      </NUploadDragger>
    </NUpload>

    <div v-else class="preview-container">
      <div class="preview-header">
        <NSpace align="center" :size="8">
          <NText depth="3" style="font-size: 12px">
            {{ fileInfo }}
          </NText>
          <NTag size="tiny" :type="detectedType === 'vector' ? 'success' : 'info'" :bordered="false">
            {{ detectedType === 'vector' ? '矢量图' : '位图' }}
          </NTag>
        </NSpace>
        <NButton size="tiny" quaternary type="error" :disabled="disabled" @click="clearFile">
          移除文件
        </NButton>
      </div>
      <NImage
        :src="previewUrl ?? undefined"
        object-fit="contain"
        :img-props="{ style: 'max-width: 100%; max-height: 400px; object-fit: contain; cursor: zoom-in;' }"
        style="border-radius: 4px"
      />
      <NText depth="3" style="font-size: 11px; margin-top: 4px; display: block">
        点击图片可放大查看，支持滚轮缩放和拖拽移动
      </NText>
    </div>
  </NCard>
</template>

<style scoped>
.preview-container {
  text-align: center;
}

.preview-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 8px;
}
</style>
