<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import {
  NAlert,
  NButton,
  NCard,
  NCheckbox,
  NEmpty,
  NForm,
  NFormItem,
  NList,
  NListItem,
  NPopconfirm,
  NSpace,
  NTag,
  NText,
  NUpload,
  useMessage,
} from 'naive-ui'
import { useColorDBUploadFlow } from '../../composables/useColorDBUploadFlow'
import { fetchSessionColorDBs, deleteSessionColorDB } from '../../api/session'
import { roundTo } from '../../runtime/number'
import type { ColorDBInfo } from '../../types'

const props = withDefaults(
  defineProps<{
    title?: string
    tips?: string
  }>(),
  {
    title: '上传 ColorDB',
    tips: '如果你已经有 ColorDB JSON 文件（之前构建并下载），可以直接上传使用，无需重新构建。支持同时选择多个文件批量上传。',
  },
)

const emit = defineEmits<{
  (e: 'colordb-updated'): void
  (e: 'go-convert'): void
}>()

const message = useMessage()
const uploadCompleted = ref(false)

const sessionDbs = ref<ColorDBInfo[]>([])
const loadingDbs = ref(false)
const deletingName = ref<string | null>(null)
const selectedNames = ref<Set<string>>(new Set())
const batchDeleting = ref(false)

const allSelected = computed(
  () => sessionDbs.value.length > 0 && selectedNames.value.size === sessionDbs.value.length,
)
const someSelected = computed(
  () => selectedNames.value.size > 0 && selectedNames.value.size < sessionDbs.value.length,
)

function toggleSelect(name: string, checked: boolean) {
  if (checked) {
    selectedNames.value.add(name)
  } else {
    selectedNames.value.delete(name)
  }
  selectedNames.value = new Set(selectedNames.value)
}

function toggleSelectAll(checked: boolean) {
  if (checked) {
    selectedNames.value = new Set(sessionDbs.value.map((db) => db.name))
  } else {
    selectedNames.value = new Set()
  }
}

async function loadSessionDbs() {
  loadingDbs.value = true
  try {
    sessionDbs.value = await fetchSessionColorDBs()
  } catch {
    sessionDbs.value = []
  } finally {
    loadingDbs.value = false
    selectedNames.value = new Set()
  }
}

async function handleDelete(name: string) {
  deletingName.value = name
  try {
    await deleteSessionColorDB(name)
    message.success(`已删除 "${name}"`)
    emit('colordb-updated')
    await loadSessionDbs()
  } catch (err: unknown) {
    message.error(`删除失败: ${err instanceof Error ? err.message : String(err)}`)
  } finally {
    deletingName.value = null
  }
}

async function handleBatchDelete() {
  const names = [...selectedNames.value]
  if (names.length === 0) return
  batchDeleting.value = true
  let ok = 0
  let fail = 0
  for (const name of names) {
    try {
      await deleteSessionColorDB(name)
      ok++
    } catch {
      fail++
    }
  }
  if (ok > 0) emit('colordb-updated')
  if (fail === 0) {
    message.success(`已删除 ${ok} 个 ColorDB`)
  } else {
    message.warning(`删除 ${ok} 个成功，${fail} 个失败`)
  }
  batchDeleting.value = false
  await loadSessionDbs()
}

onMounted(loadSessionDbs)

const {
  fileList,
  uploading,
  uploadError,
  canUpload,
  isBatch,
  batchResults,
  handleUploadFileChange,
  handleUpload,
} = useColorDBUploadFlow({
  onUploaded: () => {
    emit('colordb-updated')
  },
  onBatchDone: (results) => {
    const ok = results.filter((r) => r.success)
    const fail = results.filter((r) => !r.success)
    if (ok.length > 0 && fail.length === 0) {
      message.success(
        ok.length === 1
          ? `ColorDB "${ok[0].dbName}" 上传成功`
          : `${ok.length} 个 ColorDB 全部上传成功`,
      )
    } else if (ok.length > 0 && fail.length > 0) {
      message.warning(`${ok.length} 个成功，${fail.length} 个失败`)
    }
    loadSessionDbs()
  },
})

async function handleUploadAndNotify() {
  const result = await handleUpload()
  if (!result && uploadError.value) {
    message.error(`上传失败: ${uploadError.value}`)
    return
  }
  if (result && result.length > 0) {
    uploadCompleted.value = true
  }
}

function handleGoToConvert() {
  emit('go-convert')
}
</script>

