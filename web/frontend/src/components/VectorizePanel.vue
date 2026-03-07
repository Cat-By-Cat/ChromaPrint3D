<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import {
  NCard,
  NSpace,
  NButton,
  NUpload,
  NUploadDragger,
  NText,
  NAlert,
  NSpin,
  NGrid,
  NGridItem,
  NInputNumber,
  NSwitch,
  NCollapse,
  NCollapseItem,
  NSelect,
} from 'naive-ui'
import type { UploadFileInfo } from 'naive-ui'
import { useAsyncTask } from '../composables/useAsyncTask'
import { usePanZoomLinkage } from '../composables/usePanZoomLinkage'
import { usePanZoomGroups } from '../composables/usePanZoomGroups'
import { useRasterToolUpload } from '../composables/useRasterToolUpload'
import { useObjectUrlLifecycle } from '../composables/useObjectUrlLifecycle'
import { useBlobDownload } from '../composables/useBlobDownload'
import ZoomableImageViewport from './common/ZoomableImageViewport.vue'
import type { VectorizeParams, VectorizeTaskStatus } from '../types'
import { useAppStore } from '../stores/app'
import {
  defaultVectorizeParams,
  normalizeVectorizeParams,
} from '../domain/vectorize/normalizeVectorizeParams'
import { toErrorMessage } from '../runtime/error'
import { fetchTextWithSession } from '../runtime/protectedRequest'
import {
  fetchVectorizeDefaults,
  fetchVectorizeTaskStatus,
  getVectorizeSvgPath,
  submitVectorize,
} from '../services/vectorizeService'

// ── File state ───────────────────────────────────────────────────────────

const svgBlobUrl = ref<string | null>(null)
const { createUrl, revokeUrl } = useObjectUrlLifecycle()
const appStore = useAppStore()
const params = ref<VectorizeParams>({
  ...defaultVectorizeParams,
})

const upload = useRasterToolUpload({
  onReset: () => {
    resetTask()
    revokeBlobUrls()
    svgContent.value = null
    panZoomGroups.resetAll()
  },
  onError: (message) => {
    error.value = message
  },
})

const {
  backendMaxUploadMb,
  clearFile,
  file,
  fileList,
  handleUploadChange,
  imageInfo,
  maxPixelText,
  originalUrl,
  rasterImageAccept,
  rasterImageFormatsText,
} = upload

const submitParams = computed<VectorizeParams>(() =>
  normalizeVectorizeParams(params.value, defaultVectorizeParams),
)

// ── Task management ─────────────────────────────────────────────────────

const svgContent = ref<string | null>(null)

const {
  taskId,
  status: taskStatus,
  loading,
  error,
  isCompleted,
  submit: submitTask,
  reset: resetTask,
} = useAsyncTask<VectorizeTaskStatus>(
  () => submitVectorize(file.value!, submitParams.value),
  fetchVectorizeTaskStatus,
  {
    onCompleted() {
      fetchSvgBlob(taskId.value!)
    },
  },
)
const { downloadByObjectUrl } = useBlobDownload((message) => {
  error.value = message
})

const canExecute = computed(() => file.value !== null && !loading.value)

async function handleVectorize() {
  if (!file.value) return
  revokeBlobUrls()
  svgContent.value = null
  panZoomGroups.resetAll()
  await submitTask()
}

async function fetchSvgBlob(id: string) {
  try {
    const text = await fetchTextWithSession(getVectorizeSvgPath(id))
    if (taskId.value !== id) return
    svgContent.value = text
    const blob = new Blob([text], { type: 'image/svg+xml' })
    revokeUrl(svgBlobUrl.value)
    svgBlobUrl.value = createUrl(blob)
  } catch (e: unknown) {
    if (taskId.value !== id) return
    error.value = toErrorMessage(e, '获取结果失败')
  }
}

const panZoomGroups = usePanZoomGroups({
  upload: 'upload',
  original: 'compare',
  result: 'compare',
})

