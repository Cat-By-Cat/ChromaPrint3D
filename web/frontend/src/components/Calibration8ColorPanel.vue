<script setup lang="ts">
import { computed, ref } from 'vue'
import {
  NAlert,
  NButton,
  NCard,
  NDivider,
  NInput,
  NSpace,
  NStep,
  NSteps,
  NTag,
  useMessage,
} from 'naive-ui'
import { generate8ColorBoard, getBoardMetaPath, getBoardModelPath } from '../api'
import { useBlobDownload } from '../composables/useBlobDownload'
import type { PaletteChannel } from '../types'
import ColorDBBuildSection from './calibration/ColorDBBuildSection.vue'

type EditablePaletteChannel = PaletteChannel & {
  id: number
}

let nextPaletteId = 0

function createChannel(color: string, material: string): EditablePaletteChannel {
  nextPaletteId += 1
  return {
    id: nextPaletteId,
    color,
    material,
  }
}

const DEFAULT_8_COLORS: EditablePaletteChannel[] = [
  createChannel('Bamboo Green', 'PLA Basic'),
  createChannel('Black', 'PLA Basic'),
  createChannel('Blue', 'PLA Basic'),
  createChannel('Cyan', 'PLA Basic'),
  createChannel('Magenta', 'PLA Basic'),
  createChannel('Red', 'PLA Basic'),
  createChannel('White', 'PLA Basic'),
  createChannel('Yellow', 'PLA Basic'),
]

const message = useMessage()
const { downloadByUrl } = useBlobDownload((error) => message.error(error))

const emit = defineEmits<{
  (e: 'colordb-updated'): void
}>()

const palette = ref<EditablePaletteChannel[]>(DEFAULT_8_COLORS.map((item) => ({ ...item })))
const board1Id = ref<string | null>(null)
const board2Id = ref<string | null>(null)
const generating1 = ref(false)
const generating2 = ref(false)
const hasReadyColorDB = ref(false)

const currentStep = computed(() => {
  if (hasReadyColorDB.value) return 3
  if (board1Id.value || board2Id.value) return 2
  return 1
})

const nextActionHint = computed(() => {
  if (!board1Id.value) return '建议先生成并打印校准板 1。'
  if (!hasReadyColorDB.value) return '上传与板号匹配的照片 + Meta 文件，构建一个或两个 ColorDB。'
  return 'ColorDB 已就绪，可到“图像转换”页面组合使用。'
})

function toPaletteRequest(paletteRows: EditablePaletteChannel[]): PaletteChannel[] {
  return paletteRows.map((row) => ({
    color: row.color,
    material: row.material,
  }))
}

function validatePalette(): boolean {
  for (const channel of palette.value) {
    if (!channel.color.trim()) {
      message.error('请填写所有颜色名称')
      return false
    }
  }
  return true
}

async function handleGenerateBoard(boardIndex: number) {
  if (!validatePalette()) return
  const isBoard1 = boardIndex === 1
  const generatingRef = isBoard1 ? generating1 : generating2
  const boardRef = isBoard1 ? board1Id : board2Id

  generatingRef.value = true
  boardRef.value = null
  try {
    const response = await generate8ColorBoard({
      palette: toPaletteRequest(palette.value),
      board_index: boardIndex,
    })
    boardRef.value = response.board_id
    message.success(`校准板 ${boardIndex} 生成成功`)
  } catch (error: unknown) {
    message.error(`生成失败: ${error instanceof Error ? error.message : String(error)}`)
  } finally {
    generatingRef.value = false
  }
}

async function download3mf(boardId: string | null) {
  if (!boardId) return
  await downloadByUrl(getBoardModelPath(boardId), `calibration-board-${boardId.slice(0, 8)}.3mf`)
}

async function downloadMeta(boardId: string | null) {
  if (!boardId) return
  await downloadByUrl(getBoardMetaPath(boardId), `calibration-board-${boardId.slice(0, 8)}.json`)
}

