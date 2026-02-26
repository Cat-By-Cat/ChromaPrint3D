<script setup lang="ts">
import { computed } from 'vue'
import { NButton, NProgress, NCard, NText, NSpace, NAlert } from 'naive-ui'
import { submitConvertRaster, submitConvertVector, fetchTaskStatus } from '../api'
import { useAsyncTask } from '../composables/useAsyncTask'
import type { ConvertAnyParams, ConvertRasterParams, ConvertVectorParams, InputType, TaskStatus } from '../types'

const props = defineProps<{
  file: File | null
  params: ConvertAnyParams
  inputType: InputType
}>()

const emit = defineEmits<{
  taskStarted: []
  taskCompleted: [task: TaskStatus]
  taskFailed: [task: TaskStatus]
}>()

const stageLabels: Record<string, string> = {
  loading_resources: '加载资源...',
  preprocessing: '预处理...',
  matching: '颜色匹配...',
  building_model: '构建模型...',
  exporting: '导出结果...',
  unknown: '处理中...',
}

function submitCurrentTask() {
  const file = props.file!
  if (props.inputType === 'vector') {
    const vp: ConvertVectorParams = {
      db_names: props.params.db_names,
      print_mode: props.params.print_mode,
      color_space: props.params.color_space,
      target_width_mm: props.params.target_width_mm,
      target_height_mm: props.params.target_height_mm,
      k_candidates: props.params.k_candidates,
      flip_y: props.params.flip_y,
      layer_height_mm: props.params.layer_height_mm,
      allowed_channels: props.params.allowed_channels,
      generate_preview: props.params.generate_preview,
      tessellation_tolerance_mm: props.params.tessellation_tolerance_mm,
      gradient_dither: props.params.gradient_dither,
      gradient_dither_strength: props.params.gradient_dither_strength,
    }
    return submitConvertVector(file, vp)
  }
  const rp: ConvertRasterParams = {
    db_names: props.params.db_names,
    print_mode: props.params.print_mode,
    color_space: props.params.color_space,
    max_width: props.params.max_width,
    max_height: props.params.max_height,
    target_width_mm: props.params.target_width_mm,
    target_height_mm: props.params.target_height_mm,
    scale: props.params.scale,
    k_candidates: props.params.k_candidates,
    cluster_count: props.params.cluster_count,
    dither: props.params.dither,
    dither_strength: props.params.dither_strength,
    allowed_channels: props.params.allowed_channels,
    model_enable: props.params.model_enable,
    model_only: props.params.model_only,
    model_threshold: props.params.model_threshold,
    model_margin: props.params.model_margin,
    flip_y: props.params.flip_y,
    pixel_mm: props.params.pixel_mm,
    layer_height_mm: props.params.layer_height_mm,
    generate_preview: props.params.generate_preview,
    generate_source_mask: props.params.generate_source_mask,
  }
  return submitConvertRaster(file, rp)
}

const {
  status: taskStatus,
  loading,
  error,
  isCompleted,
  submit: submitTask,
} = useAsyncTask<TaskStatus>(
  submitCurrentTask,
  fetchTaskStatus,
  {
    onCompleted(s) { emit('taskCompleted', s) },
    onFailed(s)    { emit('taskFailed', s) },
  },
)

const isRunning = computed(() => {
  const s = taskStatus.value?.status
  return s === 'pending' || s === 'running'
})

const progressPercent = computed(() => {
  if (!taskStatus.value) return 0
  return Math.round(taskStatus.value.progress * 100)
})

const stageText = computed(() => {
  if (!taskStatus.value) return ''
  return stageLabels[taskStatus.value.stage] || taskStatus.value.stage
})

const canSubmit = computed(() => {
  return props.file !== null && !loading.value
})

async function handleConvert() {
  if (!props.file) return
  emit('taskStarted')
  await submitTask()
}
</script>

<template>
  <NCard size="small">
    <NSpace vertical :size="12">
      <NSpace align="center">
        <NButton
          type="primary"
          size="large"
          :loading="loading"
          :disabled="!canSubmit"
          @click="handleConvert"
        >
          {{ isRunning ? '转换中...' : '开始转换' }}
        </NButton>

        <NText v-if="!file" depth="3" style="font-size: 13px">
          请先上传文件
        </NText>
      </NSpace>

      <NAlert v-if="error" type="error" closable @close="error = null">
        {{ error }}
      </NAlert>

      <div v-if="taskStatus && isRunning">
        <NSpace align="center" :size="8" style="margin-bottom: 4px">
          <NText style="font-size: 13px">{{ stageText }}</NText>
          <NText depth="3" style="font-size: 12px">{{ progressPercent }}%</NText>
        </NSpace>
        <NProgress
          type="line"
          :percentage="progressPercent"
          :show-indicator="false"
          :height="8"
          status="info"
          :processing="true"
        />
      </div>

      <div v-if="isCompleted">
        <NProgress
          type="line"
          :percentage="100"
          :show-indicator="false"
          :height="8"
          status="success"
        />
        <NText type="success" style="font-size: 13px; margin-top: 4px; display: block">
          转换完成
        </NText>
      </div>
    </NSpace>
  </NCard>
</template>