const { groupValueFor, linkageGroupOptions, linkageMode, linkageModeOptions, setViewGroup } =
  usePanZoomLinkage({
    panZoomGroups,
    linkedGroups: {
      original: 'A',
      result: 'A',
    },
    independentGroups: {
      original: 'self:original',
      result: 'self:result',
    },
  })

// ── File management ──────────────────────────────────────────────────────

function revokeBlobUrls() {
  revokeUrl(svgBlobUrl.value)
  svgBlobUrl.value = null
}

// ── Downloads ────────────────────────────────────────────────────────────

async function handleDownloadSvg() {
  if (!svgBlobUrl.value) return
  try {
    await downloadByObjectUrl(svgBlobUrl.value, `${fileBaseName.value}.svg`)
  } catch {
    // handled by useBlobDownload callback
  }
}

function handleUseSvgForConvert() {
  if (!svgContent.value) return
  const resultFile = new File([svgContent.value], `${fileBaseName.value}.svg`, {
    type: 'image/svg+xml',
  })
  appStore.setSelectedFile(resultFile)
  appStore.activeTab = 'convert'
}

// ── Computed helpers ─────────────────────────────────────────────────────

const fileBaseName = computed(() => {
  if (!file.value) return 'image'
  const name = file.value.name
  const dot = name.lastIndexOf('.')
  return dot > 0 ? name.substring(0, dot) : name
})

const timingText = computed(() => {
  const t = taskStatus.value?.timing
  if (!t) return null
  const parts: string[] = []
  if (t.decode_ms > 0) parts.push(`解码 ${t.decode_ms.toFixed(0)}ms`)
  if (t.vectorize_ms > 0) parts.push(`矢量化 ${t.vectorize_ms.toFixed(0)}ms`)
  parts.push(`总计 ${t.pipeline_ms.toFixed(0)}ms`)
  return parts.join(' | ')
})

