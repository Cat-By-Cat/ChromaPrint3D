<script setup lang="ts">
import { computed } from 'vue'
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
  loading_resources: '加载资源...',
  preprocessing: '预处理...',
  matching: '颜色匹配...',
  building_model: '构建模型...',
  exporting: '导出结果...',
  unknown: '处理中...',
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
} = useAsyncTask<TaskStatus>(submitCurrentTask, fetchTaskStatus, {
  onCompleted(s) {
    appStore.setCompletedTask(s)
  },
  onFailed() {
    appStore.markTaskFailed()
  },
})

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
  return selectedFile.value !== null && !loading.value
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
