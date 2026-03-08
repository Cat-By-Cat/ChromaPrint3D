<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { storeToRefs } from 'pinia'
import { NCard, NText, NSpace, NAlert } from 'naive-ui'
import { useAsyncTask } from '../composables/useAsyncTask'
import { useBlobDownload } from '../composables/useBlobDownload'
import { fetchConvertTaskStatus, submitConvertTask } from '../services/convertService'
import { getResultPath } from '../services/resultService'
import { useAppStore } from '../stores/app'
import ProgressActionGroup from './common/ProgressActionGroup.vue'
import type { TaskStatus } from '../types'

const appStore = useAppStore()
const { selectedFile, params, inputType, completedTask } = storeToRefs(appStore)
const { downloadByUrl } = useBlobDownload()

const stageLabels: Record<string, string> = {
  loading_resources: '加载资源',
  preprocessing: '预处理',
  matching: '颜色匹配',
  building_model: '构建模型',
  exporting: '导出结果',
  unknown: '处理中',
}

interface StageWeight {
  start: number
  weight: number
}

const stageWeights: Record<string, StageWeight> = {
  loading_resources: { start: 0.0, weight: 0.05 },
  preprocessing: { start: 0.05, weight: 0.30 },
  matching: { start: 0.35, weight: 0.15 },
  building_model: { start: 0.50, weight: 0.20 },
  exporting: { start: 0.70, weight: 0.30 },
}

function computeOverallProgress(stage: string, stageProgress: number): number {
  const w = stageWeights[stage]
  if (!w) return 0
  return Math.min(1, w.start + w.weight * Math.max(0, Math.min(1, stageProgress)))
}

function submitCurrentTask() {
  const file = selectedFile.value!
  return submitConvertTask(file, params.value, inputType.value)
}

const {
  status: taskStatus,
  loading,
  error,
  isCompleted,
  submit: submitTask,
  reset: resetTask,
} = useAsyncTask<TaskStatus>(submitCurrentTask, fetchConvertTaskStatus, {
  onCompleted(s) {
    appStore.setCompletedTask(s)
  },
  onFailed() {
    appStore.markTaskFailed()
  },
})

watch(selectedFile, () => {
  resetTask()
})

const isRunning = computed(() => {
  const s = taskStatus.value?.status
  return s === 'pending' || s === 'running'
})

const progressPercent = computed(() => {
  if (!taskStatus.value) return 0
  const overall = computeOverallProgress(taskStatus.value.stage, taskStatus.value.progress)
  return Math.round(overall * 100)
})

const stageText = computed(() => {
  if (!taskStatus.value) return ''
  return stageLabels[taskStatus.value.stage] || taskStatus.value.stage
})

const taskState = computed(() => taskStatus.value?.status ?? '')
const showRunningProgress = computed(() => taskState.value === 'running')

const convertButtonText = computed(() => {
  if (taskState.value === 'pending') return '排队中'
  if (showRunningProgress.value) return `${stageText.value || '处理中'} ${progressPercent.value}%`
  if (loading.value) return '提交任务'
  return '开始转换'
})

const canSubmit = computed(() => {
  return selectedFile.value !== null && !loading.value
})

const result = computed(() => completedTask.value?.result ?? null)
const completedTaskId = computed(() => completedTask.value?.id ?? '')
const isDownloading3mf = ref(false)
const canDownload3mf = computed(() => {
  return (
    completedTask.value?.status === 'completed' &&
    Boolean(result.value?.has_3mf) &&
    Boolean(completedTaskId.value)
  )
})

const isVectorInput = computed(() => inputType.value === 'vector')

const vectorFileSizeText = computed(() => {
  if (!isVectorInput.value || !selectedFile.value) return ''
  const kb = selectedFile.value.size / 1024
  if (kb < 1024) return `${kb.toFixed(0)} KB`
  return `${(kb / 1024).toFixed(1)} MB`
})

const download3mfFilename = computed(() => {
  const rawName = selectedFile.value?.name?.trim() ?? ''
  if (rawName.length > 0) {
    const dot = rawName.lastIndexOf('.')
    const baseName = dot > 0 ? rawName.slice(0, dot) : rawName
    return `${baseName || 'result'}.3mf`
  }
  return completedTaskId.value ? `${completedTaskId.value.substring(0, 8)}.3mf` : 'result.3mf'
})

async function handleConvert() {
  if (!selectedFile.value) return
  appStore.markTaskStarted()
  await submitTask()
}

async function handleDownload3MF() {
  if (!canDownload3mf.value || isDownloading3mf.value) return
  isDownloading3mf.value = true
  try {
    await downloadByUrl(getResultPath(completedTaskId.value), download3mfFilename.value)
  } catch {
    // error is already handled by runtime abstraction caller when needed
  } finally {
    isDownloading3mf.value = false
  }
}
</script>

<template>
  <NCard size="small">
    <NSpace vertical :size="12">
      <NSpace vertical :size="8">
        <ProgressActionGroup
          :primary-label="convertButtonText"
          :primary-disabled="!canSubmit"
          :primary-show-progress="showRunningProgress"
          :primary-progress-percent="progressPercent"
          :secondary-visible="isCompleted && Boolean(result?.has_3mf)"
          secondary-label="下载3MF文件"
          secondary-type="success"
          :secondary-disabled="!canDownload3mf || isDownloading3mf"
          :secondary-loading="isDownloading3mf"
          @primary-click="handleConvert"
          @secondary-click="handleDownload3MF"
        />
        <NText v-if="!selectedFile" depth="3" style="font-size: 13px"> 请先上传文件 </NText>
      </NSpace>

      <NAlert v-if="isVectorInput && !isRunning && !isCompleted" type="warning">
        当前输入为矢量图 (SVG{{ vectorFileSizeText ? `, ${vectorFileSizeText}` : ''
        }})，路径较多的复杂矢量图转换耗时可能较长，请耐心等待。
      </NAlert>

      <NAlert v-if="error" type="error" closable @close="error = null">
        {{ error }}
      </NAlert>
    </NSpace>
  </NCard>
</template>