const svgSizeText = computed(() => {
  const size = taskStatus.value?.svg_size
  if (!size) return null
  if (size < 1024) return `${size} B`
  if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KB`
  return `${(size / 1024 / 1024).toFixed(2)} MB`
})

// ── Lifecycle ────────────────────────────────────────────────────────────

onMounted(async () => {
  try {
    const serverDefaults = await fetchVectorizeDefaults()
    params.value = normalizeVectorizeParams(
      {
        ...params.value,
        ...serverDefaults,
      },
      defaultVectorizeParams,
    )
  } catch {
    // Fallback to local defaults when server endpoint is unavailable.
  }
})
</script>

<template>
  <NSpace vertical :size="16">
    <NGrid :cols="2" :x-gap="16" responsive="screen" item-responsive>
      <NGridItem span="2 m:1">
        <NCard title="图片上传" size="small">
          <NUpload
            v-if="!file"
            :accept="rasterImageAccept"
            :max="1"
            :default-upload="false"
            :file-list="fileList"
            :show-file-list="false"
            @update:file-list="
              (v: UploadFileInfo[]) => {
                fileList = v
              }
            "
            @change="handleUploadChange"
          >
            <NUploadDragger>
              <NSpace vertical align="center" justify="center" style="padding: 32px 16px">
                <NText depth="3" style="font-size: 14px"> 点击或拖拽图片到此处上传 </NText>
                <NText depth="3" style="font-size: 12px">
                  支持 {{ rasterImageFormatsText }} 格式
                </NText>
                <NText depth="3" style="font-size: 11px">
                  文件最大 {{ backendMaxUploadMb }}MB，位图最大 {{ maxPixelText }} 像素
                </NText>
              </NSpace>
            </NUploadDragger>
          </NUpload>
          <div v-else class="upload-preview">
            <div class="upload-preview-header">
              <NText depth="3" style="font-size: 12px">{{ imageInfo }}</NText>
              <NButton size="tiny" quaternary type="error" @click="clearFile"> 移除图片 </NButton>
            </div>
            <ZoomableImageViewport
              :src="originalUrl ?? undefined"
              alt="original"
              :height="260"
              :controller="panZoomGroups.controllerFor('upload')"
            />
          </div>
        </NCard>
      </NGridItem>

      <NGridItem span="2 m:1">
        <NCard title="矢量化设置" size="small">
          <NSpace vertical :size="12">
            <div>
              <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                颜色数量
              </NText>
              <NInputNumber
                v-model:value="params.num_colors"
                :min="2"
                :max="256"
                :disabled="loading"
                size="small"
                style="width: 100%"
              />
              <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                越小越“卡通化”，越大越接近原图但文件更大、处理更慢。建议先用 12~24。
              </NText>
            </div>
            <div>
              <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                曲线拟合精度（值越小越贴近原边界）
              </NText>
              <NInputNumber
                v-model:value="params.curve_fit_error"
                :min="0.1"
                :max="5"
                :step="0.1"
                :precision="2"
                :disabled="loading"
                size="small"
                style="width: 100%"
              />
              <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                越小越贴边界（细节更多，路径更复杂）；越大越平滑（更简洁，但可能丢拐角）。
                建议 0.6~1.0。
              </NText>
            </div>
            <div>
              <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                轮廓简化强度（值越大节点越少）
              </NText>
              <NInputNumber
                v-model:value="params.contour_simplify"
                :min="0"
                :max="10"
                :step="0.05"
                :precision="2"
                :disabled="loading"
                size="small"
                style="width: 100%"
              />
              <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                控制“减少节点”的力度。值大：SVG 更小更顺滑；值小：保细节更好。建议 0.35~0.8。
              </NText>
            </div>
            <div>
              <div
                style="display: flex; align-items: center; justify-content: space-between; gap: 8px"
              >
                <NText depth="3" style="font-size: 12px">启用细线描边增强</NText>
                <NSwitch v-model:value="params.svg_enable_stroke" :disabled="loading" />
              </div>
              <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                适合线稿、手绘、细边缘。若出现“边缘描得太重”，可先关闭再比较。
              </NText>
            </div>
            <div>
              <div
                style="display: flex; align-items: center; justify-content: space-between; gap: 8px"
              >
                <NText depth="3" style="font-size: 12px">启用覆盖修复</NText>
                <NSwitch v-model:value="params.enable_coverage_fix" :disabled="loading" />
              </div>
              <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                用于修补“区域缺块/漏填充”。一般建议保持开启，仅在边缘过厚时再尝试关闭。
              </NText>
            </div>
            <NText depth="3" style="font-size: 11px">
              建议先调“颜色数量 + 曲线拟合精度 + 轮廓简化”，其余保持默认。
            </NText>
            <NAlert type="info" :show-icon="false">
              <NText depth="3" style="font-size: 12px; display: block">
                推荐调参顺序：
              </NText>
              <NText depth="3" style="font-size: 11px; display: block">
                1) 先调“颜色数量”控制整体色块层次；
              </NText>
              <NText depth="3" style="font-size: 11px; display: block">
                2) 再调“曲线拟合精度”平衡边缘贴合与平滑；
              </NText>
              <NText depth="3" style="font-size: 11px; display: block">
                3) 最后用“轮廓简化强度”控制文件大小和节点数量。
              </NText>
            </NAlert>
            <NCollapse>
              <NCollapseItem title="高级参数（通常保持默认）" name="advanced">
                <NSpace vertical :size="10">
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      预处理空间平滑（Mean Shift sp）
                    </NText>
                    <NInputNumber
                      v-model:value="params.smoothing_spatial"
                      :min="0"
                      :max="50"
                      :step="0.5"
                      :precision="1"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      控制“按空间邻近”做平滑。增大可去噪并让色块更整齐，但可能糊掉小细节。
                    </NText>
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      预处理颜色平滑（Mean Shift sr）
                    </NText>
                    <NInputNumber
                      v-model:value="params.smoothing_color"
                      :min="0"
                      :max="80"
                      :step="0.5"
                      :precision="1"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      控制“颜色差异”可被合并的力度。增大后颜色更统一，但可能丢失微小色阶。
                    </NText>
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      细线检测半径（像素）
                    </NText>
                    <NInputNumber
                      v-model:value="params.thin_line_max_radius"
                      :min="0.5"
                      :max="10"
                      :step="0.1"
                      :precision="1"
                      :disabled="loading || !params.svg_enable_stroke"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      决定多细的结构会被当作“线条”处理。值大保细线更积极，也可能把窄色块当线条。
                    </NText>
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      大图预处理像素上限
                    </NText>
                    <NInputNumber
                      v-model:value="params.max_working_pixels"
                      :min="0"
                      :max="100000000"
                      :step="100000"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      输入像素超过该值时会先缩小再矢量化，可显著减少 SVG 体积；设为 0 可禁用。
                    </NText>
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      SLIC 超像素尺寸
                    </NText>
                    <NInputNumber
                      v-model:value="params.slic_region_size"
                      :min="0"
                      :max="100"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      超像素的目标大小。越小越精细（保细节更多但更慢），越大越快但边缘粗糙。建议
                      15~30。
                    </NText>
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      边缘对齐灵敏度
                    </NText>
                    <NInputNumber
                      v-model:value="params.edge_sensitivity"
                      :min="0"
                      :max="1"
                      :step="0.05"
                      :precision="2"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      SLIC
                      超像素在边缘处降低空间权重的力度。值越大超像素边界越贴合图像边缘；设为 0
                      则退化为标准 SLIC。建议 0.6~1.0。
                    </NText>
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      边界细化迭代次数
                    </NText>
                    <NInputNumber
                      v-model:value="params.refine_passes"
                      :min="0"
                      :max="20"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      对边界像素进行逐像素重分配的轮次。次数越多边缘越锐利但更慢；设为 0
                      禁用细化。建议 4~8。
                    </NText>
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      合并颜色容差
                    </NText>
                    <NInputNumber
                      v-model:value="params.max_merge_color_dist"
                      :min="0"
                      :max="2000"
                      :step="10"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      小区域合并时允许的最大 LAB 颜色距离²。值越小越保留高对比小细节（如细线、文字），值越大碎块更少但可能吞噬细特征。建议
                      100~300。
                    </NText>
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      最小区域面积（像素²）
                    </NText>
                    <NInputNumber
                      v-model:value="params.min_region_area"
                      :min="0"
                      :max="1000000"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      小于该面积的碎块会被并入邻近区域。增大可减少噪点，但会吞掉小细节。
                    </NText>
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      最小轮廓面积（像素²）
                    </NText>
                    <NInputNumber
                      v-model:value="params.min_contour_area"
                      :min="0"
                      :max="1000000"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      小于该面积的闭合形状直接忽略。适合清理噪点，过大可能丢失小图案。
                    </NText>
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      最小孔洞面积（像素²）
                    </NText>
                    <NInputNumber
                      v-model:value="params.min_hole_area"
                      :min="0"
                      :max="100000"
                      :step="0.5"
                      :precision="1"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      小于该面积的孔洞会被填平。想保留镂空细节时请适当调小。
                    </NText>
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      最低覆盖率（低于该值触发覆盖修复）
                    </NText>
                    <NInputNumber
                      v-model:value="params.min_coverage_ratio"
                      :min="0"
                      :max="1"
                      :step="0.001"
                      :precision="3"
                      :disabled="loading || !params.enable_coverage_fix"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      越接近 1，系统越容易触发“补漏填充”。出现缺块时可调高（如 0.999）。
                    </NText>
                  </div>
                  <div>
                    <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
                      结果描边宽度（像素）
                    </NText>
                    <NInputNumber
                      v-model:value="params.svg_stroke_width"
                      :min="0"
                      :max="20"
                      :step="0.1"
                      :precision="1"
                      :disabled="loading || !params.svg_enable_stroke"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      仅在“启用细线描边增强”时生效。太大容易糊边，通常 0.3~0.8 更自然。
                    </NText>
                  </div>
                </NSpace>
              </NCollapseItem>
            </NCollapse>
            <NButton
              type="primary"
              block
              :loading="loading"
              :disabled="!canExecute"
              @click="handleVectorize"
            >
              {{ loading ? '处理中...' : '开始矢量化' }}
            </NButton>
            <NButton v-if="isCompleted && svgBlobUrl" block @click="handleDownloadSvg">
              下载 SVG{{ svgSizeText ? ` (${svgSizeText})` : '' }}
            </NButton>
            <NButton v-if="isCompleted && svgContent" type="success" block @click="handleUseSvgForConvert">
              使用 SVG 结果进行图像转换
            </NButton>
          </NSpace>
        </NCard>
      </NGridItem>
    </NGrid>

    <NAlert v-if="error" type="error" closable @close="error = null">
      {{ error }}
    </NAlert>

    <div v-if="loading" style="text-align: center; padding: 40px 0">
      <NSpin size="large" />
      <NText depth="3" style="display: block; margin-top: 12px">
        {{ taskStatus?.status === 'pending' ? '排队等待中...' : '正在进行矢量化处理...' }}
      </NText>
    </div>

    <NCard v-if="isCompleted && svgBlobUrl" title="矢量化结果" size="small">
      <template #header-extra>
        <NSpace :size="8" align="center">
          <NText depth="3" style="font-size: 12px">
            {{ taskStatus!.width }} x {{ taskStatus!.height }} | {{ taskStatus!.num_shapes }} 个形状
            | SVG {{ svgSizeText }}
          </NText>
          <NButton size="tiny" quaternary @click="panZoomGroups.resetAll"> 重置视图 </NButton>
        </NSpace>
      </template>
      <NText
        v-if="timingText"
        depth="3"
        style="font-size: 11px; display: block; margin-bottom: 8px; font-family: monospace"
      >
        {{ timingText }}
      </NText>
      <NText depth="3" style="font-size: 11px; display: block; margin-bottom: 8px">
        滚轮缩放，拖拽移动，可对比矢量化效果
      </NText>
      <NSpace align="center" :size="8" class="linkage-toolbar">
        <NText depth="3" class="linkage-toolbar__label">联动模式</NText>
        <NSelect
          v-model:value="linkageMode"
          size="small"
          :options="linkageModeOptions"
          style="width: 140px"
        />
      </NSpace>
      <div v-if="linkageMode === 'custom'" class="linkage-custom-grid">
        <NText depth="3" class="linkage-custom-grid__label">原图</NText>
        <NSelect
          :value="groupValueFor('original')"
          size="small"
          :options="linkageGroupOptions"
          @update:value="(value) => setViewGroup('original', String(value ?? 'A'))"
        />
        <NText depth="3" class="linkage-custom-grid__label">SVG 结果</NText>
        <NSelect
          :value="groupValueFor('result')"
          size="small"
          :options="linkageGroupOptions"
          @update:value="(value) => setViewGroup('result', String(value ?? 'A'))"
        />
      </div>
      <div class="preview-row">
        <div class="preview-col">
          <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
            原图
          </NText>
          <ZoomableImageViewport
            :src="originalUrl ?? undefined"
            alt="original"
            :height="400"
            :controller="panZoomGroups.controllerFor('original')"
          />
        </div>
        <div class="preview-col">
          <NText depth="3" style="font-size: 12px; margin-bottom: 4px; display: block">
            SVG 结果
          </NText>
          <ZoomableImageViewport
            :src="svgBlobUrl ?? undefined"
            alt="svg result"
            :height="400"
            :controller="panZoomGroups.controllerFor('result')"
          />
        </div>
      </div>
    </NCard>
  </NSpace>
</template>

<style scoped>
.upload-preview {
  text-align: center;
}

.upload-preview-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 8px;
}

.preview-row {
  display: flex;
  gap: 12px;
}

.preview-col {
  flex: 1;
  min-width: 0;
}

.linkage-toolbar {
  margin-bottom: 8px;
}

.linkage-toolbar__label {
  font-size: 12px;
}

.linkage-custom-grid {
  display: grid;
  grid-template-columns: auto minmax(0, 180px);
  gap: 8px 10px;
  align-items: center;
  margin-bottom: 8px;
}

.linkage-custom-grid__label {
  font-size: 12px;
}

@media (max-width: 768px) {
  .preview-row {
    flex-direction: column;
  }
}
</style>
