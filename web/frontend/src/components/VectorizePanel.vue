<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
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
import type { UploadFileInfo, SelectOption } from 'naive-ui'
import {
  submitVectorize,
  fetchVectorizeDefaults,
  fetchVectorizeTaskStatus,
  getVectorizeSvgPath,
} from '../api'
import { useAsyncTask } from '../composables/useAsyncTask'
import { usePanZoomGroups } from '../composables/usePanZoomGroups'
import { useObjectUrlLifecycle } from '../composables/useObjectUrlLifecycle'
import { useBlobDownload } from '../composables/useBlobDownload'
import ZoomableImageViewport from './common/ZoomableImageViewport.vue'
import type { VectorizeParams, VectorizeTaskStatus } from '../types'
import { useAppStore } from '../stores/app'
import {
  RASTER_IMAGE_ACCEPT,
  RASTER_IMAGE_FORMATS_TEXT,
  validateImageUploadFile,
} from '../domain/upload/imageUploadValidation'
import { getUploadMaxMb, getUploadMaxPixels } from '../runtime/env'
import { roundTo } from '../runtime/number'
import { fetchTextWithSession } from '../runtime/protectedRequest'

// ── File state ───────────────────────────────────────────────────────────

const file = ref<File | null>(null)
const fileList = ref<UploadFileInfo[]>([])
const originalUrl = ref<string | null>(null)
const svgBlobUrl = ref<string | null>(null)
const { createUrl, revokeUrl } = useObjectUrlLifecycle()
const appStore = useAppStore()
const backendMaxUploadMb = getUploadMaxMb()
const maxPixelText = getUploadMaxPixels().toLocaleString('zh-CN')

// ── Parameters ───────────────────────────────────────────────────────────

const defaultParams = {
  num_colors: 16,
  min_region_area: 10,
  curve_fit_error: 0.8,
  corner_angle_threshold: 135,
  smoothing_spatial: 15,
  smoothing_color: 25,
  upscale_short_edge: 600,
  max_working_pixels: 3000000,
  slic_region_size: 20,
  thin_line_max_radius: 2.5,
  min_contour_area: 10,
  min_hole_area: 4.0,
  contour_simplify: 0.45,
  topology_cleanup: 0.15,
  enable_coverage_fix: true,
  min_coverage_ratio: 0.998,
  svg_enable_stroke: true,
  svg_stroke_width: 0.5,
} satisfies Required<
  Pick<
    VectorizeParams,
    | 'num_colors'
    | 'min_region_area'
    | 'curve_fit_error'
    | 'corner_angle_threshold'
    | 'smoothing_spatial'
    | 'smoothing_color'
    | 'upscale_short_edge'
    | 'max_working_pixels'
    | 'slic_region_size'
    | 'thin_line_max_radius'
    | 'min_contour_area'
    | 'min_hole_area'
    | 'contour_simplify'
    | 'topology_cleanup'
    | 'enable_coverage_fix'
    | 'min_coverage_ratio'
    | 'svg_enable_stroke'
    | 'svg_stroke_width'
  >
>

const params = ref<VectorizeParams>({
  ...defaultParams,
})

function normalizeNumber(
  value: unknown,
  fallback: number,
  min: number,
  max: number,
  integer = false,
  precision?: number,
): number {
  let out = typeof value === 'number' && Number.isFinite(value) ? value : fallback
  if (integer) out = Math.round(out)
  else if (typeof precision === 'number') out = roundTo(out, precision)
  return Math.min(max, Math.max(min, out))
}

function normalizeBoolean(value: unknown, fallback: boolean): boolean {
  return typeof value === 'boolean' ? value : fallback
}

