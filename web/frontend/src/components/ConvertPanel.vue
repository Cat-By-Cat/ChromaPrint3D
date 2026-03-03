<script setup lang="ts">
import { computed, watch } from 'vue'
import { storeToRefs } from 'pinia'
import { NButton, NProgress, NCard, NText, NSpace, NAlert } from 'naive-ui'
import { fetchTaskStatus } from '../api'
import { useAsyncTask } from '../composables/useAsyncTask'
import { submitConvertTask } from '../services/convertService'
import { useAppStore } from '../stores/app'
import type { TaskStatus } from '../types'

const appStore = useAppStore()
const { selectedFile, params, inputType } = storeToRefs(appStore)

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
} = useAsyncTask<TaskStatus>(submitCurrentTask, fetchTaskStatus, {
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

const canSubmit = computed(() => {
  return selectedFile.value !== null && !loading.value
})

const isVectorInput = computed(() => inputType.value === 'vector')

const vectorFileSizeText = computed(() => {
  if (!isVectorInput.value || !selectedFile.value) return ''
  const kb = selectedFile.value.size / 1024
  if (kb < 1024) return `${kb.toFixed(0)} KB`
  return `${(kb / 1024).toFixed(1)} MB`
})

async function handleConvert() {
  if (!selectedFile.value) return
  appStore.markTaskStarted()
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

        <NText v-if="!selectedFile" depth="3" style="font-size: 13px"> 请先上传文件 </NText>
      </NSpace>

      <NAlert v-if="isVectorInput && !isRunning && !isCompleted" type="warning">
        当前输入为矢量图 (SVG{{ vectorFileSizeText ? `, ${vectorFileSizeText}` : ''
        }})，路径较多的复杂矢量图转换耗时可能较长，请耐心等待。
      </NAlert>

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