function handleColorDBUpdated() {
  hasReadyColorDB.value = true
  emit('colordb-updated')
}
</script>

<template>
  <NSpace vertical :size="20" class="calibration-layout">
    <NCard class="calibration-card">
      <template #header>
        <div class="calibration-header">
          <NSpace align="center" :size="8">
            <span>八色校准流程</span>
            <NTag size="small" type="warning" :bordered="false">Beta</NTag>
          </NSpace>
          <p class="calibration-subtitle">当前步骤：第 {{ currentStep }} 步 / 3</p>
        </div>
      </template>

      <NSteps :current="currentStep" size="small" class="calibration-steps">
        <NStep title="生成两张校准板" description="按同一组颜色生成板 1 / 板 2" />
        <NStep title="打印与拍摄" description="建议先板 1，追求更高精度再补板 2" />
        <NStep title="构建 ColorDB" description="每张校准板可分别构建一个 ColorDB 并自动加入会话" />
      </NSteps>

      <NAlert type="info" :bordered="false" class="calibration-step-alert">
        {{ nextActionHint }}
      </NAlert>
    </NCard>

    <NCard title="步骤 1：生成八色校准板" class="calibration-card">
      <NSpace vertical :size="12">
        <NAlert type="info" :bordered="false">
          八色校准推荐生成两张校准板（各 40 x 40）。板 1 覆盖主色域，板 2 用于补充细节颜色。
        </NAlert>

        <div v-for="(channel, index) in palette" :key="channel.id" class="calibration-palette-row">
          <NTag :bordered="false" type="info" size="small">{{ index + 1 }}</NTag>
          <NInput
            v-model:value="channel.color"
            placeholder="颜色名称（例如 White）"
            class="calibration-palette-input"
          />
          <NInput
            v-model:value="channel.material"
            placeholder="材质（例如 PLA Basic）"
            class="calibration-palette-input"
          />
        </div>

        <NDivider />

        <div class="calibration-actions">
          <NButton type="primary" :loading="generating1" @click="handleGenerateBoard(1)">
            生成校准板 1（推荐优先）
          </NButton>
          <NTag v-if="board1Id" type="success" :bordered="false" size="small">板 1 已生成</NTag>
        </div>

        <div v-if="board1Id" class="calibration-actions">
          <NButton type="success" size="small" @click="download3mf(board1Id)">下载板 1 的 3MF</NButton>
          <NButton type="info" size="small" @click="downloadMeta(board1Id)">下载板 1 的 Meta</NButton>
        </div>

        <div class="calibration-actions">
          <NButton type="primary" :loading="generating2" @click="handleGenerateBoard(2)">
            生成校准板 2
          </NButton>
          <NTag v-if="board2Id" type="success" :bordered="false" size="small">板 2 已生成</NTag>
        </div>

        <div v-if="board2Id" class="calibration-actions">
          <NButton type="success" size="small" @click="download3mf(board2Id)">下载板 2 的 3MF</NButton>
          <NButton type="info" size="small" @click="downloadMeta(board2Id)">下载板 2 的 Meta</NButton>
        </div>
      </NSpace>
    </NCard>

    <NCard title="步骤 2：打印与拍摄说明" class="calibration-card">
      <NSpace vertical :size="12">
        <NAlert type="warning" :bordered="false" title="关键点">
          每张校准板都对应各自的 Meta 文件。构建 ColorDB 时，照片和 Meta 必须来自同一张板。
        </NAlert>
        <NAlert type="info" :bordered="false">
          板 1 足以得到较好效果；若追求更完整的色域覆盖，可继续打印板 2 并再构建一个 ColorDB。
        </NAlert>
      </NSpace>
    </NCard>

    <ColorDBBuildSection
      title="步骤 3：构建 ColorDB"
      tips="你可以为板 1 和板 2 各构建一个 ColorDB，后续在图像转换里组合使用。"
      build-button-text="构建并添加 ColorDB"
      @colordb-updated="handleColorDBUpdated"
    />
  </NSpace>
</template>