const submitParams = computed<VectorizeParams>(() => {
  const p = params.value
  const out: VectorizeParams = {
    num_colors: normalizeNumber(p.num_colors, defaultParams.num_colors, 2, 256, true),
    curve_fit_error: normalizeNumber(
      p.curve_fit_error,
      defaultParams.curve_fit_error,
      0.1,
      5,
      false,
      2,
    ),
    corner_angle_threshold: normalizeNumber(
      p.corner_angle_threshold,
      defaultParams.corner_angle_threshold,
      90,
      175,
      false,
      1,
    ),
    smoothing_spatial: normalizeNumber(
      p.smoothing_spatial,
      defaultParams.smoothing_spatial,
      0,
      50,
      false,
      1,
    ),
    smoothing_color: normalizeNumber(
      p.smoothing_color,
      defaultParams.smoothing_color,
      0,
      80,
      false,
      1,
    ),
    upscale_short_edge: normalizeNumber(
      p.upscale_short_edge,
      defaultParams.upscale_short_edge,
      0,
      2000,
      true,
    ),
    max_working_pixels: normalizeNumber(
      p.max_working_pixels,
      defaultParams.max_working_pixels,
      0,
      100000000,
      true,
    ),
    slic_region_size: normalizeNumber(
      p.slic_region_size,
      defaultParams.slic_region_size,
      0,
      100,
      true,
    ),
    thin_line_max_radius: normalizeNumber(
      p.thin_line_max_radius,
      defaultParams.thin_line_max_radius,
      0.5,
      10,
      false,
      1,
    ),
    min_region_area: normalizeNumber(
      p.min_region_area,
      defaultParams.min_region_area,
      0,
      1000000,
      true,
    ),
    min_contour_area: normalizeNumber(
      p.min_contour_area,
      defaultParams.min_contour_area,
      0,
      1000000,
    ),
    min_hole_area: normalizeNumber(p.min_hole_area, defaultParams.min_hole_area, 0, 1000000, false, 1),
    contour_simplify: normalizeNumber(
      p.contour_simplify,
      defaultParams.contour_simplify,
      0,
      10,
      false,
      2,
    ),
    topology_cleanup: normalizeNumber(
      p.topology_cleanup,
      defaultParams.topology_cleanup,
      0,
      10,
      false,
      2,
    ),
    enable_coverage_fix: normalizeBoolean(p.enable_coverage_fix, defaultParams.enable_coverage_fix),
    min_coverage_ratio: normalizeNumber(
      p.min_coverage_ratio,
      defaultParams.min_coverage_ratio,
      0,
      1,
      false,
      3,
    ),
    svg_enable_stroke: normalizeBoolean(p.svg_enable_stroke, defaultParams.svg_enable_stroke),
    svg_stroke_width: normalizeNumber(
      p.svg_stroke_width,
      defaultParams.svg_stroke_width,
      0,
      20,
      false,
      1,
    ),
  }
  return out
})

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
    error.value = e instanceof Error ? e.message : '获取结果失败'
  }
}

const panZoomGroups = usePanZoomGroups({
  upload: 'upload',
  original: 'compare',
  result: 'compare',
})

type LinkageMode = 'linked' | 'independent' | 'custom'

const linkageMode = ref<LinkageMode>('linked')
const linkageModeOptions: SelectOption[] = [
  { label: '全部联动', value: 'linked' },
  { label: '全部独立', value: 'independent' },
  { label: '自定义分组', value: 'custom' },
]
const linkageGroupOptions: SelectOption[] = [
  { label: 'A 组', value: 'A' },
  { label: 'B 组', value: 'B' },
  { label: '独立', value: '__self__' },
]

function applyLinkageMode(mode: LinkageMode): void {
  if (mode === 'linked') {
    panZoomGroups.setGroups({
      original: 'A',
      result: 'A',
    })
    panZoomGroups.resetAll()
    return
  }
  if (mode === 'independent') {
    panZoomGroups.setGroups({
      original: 'self:original',
      result: 'self:result',
    })
    panZoomGroups.resetAll()
  }
}

function groupValueFor(viewId: 'original' | 'result'): string {
  const group = panZoomGroups.getViewGroup(viewId)
  return group.startsWith('self:') ? '__self__' : group
}

function setViewGroup(viewId: 'original' | 'result', value: string): void {
  if (value === '__self__') {
    panZoomGroups.setViewGroup(viewId, `self:${viewId}`)
  } else {
    panZoomGroups.setViewGroup(viewId, value)
  }
  panZoomGroups.resetAll()
}

watch(
  () => linkageMode.value,
  (mode) => {
    if (mode === 'custom') return
    applyLinkageMode(mode)
  },
  { immediate: true },
)

// ── File management ──────────────────────────────────────────────────────

async function handleUploadChange(options: { fileList: UploadFileInfo[] }) {
  const files = options.fileList
  if (files.length === 0) {
    clearFile()
    return
  }
  const latest = files[files.length - 1]
  if (latest?.file) {
    const validation = await validateImageUploadFile(latest.file, 'raster-tool')
    if (!validation.ok) {
      clearFile()
      error.value = validation.message
      return
    }
    error.value = null
    file.value = latest.file
  }
}

function revokeBlobUrls() {
  revokeUrl(svgBlobUrl.value)
  svgBlobUrl.value = null
}

