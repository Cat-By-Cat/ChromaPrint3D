<script setup lang="ts">
import { computed } from 'vue'
import { storeToRefs } from 'pinia'
import {
  NCard,
  NImage,
  NButton,
  NDescriptions,
  NDescriptionsItem,
  NSpace,
  NGrid,
  NGridItem,
  NText,
} from 'naive-ui'
import { getPreviewUrl, getSourceMaskUrl, getResultUrl } from '../api'
import { useBlobDownload } from '../composables/useBlobDownload'
import { useAppStore } from '../stores/app'

const appStore = useAppStore()
const { completedTask } = storeToRefs(appStore)
const isCompleted = computed(() => completedTask.value?.status === 'completed')
const result = computed(() => completedTask.value?.result ?? null)
const taskId = computed(() => completedTask.value?.id ?? '')
const { downloadByUrl } = useBlobDownload()

async function handleDownload3MF() {
  if (!taskId.value) return
  const url = getResultUrl(taskId.value)
  try {
    await downloadByUrl(url, `${taskId.value.substring(0, 8)}.3mf`)
  } catch {
    // error is already handled by runtime abstraction caller when needed
  }
}
</script>

<template>
  <NCard v-if="isCompleted && result" title="转换结果" size="small">
    <NSpace vertical :size="16">
      <!-- Preview images -->
      <NGrid :cols="2" :x-gap="16" :y-gap="16">
        <NGridItem v-if="result.has_preview">
          <NCard title="预览图" size="small" embedded>
            <NImage
              :src="getPreviewUrl(taskId)"
              fallback-src=""
              object-fit="contain"
              :img-props="{
                style: 'max-width: 100%; max-height: 480px; object-fit: contain; cursor: zoom-in;',
              }"
              style="border-radius: 4px"
            />
          </NCard>
        </NGridItem>

        <NGridItem v-if="result.has_source_mask">
          <NCard title="颜色源掩码" size="small" embedded>
            <NImage
              :src="getSourceMaskUrl(taskId)"
              fallback-src=""
              object-fit="contain"
              :img-props="{
                style: 'max-width: 100%; max-height: 480px; object-fit: contain; cursor: zoom-in;',
              }"
              style="border-radius: 4px"
            />
          </NCard>
        </NGridItem>
      </NGrid>

      <!-- Download button & dimensions info -->
      <NSpace v-if="result.has_3mf" align="center">
        <NButton type="primary" @click="handleDownload3MF"> 下载 3MF 文件 </NButton>
        <NText depth="3" style="font-size: 12px">
          {{ result.input_width }}×{{ result.input_height }} px
          <template v-if="result.physical_width_mm > 0">
            | {{ result.physical_width_mm.toFixed(1) }}×{{ result.physical_height_mm.toFixed(1) }}
            mm
          </template>
          <template v-if="result.resolved_pixel_mm > 0">
            | 像素 {{ result.resolved_pixel_mm.toFixed(2) }} mm
          </template>
        </NText>
      </NSpace>

      <!-- Match statistics -->
      <NDescriptions label-placement="left" bordered :column="2" size="small" title="匹配统计">
        <NDescriptionsItem label="聚类总数">
          {{ result.stats.clusters_total }}
        </NDescriptionsItem>
        <NDescriptionsItem label="数据库匹配">
          {{ result.stats.db_only }}
        </NDescriptionsItem>
        <NDescriptionsItem label="模型回退">
          {{ result.stats.model_fallback }}
        </NDescriptionsItem>
        <NDescriptionsItem label="模型查询">
          {{ result.stats.model_queries }}
        </NDescriptionsItem>
        <NDescriptionsItem label="数据库平均色差">
          {{ result.stats.avg_db_de.toFixed(2) }}
        </NDescriptionsItem>
        <NDescriptionsItem label="模型平均色差">
          {{ result.stats.avg_model_de.toFixed(2) }}
        </NDescriptionsItem>
      </NDescriptions>
    </NSpace>
  </NCard>
</template>
