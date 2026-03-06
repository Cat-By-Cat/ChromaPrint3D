<script setup lang="ts">
import { computed, ref } from 'vue'
import {
  NAlert,
  NButton,
  NCard,
  NDivider,
  NImage,
  NInput,
  NSpace,
  NStep,
  NSteps,
  NTag,
  useMessage,
} from 'naive-ui'
import { useBlobDownload } from '../composables/useBlobDownload'
import { generateBoard, getBoardMetaPath, getBoardModelPath } from '../services/calibrationService'
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

const message = useMessage()
const { downloadByUrl } = useBlobDownload((error) => message.error(error))

const emit = defineEmits<{
  (e: 'colordb-updated'): void
}>()

const maxChannels = 4
const minChannels = 2
const hasReadyColorDB = ref(false)
const boardId = ref<string | null>(null)
const generating = ref(false)

const palette = ref<EditablePaletteChannel[]>([
  createChannel('White', 'PLA Basic'),
  createChannel('Yellow', 'PLA Basic'),
  createChannel('Red', 'PLA Basic'),
  createChannel('Blue', 'PLA Basic'),
])

const currentStep = computed(() => {
  if (hasReadyColorDB.value) return 3
  if (boardId.value) return 2
  return 1
})

const nextActionHint = computed(() => {
  if (!boardId.value) return '先确认颜色/材质并生成校准板。'
  if (!hasReadyColorDB.value) return '打印并拍照后，完成第 3 步（构建 ColorDB）。'
  return 'ColorDB 已就绪，可切换到“图像转换”页面开始使用。'
})

function toPaletteRequest(paletteRows: EditablePaletteChannel[]): PaletteChannel[] {
  return paletteRows.map((row) => ({
    color: row.color,
    material: row.material,
  }))
}

function addChannel() {
  if (palette.value.length >= maxChannels) {
    message.warning(`最多支持 ${maxChannels} 个通道`)
    return
  }
  palette.value.push(createChannel('', 'PLA Basic'))
}

function removeChannel(index: number) {
  if (palette.value.length <= minChannels) {
    message.warning(`至少需要 ${minChannels} 个通道`)
    return
  }
  palette.value.splice(index, 1)
}

async function handleGenerate() {
  for (const channel of palette.value) {
    if (!channel.color.trim()) {
      message.error('请填写所有颜色名称')
      return
    }
  }

  generating.value = true
  boardId.value = null
  try {
    const response = await generateBoard({ palette: toPaletteRequest(palette.value) })
    boardId.value = response.board_id
    message.success('校准板生成成功')
  } catch (error: unknown) {
    message.error(`生成失败: ${error instanceof Error ? error.message : String(error)}`)
  } finally {
    generating.value = false
  }
}

async function download3mf() {
  if (!boardId.value) return
  await downloadByUrl(getBoardModelPath(boardId.value), `calibration-board-${boardId.value.slice(0, 8)}.3mf`)
}

async function downloadMeta() {
  if (!boardId.value) return
  await downloadByUrl(getBoardMetaPath(boardId.value), `calibration-board-${boardId.value.slice(0, 8)}.json`)
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
            <span>四色校准流程</span>
            <NTag size="small" :bordered="false" type="info">4 色</NTag>
          </NSpace>
          <p class="calibration-subtitle">当前步骤：第 {{ currentStep }} 步 / 3</p>
        </div>
      </template>

      <NSteps :current="currentStep" size="small" class="calibration-steps">
        <NStep title="生成校准板" description="确认颜色与材质，生成 3MF 与 Meta" />
        <NStep title="打印与拍摄" description="打印校准板并拍摄清晰照片" />
        <NStep title="构建 ColorDB" description="上传照片和 Meta 生成数据库并自动加入会话" />
      </NSteps>

      <NAlert type="info" :bordered="false" class="calibration-step-alert">
        {{ nextActionHint }}
      </NAlert>
    </NCard>

    <NCard title="步骤 1：生成校准板" class="calibration-card">
      <NSpace vertical :size="12">
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
          <NButton size="small" quaternary type="error" @click="removeChannel(index)">删除</NButton>
        </div>

        <div class="calibration-inline-actions">
          <NButton size="small" :disabled="palette.length >= maxChannels" @click="addChannel">
            添加通道
          </NButton>
        </div>

        <NDivider />

        <div class="calibration-actions">
          <NButton type="primary" :loading="generating" @click="handleGenerate">
            生成校准板
          </NButton>
          <NTag v-if="boardId" type="success" :bordered="false" size="small">校准板已生成</NTag>
        </div>

        <div v-if="boardId" class="calibration-actions">
          <NButton type="success" @click="download3mf">下载 3MF 模型</NButton>
          <NButton type="info" @click="downloadMeta">下载 Meta JSON</NButton>
        </div>
      </NSpace>
    </NCard>

    <NCard title="步骤 2：打印与拍摄" class="calibration-card">
      <NSpace vertical :size="14">
        <NAlert type="warning" title="注意" :bordered="false">
          上传的 Meta 文件必须与拍摄照片来自同一块校准板，不匹配会导致构建结果错误。
        </NAlert>

        <NDivider>示例：已打印校准板照片</NDivider>
        <div class="calibration-image-wrap">
          <NImage
            src="/examples/RYBW_new.jpg"
            object-fit="contain"
            :img-props="{
              style: 'max-width: 100%; max-height: 380px; object-fit: contain; cursor: zoom-in;',
            }"
            alt="四色校准板照片示例"
          />
          <p class="calibration-image-caption">
            示例为 Red/Yellow/Blue/White 四色校准板照片，请尽量保持均匀光照和清晰边界。
          </p>
        </div>
      </NSpace>
    </NCard>

    <ColorDBBuildSection
      title="步骤 3：构建 ColorDB"
      tips="上传照片和匹配的 Meta 文件后，系统会自动构建并添加到当前会话。"
      build-button-text="构建并添加 ColorDB"
      @colordb-updated="handleColorDBUpdated"
    />
  </NSpace>
</template>