function clearFile() {
  file.value = null
  fileList.value = []
  resetTask()
  revokeBlobUrls()
  svgContent.value = null
  revokeUrl(originalUrl.value)
  originalUrl.value = null
  panZoomGroups.resetAll()
}

watch(file, (f) => {
  revokeUrl(originalUrl.value)
  originalUrl.value = null
  if (f) {
    originalUrl.value = createUrl(f)
  }
  resetTask()
  revokeBlobUrls()
  svgContent.value = null
  panZoomGroups.resetAll()
})

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

const imageInfo = computed(() => {
  if (!file.value) return null
  const sizeMB = (file.value.size / 1024 / 1024).toFixed(2)
  return `${file.value.name} (${sizeMB} MB)`
})

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
    params.value = {
      ...params.value,
      ...serverDefaults,
      num_colors: normalizeNumber(serverDefaults.num_colors, defaultParams.num_colors, 2, 256, true),
      curve_fit_error: normalizeNumber(
        serverDefaults.curve_fit_error,
        defaultParams.curve_fit_error,
        0.1,
        5,
        false,
        2,
      ),
      corner_angle_threshold: normalizeNumber(
        serverDefaults.corner_angle_threshold,
        defaultParams.corner_angle_threshold,
        90,
        175,
        false,
        1,
      ),
      smoothing_spatial: normalizeNumber(
        serverDefaults.smoothing_spatial,
        defaultParams.smoothing_spatial,
        0,
        50,
        false,
        1,
      ),
      smoothing_color: normalizeNumber(
        serverDefaults.smoothing_color,
        defaultParams.smoothing_color,
        0,
        80,
        false,
        1,
      ),
      upscale_short_edge: normalizeNumber(
        serverDefaults.upscale_short_edge,
        defaultParams.upscale_short_edge,
        0,
        2000,
        true,
      ),
      max_working_pixels: normalizeNumber(
        serverDefaults.max_working_pixels,
        defaultParams.max_working_pixels,
        0,
        100000000,
        true,
      ),
      slic_region_size: normalizeNumber(
        serverDefaults.slic_region_size,
        defaultParams.slic_region_size,
        0,
        100,
        true,
      ),
      thin_line_max_radius: normalizeNumber(
        serverDefaults.thin_line_max_radius,
        defaultParams.thin_line_max_radius,
        0.5,
        10,
        false,
        1,
      ),
      min_region_area: normalizeNumber(
        serverDefaults.min_region_area,
        defaultParams.min_region_area,
        0,
        1000000,
        true,
      ),
      min_contour_area: normalizeNumber(
        serverDefaults.min_contour_area,
        defaultParams.min_contour_area,
        0,
        1000000,
      ),
      min_hole_area: normalizeNumber(
        serverDefaults.min_hole_area,
        defaultParams.min_hole_area,
        0,
        1000000,
        false,
        1,
      ),
      contour_simplify: normalizeNumber(
        serverDefaults.contour_simplify,
        defaultParams.contour_simplify,
        0,
        10,
        false,
        2,
      ),
      topology_cleanup: normalizeNumber(
        serverDefaults.topology_cleanup,
        defaultParams.topology_cleanup,
        0,
        10,
        false,
        2,
      ),
      enable_coverage_fix: normalizeBoolean(
        serverDefaults.enable_coverage_fix,
        defaultParams.enable_coverage_fix,
      ),
      min_coverage_ratio: normalizeNumber(
        serverDefaults.min_coverage_ratio,
        defaultParams.min_coverage_ratio,
        0,
        1,
        false,
        3,
      ),
      svg_enable_stroke: normalizeBoolean(
        serverDefaults.svg_enable_stroke,
        defaultParams.svg_enable_stroke,
      ),
      svg_stroke_width: normalizeNumber(
        serverDefaults.svg_stroke_width,
        defaultParams.svg_stroke_width,
        0,
        20,
        false,
        1,
      ),
    }
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
            :accept="RASTER_IMAGE_ACCEPT"
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
                  支持 {{ RASTER_IMAGE_FORMATS_TEXT }} 格式
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
                      拓扑清理强度（值越大更简化）
                    </NText>
                    <NInputNumber
                      v-model:value="params.topology_cleanup"
                      :min="0"
                      :max="10"
                      :step="0.05"
                      :precision="2"
                      :disabled="loading"
                      size="small"
                      style="width: 100%"
                    />
                    <NText depth="3" style="font-size: 11px; display: block; margin-top: 4px">
                      清理复杂边界的力度。值大更稳更简洁，值小更保留原始细节。
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
                      :max="5"
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