<template>
  <NCard :title="props.title" class="calibration-card">
    <NSpace vertical :size="16">
      <NAlert type="info" :bordered="false">
        {{ props.tips }}
      </NAlert>

      <NForm label-placement="top" class="calibration-form-block">
        <NFormItem label="选择 ColorDB JSON 文件">
          <NUpload
            v-model:file-list="fileList"
            accept=".json"
            multiple
            :default-upload="false"
            list-type="text"
            @change="handleUploadFileChange"
          >
            <NButton>选择文件</NButton>
          </NUpload>
        </NFormItem>
      </NForm>

      <NAlert v-if="isBatch" type="info" :bordered="false">
        已选择 {{ fileList.length }} 个文件，将使用各文件内部名称批量上传。
      </NAlert>

      <div class="calibration-actions">
        <NButton
          type="primary"
          :loading="uploading"
          :disabled="!canUpload"
          @click="handleUploadAndNotify"
        >
          {{ isBatch ? `上传全部（${fileList.length} 个）` : '上传 ColorDB' }}
        </NButton>
        <NButton v-if="uploadCompleted" type="success" secondary @click="handleGoToConvert">
          前往图像转换
        </NButton>
      </div>

      <NAlert v-if="uploadError" type="error" title="上传失败" class="calibration-error-alert">
        {{ uploadError }}
      </NAlert>

      <div v-if="batchResults.length > 1" class="batch-results">
        <NList bordered size="small">
          <NListItem v-for="(r, i) in batchResults" :key="i">
            <NSpace align="center" :size="8">
              <NTag :type="r.success ? 'success' : 'error'" size="small" :bordered="false">
                {{ r.success ? '成功' : '失败' }}
              </NTag>
              <NText>{{ r.fileName }}</NText>
              <NText v-if="r.success && r.dbName" depth="3" style="font-size: 12px">
                → {{ r.dbName }}
              </NText>
              <NText v-if="!r.success && r.error" type="error" style="font-size: 12px">
                {{ r.error }}
              </NText>
            </NSpace>
          </NListItem>
        </NList>
      </div>
    </NSpace>
  </NCard>

  <NCard title="已上传的 ColorDB" class="calibration-card session-db-card">
    <NSpace vertical :size="12">
      <NText depth="3" style="font-size: 13px">
        以下是当前会话中已上传的自定义 ColorDB，关闭浏览器或会话过期后将自动清除。
      </NText>

      <NEmpty v-if="!loadingDbs && sessionDbs.length === 0" description="暂无已上传的 ColorDB" />

      <template v-else>
        <div v-if="sessionDbs.length > 1" class="session-db-toolbar">
          <NCheckbox
            :checked="allSelected"
            :indeterminate="someSelected"
            @update:checked="toggleSelectAll"
          >
            全选
          </NCheckbox>
          <NPopconfirm
            v-if="selectedNames.size > 0"
            positive-text="确认删除"
            negative-text="取消"
            @positive-click="handleBatchDelete"
          >
            <template #trigger>
              <NButton size="small" type="error" secondary :loading="batchDeleting">
                删除选中（{{ selectedNames.size }}）
              </NButton>
            </template>
            确定要删除选中的 {{ selectedNames.size }} 个 ColorDB 吗？删除后无法恢复。
          </NPopconfirm>
        </div>

        <NList bordered hoverable>
          <NListItem v-for="db in sessionDbs" :key="db.name">
            <template #prefix>
              <NSpace align="center" :size="8">
                <NCheckbox
                  :checked="selectedNames.has(db.name)"
                  @update:checked="(v: boolean) => toggleSelect(db.name, v)"
                />
                <NTag size="small" type="info" :bordered="false">{{ db.num_entries }} 条</NTag>
              </NSpace>
            </template>
            <NSpace vertical :size="2">
              <NText strong>{{ db.name }}</NText>
              <NText depth="3" style="font-size: 12px">
                {{ db.num_channels }} 通道 · {{ db.max_color_layers }} 色层 ·
                {{ roundTo(db.layer_height_mm, 3) }}mm 层高 ·
                {{ roundTo(db.line_width_mm, 3) }}mm 线宽
              </NText>
            </NSpace>
            <template #suffix>
              <NPopconfirm
                positive-text="确认删除"
                negative-text="取消"
                @positive-click="handleDelete(db.name)"
              >
                <template #trigger>
                  <NButton
                    size="small"
                    type="error"
                    quaternary
                    :loading="deletingName === db.name"
                  >
                    删除
                  </NButton>
                </template>
                确定要删除 "{{ db.name }}" 吗？删除后无法恢复。
              </NPopconfirm>
            </template>
          </NListItem>
        </NList>
      </template>

      <NButton size="small" quaternary :loading="loadingDbs" @click="loadSessionDbs">
        刷新列表
      </NButton>
    </NSpace>
  </NCard>
</template>
