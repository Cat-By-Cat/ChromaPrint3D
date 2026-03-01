<script setup lang="ts">
import {
  NAlert,
  NButton,
  NCard,
  NForm,
  NFormItem,
  NGrid,
  NGi,
  NInput,
  NSpin,
  NSpace,
  NText,
  NUpload,
  useMessage,
} from 'naive-ui'
import { getSessionColorDBDownloadUrl } from '../../api'
import { useBlobDownload } from '../../composables/useBlobDownload'
import { useColorDBBuildFlow } from '../../composables/useColorDBBuildFlow'

const props = withDefaults(
  defineProps<{
    title?: string
    tips?: string
    buildButtonText?: string
  }>(),
  {
    title: '构建 ColorDB',
    tips: '',
    buildButtonText: '构建 ColorDB',
  },
)

const emit = defineEmits<{
  (e: 'colordb-updated'): void
}>()

const message = useMessage()
const { downloadByUrl } = useBlobDownload((error) => message.error(error))

const {
  calibImage,
  calibMeta,
  dbName,
  dbNameError,
  building,
  builtDB,
  buildError,
  canBuild,
  handleImageUpload,
  handleMetaUpload,
  handleBuild,
} = useColorDBBuildFlow({
  onBuilt: (result) => {
    message.success(`ColorDB "${result.name}" 构建成功，已自动添加到可用数据库`)
    emit('colordb-updated')
  },
})

async function handleBuildAndNotify() {
  const result = await handleBuild()
  if (!result && buildError.value) {
    message.error(`构建失败: ${buildError.value}`)
  }
}

async function downloadBuiltDB() {
  if (!builtDB.value) return
  await downloadByUrl(getSessionColorDBDownloadUrl(builtDB.value.name), `${builtDB.value.name}.json`)
}
</script>

<template>
  <NCard :title="props.title" class="calibration-card">
    <NSpace vertical :size="16">
      <NAlert v-if="props.tips" type="info" :bordered="false">
        {{ props.tips }}
      </NAlert>

      <NGrid :cols="2" :x-gap="16" :y-gap="12" responsive="screen" item-responsive>
        <NGi span="2 m:1">
          <NForm label-placement="top" class="calibration-form-block">
            <NFormItem label="上传校准板照片">
              <NUpload
                accept="image/*"
                :max="1"
                :default-upload="false"
                list-type="text"
                @change="handleImageUpload"
              >
                <NButton>选择图片</NButton>
              </NUpload>
            </NFormItem>
            <NText v-if="calibImage" depth="3" class="calibration-file-name">
              已选择：{{ calibImage.name }}
            </NText>
          </NForm>
        </NGi>

        <NGi span="2 m:1">
          <NForm label-placement="top" class="calibration-form-block">
            <NFormItem label="上传 Meta JSON 文件">
              <NUpload
                accept=".json"
                :max="1"
                :default-upload="false"
                list-type="text"
                @change="handleMetaUpload"
              >
                <NButton>选择 JSON</NButton>
              </NUpload>
            </NFormItem>
            <NText v-if="calibMeta" depth="3" class="calibration-file-name">
              已选择：{{ calibMeta.name }}
            </NText>
          </NForm>
        </NGi>
      </NGrid>

      <NForm label-placement="top" class="calibration-form-block">
        <NFormItem
          label="ColorDB 名称"
          :validation-status="dbNameError ? 'error' : undefined"
          :feedback="dbNameError || '仅支持字母、数字和下划线'"
        >
          <NInput
            v-model:value="dbName"
            placeholder="例如：my_printer_profile"
            class="calibration-db-input"
          />
        </NFormItem>
      </NForm>

      <div class="calibration-actions">
        <NButton type="primary" :loading="building" :disabled="!canBuild" @click="handleBuildAndNotify">
          {{ props.buildButtonText }}
        </NButton>
      </div>

      <NSpin :show="building">
        <NAlert v-if="builtDB" type="success" title="构建成功">
          <p class="calibration-result-line">名称：{{ builtDB.name }}</p>
          <p class="calibration-result-line">通道数：{{ builtDB.num_channels }}</p>
          <p class="calibration-result-line">条目数：{{ builtDB.num_entries }}</p>
          <div class="calibration-actions calibration-actions--compact">
            <NButton size="small" type="info" @click="downloadBuiltDB">下载 ColorDB JSON</NButton>
          </div>
          <p class="calibration-result-tip">
            已自动添加到当前会话的可用数据库列表中，可在“图像转换”页面直接使用。
          </p>
        </NAlert>

        <NAlert v-if="buildError" type="error" title="构建失败" class="calibration-error-alert">
          {{ buildError }}
        </NAlert>
      </NSpin>
    </NSpace>
  </NCard>
</template>
