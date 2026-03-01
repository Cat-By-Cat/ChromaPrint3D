<script setup lang="ts">
import { ref } from 'vue'
import {
  NAlert,
  NButton,
  NCard,
  NForm,
  NFormItem,
  NGrid,
  NGi,
  NInput,
  NSpace,
  NUpload,
  useMessage,
} from 'naive-ui'
import { useColorDBUploadFlow } from '../../composables/useColorDBUploadFlow'

const props = withDefaults(
  defineProps<{
    title?: string
    tips?: string
  }>(),
  {
    title: '上传 ColorDB',
    tips: '如果你已经有 ColorDB JSON 文件（之前构建并下载），可以直接上传使用，无需重新构建。',
  },
)

const emit = defineEmits<{
  (e: 'colordb-updated'): void
  (e: 'go-convert'): void
}>()

const message = useMessage()
const uploadCompleted = ref(false)

const {
  uploadFile,
  uploadName,
  uploading,
  uploadError,
  uploadNameError,
  canUpload,
  handleUploadFileChange,
  handleUpload,
} = useColorDBUploadFlow({
  onUploaded: (result) => {
    message.success(`ColorDB "${result.name}" 上传成功，已添加到当前会话`)
    emit('colordb-updated')
  },
})

async function handleUploadAndNotify() {
  const result = await handleUpload()
  if (!result && uploadError.value) {
    message.error(`上传失败: ${uploadError.value}`)
    return
  }
  if (result) {
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

      <NGrid :cols="2" :x-gap="16" :y-gap="12" responsive="screen" item-responsive>
        <NGi span="2 m:1">
          <NForm label-placement="top" class="calibration-form-block">
            <NFormItem label="选择 ColorDB JSON 文件">
              <NUpload
                accept=".json"
                :max="1"
                :default-upload="false"
                list-type="text"
                @change="handleUploadFileChange"
              >
                <NButton>选择文件</NButton>
              </NUpload>
            </NFormItem>
            <p v-if="uploadFile" class="calibration-file-name">已选择：{{ uploadFile.name }}</p>
          </NForm>
        </NGi>

        <NGi span="2 m:1">
          <NForm label-placement="top" class="calibration-form-block">
            <NFormItem
              label="名称（可选，覆盖文件中的名称）"
              :validation-status="uploadNameError ? 'error' : undefined"
              :feedback="uploadNameError || '留空则使用文件中的名称'"
            >
              <NInput
                v-model:value="uploadName"
                placeholder="例如：my_printer_profile"
                class="calibration-db-input"
              />
            </NFormItem>
          </NForm>
        </NGi>
      </NGrid>

      <div class="calibration-actions">
        <NButton type="primary" :loading="uploading" :disabled="!canUpload" @click="handleUploadAndNotify">
          上传 ColorDB
        </NButton>
        <NButton v-if="uploadCompleted" type="success" secondary @click="handleGoToConvert">
          前往图像转换
        </NButton>
      </div>

      <NAlert v-if="uploadError" type="error" title="上传失败" class="calibration-error-alert">
        {{ uploadError }}
      </NAlert>
    </NSpace>
  </NCard>
</template>
