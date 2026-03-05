<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { storeToRefs } from 'pinia'
import {
  NCard,
  NFormItem,
  NSelect,
  NInputNumber,
  NSwitch,
  NSlider,
  NCollapse,
  NCollapseItem,
  NSpin,
  NAlert,
  NTooltip,
} from 'naive-ui'
import type { SelectOption } from 'naive-ui'
import { fetchDefaults, fetchColorDBs } from '../api'
import type { ConvertAnyParams, ColorDBInfo, DefaultConfig, PaletteChannel } from '../types'
import { PIXEL_SIZE_PRESETS } from '../types'
import { createInitialConvertParams } from '../domain/params/convertDefaults'
import { formatFloat, roundTo } from '../runtime/number'
import { useAppStore } from '../stores/app'
import ParamModeSwitch from './param/ParamModeSwitch.vue'
import ColorDBSelector from './param/ColorDBSelector.vue'
import ChannelSelector from './param/ChannelSelector.vue'
import ParamSimpleSection from './param/ParamSimpleSection.vue'
import ParamAdvancedSection from './param/ParamAdvancedSection.vue'

defineProps<{
  disabled?: boolean
}>()

const appStore = useAppStore()
const { params: modelValue, inputType, imageDimensions, colordbVersion } = storeToRefs(appStore)

const loading = ref(true)
const error = ref<string | null>(null)
const colorDBs = ref<ColorDBInfo[]>([])
const defaults = ref<DefaultConfig | null>(null)

const mode = ref<'simple' | 'advanced'>('simple')

const isRaster = computed(() => inputType.value === 'raster')
const isVector = computed(() => inputType.value === 'vector')
const supportsModelGate = computed(() => isRaster.value || isVector.value)

// --- Material / Vendor filter ---
const selectedMaterial = ref('PLA')
const selectedVendor = ref('BambooLab')

// --- Simple mode local state ---
const targetWidthMm = ref(200)
const targetHeightMm = ref(200)
const selectedPixelPresetIndex = ref(1) // default: 0.42mm
const customPixelMm = ref(0.42)
const targetDimensionUpperBound = 2000
const clusterCountUpperBound = 256
const slicTargetSuperpixelsUpperBound = 4096
const slicCompactnessUpperBound = 100
const slicIterationsUpperBound = 50
const simpleLabelWidth = 108
const inlineLabelWidth = 92
const formatTooltip1Decimal = (value: number) => formatFloat(value, 1)
const formatTooltip2Decimals = (value: number) => formatFloat(value, 2)

const effectivePixelMm = computed(() => {
  const preset = PIXEL_SIZE_PRESETS[selectedPixelPresetIndex.value]
  return preset && preset.value > 0 ? preset.value : customPixelMm.value
})
const isCustomPixel = computed(() => {
  const preset = PIXEL_SIZE_PRESETS[selectedPixelPresetIndex.value]
  return !preset || preset.value === 0
})

const pixelPresetOptions = computed<SelectOption[]>(() =>
  PIXEL_SIZE_PRESETS.map((p, i) => ({
    label: p.label,
    value: i,
  })),
)

// Sync simple mode state to params
watch([targetWidthMm, targetHeightMm, effectivePixelMm], () => {
  if (mode.value === 'simple' && isRaster.value) {
    update({
      target_width_mm: targetWidthMm.value,
      target_height_mm: targetHeightMm.value,
      pixel_mm: roundTo(effectivePixelMm.value, 2),
      max_width: 0,
      max_height: 0,
      scale: 1.0,
    })
  } else if (mode.value === 'simple' && isVector.value) {
    update({
      target_width_mm: targetWidthMm.value,
      target_height_mm: targetHeightMm.value,
    })
  }
})

// Output info for simple mode (raster only)
const simpleOutputInfo = computed(() => {
  if (!isRaster.value) return null
  const px = effectivePixelMm.value
  if (px <= 0) return null
  const maxPxW = Math.floor(targetWidthMm.value / px)
  const maxPxH = Math.floor(targetHeightMm.value / px)

  const dims = imageDimensions.value
  let actualPxW = maxPxW
  let actualPxH = maxPxH
  let upscaleRatio: number | null = null

  if (dims && dims.width > 0 && dims.height > 0) {
    const scaleW = maxPxW / dims.width
    const scaleH = maxPxH / dims.height
    const scale = Math.min(scaleW, scaleH)
    actualPxW = Math.round(dims.width * scale)
    actualPxH = Math.round(dims.height * scale)
    if (scale > 1.0) upscaleRatio = scale
  }

  return {
    maxPxW,
    maxPxH,
    actualPxW,
    actualPxH,
    actualWidthMm: +(actualPxW * px).toFixed(1),
    actualHeightMm: +(actualPxH * px).toFixed(1),
    upscaleRatio,
  }
})

// --- Common state ---

const colorSpaceOptions: SelectOption[] = [
  { label: 'Lab', value: 'lab' },
  { label: 'RGB', value: 'rgb' },
]

const ditherOptions: SelectOption[] = [
  { label: '关闭', value: 'none' },
  { label: 'Floyd-Steinberg', value: 'floyd_steinberg' },
]

const clusterMethodOptions: SelectOption[] = [
  { label: 'SLIC 超像素', value: 'slic' },
  { label: 'K-Means 聚类', value: 'kmeans' },
]

const gradientDitherOptions: SelectOption[] = [
  { label: '关闭', value: 'none' },
  { label: 'Floyd-Steinberg', value: 'floyd_steinberg' },
]

const tooltips = {
  print_mode:
    '决定色层数和层高。0.08mm x 5 层: 层高 0.08mm，5 层叠色；0.04mm x 10 层: 层高 0.04mm，10 层叠色。两者总叠色厚度相同 (0.4mm)',
  color_space: '颜色匹配时使用的色彩空间。Lab 更符合人眼感知（推荐）；RGB 为直接 RGB 距离计算',
  max_width: '图像缩放后的最大宽度（像素），超出时等比缩小。值越小处理越快，输出模型越小',
  max_height: '图像缩放后的最大高度（像素），超出时等比缩小。值越小处理越快，输出模型越小',
  target_width_mm: '输出 3MF 模型的目标物理宽度（毫米），图像将被缩放以适应此尺寸',
  target_height_mm: '输出 3MF 模型的目标物理高度（毫米），图像将被缩放以适应此尺寸',
  pixel_mm_simple:
    '每个像素对应的物理线宽（毫米），由打印机喷嘴尺寸决定。0.4mm 喷嘴推荐 0.42mm，0.2mm 喷嘴推荐 0.22mm',
  cluster_method: '非抖动匹配时的聚类算法。SLIC 以空间连续性优先，边缘通常更稳定；K-Means 仅按颜色聚类',
  cluster_count:
    'K-Means 聚类数。值越大颜色越精细，0 或 1 表示不聚类（逐像素匹配）',
  slic_target_superpixels:
    'SLIC 目标超像素数量。值越大区域越细，边缘保留更好，但计算耗时更高',
  slic_compactness:
    'SLIC 紧凑度，控制颜色相似与空间规则性的权衡。值越小更贴合颜色边界，值越大区域更规整',
  slic_iterations: 'SLIC 迭代次数。更高可提升稳定性，但会增加耗时',
  slic_min_region_ratio:
    'SLIC 小区域并入比例（相对单个超像素期望面积）。增大可减少碎片区域，过大可能吞并细节',
  dither:
    '半色调抖动通过在相邻像素间交替使用不同配方来模拟更丰富的颜色过渡。选择 SLIC 时该选项会自动关闭',
  dither_strength:
    '抖动强度，控制颜色偏移幅度。值越大抖动效果越明显，但过高可能产生颗粒感。推荐 0.6-0.9',
  db_names: '用于颜色匹配的 ColorDB，支持多选。匹配时会在所有选中的数据库中寻找最佳配方',
  allowed_channels: '选择参与配方生成的颜色通道。未选中的通道将被排除，默认使用全部通道',
  scale: '在最大宽高限制之前先对图像进行等比缩放，1.0 表示不缩放',
  k_candidates: '颜色匹配时从 KD-Tree 中取 K 个最近邻候选，再从中选择最优。值越大匹配越精确但越慢',
  flip_y: '构建 3D 模型时翻转 Y 轴方向，开启时图像顶部对应模型顶部',
  pixel_mm: '每个像素对应的实际尺寸（毫米），决定输出模型的物理大小。0 表示自动从 ColorDB 配置推导',
  layer_height_mm: '3D 打印的层高（毫米），决定模型 Z 方向分辨率。0 表示自动从打印模式推导',
  model_enable:
    '是否启用神经网络模型辅助匹配。开启后，当 ColorDB 匹配质量不佳时会尝试用模型预测更优配方',
  model_only: '跳过 ColorDB 匹配，完全使用模型预测配方。需要加载模型包',
  model_threshold:
    '色差 (DeltaE) 阈值，仅当 ColorDB 匹配色差超过此值时才启用模型。负值使用模型包默认值',
  model_margin: '模型结果需优于 ColorDB 结果至少此色差值才会被采用。负值使用模型包默认值',
  generate_preview: '生成匹配后的颜色预览图（PNG），展示每个像素最终匹配到的颜色',
  generate_source_mask:
    '生成来源掩码图（PNG），白色像素表示使用了模型预测的配方，黑色表示使用了 ColorDB 匹配',
  tessellation_tolerance_mm:
    '矢量曲线三角化时的容差（毫米），值越小曲线越平滑但网格越多。推荐 0.05-0.2',
  gradient_dither: '对 SVG 渐变区域进行抖动处理，改善渐变在有限调色板下的过渡效果',
  gradient_dither_strength: '渐变区域的抖动强度。值越大渐变过渡越平滑，但可能产生颗粒感',
} as const

function update(partial: Partial<ConvertAnyParams>) {
  appStore.setParams({ ...modelValue.value, ...partial })
}

// When switching to simple mode, push target_mm fields;
// when switching to advanced, clear target_mm so backend uses max_width/max_height
watch(mode, (newMode) => {
  if (newMode === 'simple') {
    const base: Partial<ConvertAnyParams> = {
      target_width_mm: targetWidthMm.value,
      target_height_mm: targetHeightMm.value,
    }
    if (isRaster.value) {
      Object.assign(base, {
        pixel_mm: roundTo(effectivePixelMm.value, 2),
        max_width: 0,
        max_height: 0,
        scale: 1.0,
      })
    }
    update(base)
  } else {
    if (isRaster.value) {
      update({
        target_width_mm: 0,
        target_height_mm: 0,
      })
    }
  }
})

// When inputType changes, adjust params
watch(
  () => inputType.value,
  (newType) => {
    if (newType === 'vector') {
      update({
        tessellation_tolerance_mm: modelValue.value.tessellation_tolerance_mm ?? 0.1,
        gradient_dither: modelValue.value.gradient_dither ?? 'none',
        gradient_dither_strength: modelValue.value.gradient_dither_strength ?? 0.8,
      })
    }
    if (mode.value === 'simple') {
      update({
        target_width_mm: targetWidthMm.value,
        target_height_mm: targetHeightMm.value,
      })
    }
  },
)

watch(
  () => ({
    type: inputType.value,
    method: String(modelValue.value.cluster_method ?? 'kmeans').toLowerCase(),
    dither: modelValue.value.dither ?? 'none',
  }),
  ({ type, method, dither }) => {
    if (type !== 'raster') return
    if (dither === 'blue_noise') {
      update({ dither: 'none' })
      return
    }
    if (method === 'slic' && dither !== 'none') {
      update({ dither: 'none' })
    }
  },
  { immediate: true },
)

watch(
  () => modelValue.value.gradient_dither,
  (gradientDither) => {
    if (gradientDither === 'blue_noise') {
      update({ gradient_dither: 'none' })
    }
  },
  { immediate: true },
)

// --- Channel filtering ---

function channelKey(ch: PaletteChannel): string {
  return `${ch.color}|${ch.material}`
}

interface ChannelOption {
  key: string
  color: string
  material: string
}

const selectedChannelKeys = ref<string[]>([])

const availableChannels = computed<ChannelOption[]>(() => {
  const selectedNames = modelValue.value.db_names ?? []
  const seen = new Set<string>()
  const channels: ChannelOption[] = []
  for (const db of colorDBs.value) {
    if (!selectedNames.includes(db.name)) continue
    for (const ch of db.palette) {
      const k = channelKey(ch)
      if (!seen.has(k)) {
        seen.add(k)
        channels.push({ key: k, color: ch.color, material: ch.material })
      }
    }
  }
  return channels
})

const isAllChannelsSelected = computed(() => {
  return (
    availableChannels.value.length > 0 &&
    selectedChannelKeys.value.length === availableChannels.value.length
  )
})

function emitChannelUpdate() {
  const allKeys = new Set(availableChannels.value.map((c) => c.key))
  const selectedSet = new Set(selectedChannelKeys.value)
  if (
    selectedChannelKeys.value.length >= availableChannels.value.length &&
    [...allKeys].every((k) => selectedSet.has(k))
  ) {
    update({ allowed_channels: undefined })
  } else {
    const channels = availableChannels.value
      .filter((c) => selectedSet.has(c.key))
      .map((c) => ({ color: c.color, material: c.material }))
    update({ allowed_channels: channels })
  }
}

function handleChannelKeysChange(keys: string[]) {
  selectedChannelKeys.value = keys
  emitChannelUpdate()
}

// --- Channel presets ---

interface ChannelPreset {
  label: string
  value: string
  colors: string[] | null // null = select all
}

const CHANNEL_PRESETS: ChannelPreset[] = [
  { label: '全部', value: 'all', colors: null },
  { label: 'RYBW', value: 'rybw', colors: ['Red', 'Yellow', 'Blue', 'White'] },
  { label: 'CMYW', value: 'cmyw', colors: ['Cyan', 'Magenta', 'Yellow', 'White'] },
]

const applicablePresets = computed(() => {
  const availColors = new Set(availableChannels.value.map((ch) => ch.color))
  return CHANNEL_PRESETS.filter((p) => {
    if (!p.colors) return true
    return p.colors.every((c) => availColors.has(c))
  })
})

const activeChannelPreset = computed<string | null>(() => {
  if (isAllChannelsSelected.value) return 'all'
  const selectedColors = new Set(
    availableChannels.value
      .filter((ch) => selectedChannelKeys.value.includes(ch.key))
      .map((ch) => ch.color),
  )
  for (const preset of CHANNEL_PRESETS) {
    if (!preset.colors) continue
    if (
      preset.colors.length === selectedColors.size &&
      preset.colors.every((c) => selectedColors.has(c))
    ) {
      return preset.value
    }
  }
  return null
})

const clusterMethodValue = computed<'slic' | 'kmeans'>({
  get: () => {
    const raw = String(modelValue.value.cluster_method ?? 'kmeans').toLowerCase()
    return raw === 'slic' ? 'slic' : 'kmeans'
  },
  set: (next) => {
    const method = next === 'slic' ? 'slic' : 'kmeans'
    const patch: Partial<ConvertAnyParams> = { cluster_method: method }
    if (method === 'slic' && (modelValue.value.dither ?? 'none') !== 'none') {
      patch.dither = 'none'
    }
    update(patch)
  },
})

function setClusterMethod(value: string | null) {
  clusterMethodValue.value = value === 'slic' ? 'slic' : 'kmeans'
}

const useSlicCluster = computed(() => isRaster.value && clusterMethodValue.value === 'slic')

const clusterCountValue = computed<number>({
  get: () => {
    const raw = modelValue.value.cluster_count ?? 64
    if (!Number.isFinite(raw)) return 64
    return Math.min(clusterCountUpperBound, Math.max(0, Math.round(raw)))
  },
  set: (next) => {
    const value = Math.min(clusterCountUpperBound, Math.max(0, Math.round(next)))
    update({ cluster_count: value })
  },
})

function setClusterCount(value: number | null) {
  clusterCountValue.value = value ?? 64
}

const slicTargetSuperpixelsValue = computed<number>({
  get: () => {
    const raw = modelValue.value.slic_target_superpixels ?? 256
    if (!Number.isFinite(raw)) return 256
    return Math.min(slicTargetSuperpixelsUpperBound, Math.max(0, Math.round(raw)))
  },
  set: (next) => {
    const value = Math.min(slicTargetSuperpixelsUpperBound, Math.max(0, Math.round(next)))
    update({ slic_target_superpixels: value })
  },
})

function setSlicTargetSuperpixels(value: number | null) {
  slicTargetSuperpixelsValue.value = value ?? 256
}

const slicCompactnessValue = computed<number>({
  get: () => {
    const raw = modelValue.value.slic_compactness ?? 10
    if (!Number.isFinite(raw)) return 10
    return Math.min(slicCompactnessUpperBound, Math.max(0.1, roundTo(Number(raw), 1)))
  },
  set: (next) => {
    const value = Math.min(slicCompactnessUpperBound, Math.max(0.1, roundTo(Number(next), 1)))
    update({ slic_compactness: value })
  },
})

function setSlicCompactness(value: number | null) {
  slicCompactnessValue.value = value === null ? 10 : roundTo(value, 1)
}

const slicIterationsValue = computed<number>({
  get: () => {
    const raw = modelValue.value.slic_iterations ?? 10
    if (!Number.isFinite(raw)) return 10
    return Math.min(slicIterationsUpperBound, Math.max(1, Math.round(raw)))
  },
  set: (next) => {
    const value = Math.min(slicIterationsUpperBound, Math.max(1, Math.round(next)))
    update({ slic_iterations: value })
  },
})

function setSlicIterations(value: number | null) {
  slicIterationsValue.value = value ?? 10
}

const slicMinRegionRatioValue = computed<number>({
  get: () => {
    const raw = modelValue.value.slic_min_region_ratio ?? 0.25
    if (!Number.isFinite(raw)) return 0.25
    return Math.min(1, Math.max(0, roundTo(Number(raw), 2)))
  },
  set: (next) => {
    const value = Math.min(1, Math.max(0, roundTo(Number(next), 2)))
    update({ slic_min_region_ratio: value })
  },
})

function setSlicMinRegionRatio(value: number) {
  slicMinRegionRatioValue.value = roundTo(value, 2)
}

function applyChannelPreset(value: string) {
  const preset = CHANNEL_PRESETS.find((p) => p.value === value)
  if (!preset) return
  if (!preset.colors) {
    selectedChannelKeys.value = availableChannels.value.map((c) => c.key)
  } else {
    const target = new Set(preset.colors)
    selectedChannelKeys.value = availableChannels.value
      .filter((ch) => target.has(ch.color))
      .map((ch) => ch.key)
  }
  emitChannelUpdate()
}

watch(availableChannels, (newChannels, oldChannels) => {
  const oldKeys = new Set((oldChannels ?? []).map((c) => c.key))
  const newKeys = new Set(newChannels.map((c) => c.key))

  if (oldKeys.size === newKeys.size && [...oldKeys].every((k) => newKeys.has(k))) {
    return
  }

  let updated = selectedChannelKeys.value.filter((k) => newKeys.has(k))

  for (const ch of newChannels) {
    if (!oldKeys.has(ch.key) && !updated.includes(ch.key)) {
      updated.push(ch.key)
    }
  }

  if (updated.length === 0 && newChannels.length > 0) {
    updated = newChannels.map((c) => c.key)
  }

  selectedChannelKeys.value = updated
  emitChannelUpdate()
})

function buildDBOptions(dbs: ColorDBInfo[]): SelectOption[] {
  return dbs.map((db) => {
    const suffix = db.source === 'session' ? ' (自定义)' : ''
    return {
      label: `${db.name} (${db.num_entries} 色, ${db.num_channels} 通道)${suffix}`,
      value: db.name,
    }
  })
}

const materialOptions = computed<SelectOption[]>(() => {
  const materials = [
    ...new Set(
      colorDBs.value
        .filter((db) => db.source !== 'session' && db.material_type)
        .map((db) => db.material_type!),
    ),
  ]
  materials.sort()
  return materials.map((m) => ({ label: m, value: m }))
})

const vendorOptionsForMaterial = computed<SelectOption[]>(() => {
  const vendors = [
    ...new Set(
      colorDBs.value
        .filter(
          (db) =>
            db.source !== 'session' && db.material_type === selectedMaterial.value && db.vendor,
        )
        .map((db) => db.vendor!),
    ),
  ]
  vendors.sort()
  return vendors.map((v) => ({ label: v, value: v }))
})

const filteredDBs = computed(() =>
  colorDBs.value.filter((db) => {
    if (db.source === 'session') return true
    if (db.material_type !== selectedMaterial.value) return false
    return db.vendor === selectedVendor.value
  }),
)

const isLuminaPreset = computed(() => {
  return !(selectedMaterial.value === 'PLA' && selectedVendor.value === 'BambooLab')
})

const filteredDBOptions = computed<SelectOption[]>(() => buildDBOptions(filteredDBs.value))

const modelPackAvailable = computed(() => {
  return selectedMaterial.value === 'PLA' && selectedVendor.value === 'BambooLab'
})

function selectAllFilteredDBs() {
  update({ db_names: filteredDBs.value.map((db) => db.name) })
}

watch(selectedMaterial, () => {
  const available = vendorOptionsForMaterial.value.map((o) => o.value as string)
  if (!available.includes(selectedVendor.value)) {
    selectedVendor.value = available[0] ?? ''
  }
  selectAllFilteredDBs()
})

watch(selectedVendor, () => {
  selectAllFilteredDBs()
})

watch(modelPackAvailable, (available) => {
  if (supportsModelGate.value) {
    update({ model_enable: available })
  }
})

async function loadColorDBs() {
  try {
    const dbsData = await fetchColorDBs()
    colorDBs.value = dbsData
  } catch {
    // Keep existing options on refresh failure
  }
}

watch(
  () => colordbVersion.value,
  () => {
    loadColorDBs()
  },
)

onMounted(async () => {
  try {
    const [defaultsData, dbsData] = await Promise.all([fetchDefaults(), fetchColorDBs()])
    defaults.value = defaultsData
    colorDBs.value = dbsData

    // Pick best default material/vendor
    const globalDBs = dbsData.filter((db) => db.source !== 'session')
    const materials = [...new Set(globalDBs.map((db) => db.material_type).filter(Boolean))]
    if (materials.includes('PLA')) {
      selectedMaterial.value = 'PLA'
    } else if (materials.length > 0) {
      selectedMaterial.value = materials[0]!
    }
    const vendors = [
      ...new Set(
        globalDBs
          .filter((db) => db.material_type === selectedMaterial.value)
          .map((db) => db.vendor)
          .filter(Boolean),
      ),
    ]
    if (vendors.includes('BambooLab')) {
      selectedVendor.value = 'BambooLab'
    } else if (vendors.length > 0) {
      selectedVendor.value = vendors[0]!
    }

    const initialDbNames = filteredDBs.value.map((db) => db.name)

    appStore.setParams(
      createInitialConvertParams({
        defaults: defaultsData,
        targetWidthMm: targetWidthMm.value,
        targetHeightMm: targetHeightMm.value,
        pixelMm: effectivePixelMm.value,
        dbNames: initialDbNames,
      }),
    )
  } catch (e: unknown) {
    error.value = e instanceof Error ? e.message : '加载配置失败'
  } finally {
    loading.value = false
  }
})
</script>

<template>
  <NCard title="参数配置" size="small">
    <template #header-extra>
      <ParamModeSwitch v-model="mode" />
    </template>

    <NSpin :show="loading">
      <NAlert v-if="error" type="error" :title="error" style="margin-bottom: 12px" />

      <!-- ==================== SIMPLE MODE ==================== -->
      <ParamSimpleSection v-if="mode === 'simple'" :disabled="disabled || loading">
        <!-- Target width mm (shared) -->
        <NFormItem label-placement="left" :label-width="simpleLabelWidth">
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">目标宽度 (mm)</span>
              </template>
              {{ tooltips.target_width_mm }}
            </NTooltip>
          </template>
          <div class="slider-input-row">
            <NSlider
              v-model:value="targetWidthMm"
              :min="1"
              :max="targetDimensionUpperBound"
              :step="1"
              class="slider-input-row__slider"
            />
            <NInputNumber
              v-model:value="targetWidthMm"
              :min="1"
              :max="targetDimensionUpperBound"
              :step="1"
              :show-button="false"
              class="slider-input-row__input number-input-right"
            />
          </div>
        </NFormItem>

        <!-- Target height mm (shared) -->
        <NFormItem label-placement="left" :label-width="simpleLabelWidth">
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">目标高度 (mm)</span>
              </template>
              {{ tooltips.target_height_mm }}
            </NTooltip>
          </template>
          <div class="slider-input-row">
            <NSlider
              v-model:value="targetHeightMm"
              :min="1"
              :max="targetDimensionUpperBound"
              :step="1"
              class="slider-input-row__slider"
            />
            <NInputNumber
              v-model:value="targetHeightMm"
              :min="1"
              :max="targetDimensionUpperBound"
              :step="1"
              :show-button="false"
              class="slider-input-row__input number-input-right"
            />
          </div>
        </NFormItem>

        <div v-if="isRaster" class="param-inline-row">
          <!-- Pixel size preset (raster only) -->
          <NFormItem
            class="param-inline-item"
            label-placement="left"
            :label-width="inlineLabelWidth"
          >
            <template #label>
              <NTooltip>
                <template #trigger>
                  <span class="tip-label">像素尺寸 (mm)</span>
                </template>
                {{ tooltips.pixel_mm_simple }}
              </NTooltip>
            </template>
            <div class="pixel-size-row">
              <NSelect
                class="pixel-size-row__select"
                size="small"
                :value="selectedPixelPresetIndex"
                :options="pixelPresetOptions"
                @update:value="
                  (v: number) => {
                    selectedPixelPresetIndex = v
                  }
                "
              />
              <NInputNumber
                v-if="isCustomPixel"
                v-model:value="customPixelMm"
                :min="0.05"
                :max="5"
                :step="0.01"
                :precision="2"
                :show-button="false"
                class="pixel-size-row__input number-input-right"
                placeholder="自定义像素尺寸"
              />
            </div>
          </NFormItem>

          <!-- 打印模式暂不使用，先注释掉选择控件 -->
          <!--
          <NFormItem
            class="param-inline-item"
            label-placement="left"
            :label-width="inlineLabelWidth"
          >
            <template #label>
              <NTooltip>
                <template #trigger>
                  <span class="tip-label">打印模式</span>
                </template>
                {{ tooltips.print_mode }}
              </NTooltip>
            </template>
            <NSelect
              size="small"
              :value="modelValue.print_mode"
              :options="printModeOptions"
              @update:value="(v: string) => update({ print_mode: v })"
            />
          </NFormItem>
          -->
        </div>

        <!-- Tessellation tolerance (vector only) -->
        <NFormItem v-if="isVector" label-placement="left" :label-width="simpleLabelWidth">
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">三角化容差 (mm)</span>
              </template>
              {{ tooltips.tessellation_tolerance_mm }}
            </NTooltip>
          </template>
          <NInputNumber
            :value="modelValue.tessellation_tolerance_mm ?? 0.1"
            :min="0.01"
            :max="1"
            :step="0.01"
            :precision="2"
            @update:value="
              (v: number | null) => update({ tessellation_tolerance_mm: roundTo(v ?? 0.1, 2) })
            "
          />
        </NFormItem>

        <!-- 打印模式暂不使用，先注释掉选择控件 -->
        <!--
        <NFormItem v-if="!isRaster" label-placement="left" :label-width="simpleLabelWidth">
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">打印模式</span>
              </template>
              {{ tooltips.print_mode }}
            </NTooltip>
          </template>
          <NSelect
            :value="modelValue.print_mode"
            :options="printModeOptions"
            @update:value="(v: string) => update({ print_mode: v })"
          />
        </NFormItem>
        -->

        <ColorDBSelector
          :material="selectedMaterial"
          :vendor="selectedVendor"
          :material-options="materialOptions"
          :vendor-options="vendorOptionsForMaterial"
          :db-names="modelValue.db_names"
          :db-options="filteredDBOptions"
          :db-tooltip="tooltips.db_names"
          :is-lumina-preset="isLuminaPreset"
          @update:material="
            (v: string) => {
              selectedMaterial = v
            }
          "
          @update:vendor="
            (v: string) => {
              selectedVendor = v
            }
          "
          @update:db-names="(v: string[]) => update({ db_names: v })"
        />

        <!-- Cluster method (raster only) -->
        <NFormItem v-if="isRaster" label-placement="left" :label-width="simpleLabelWidth">
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">聚类方法</span>
              </template>
              {{ tooltips.cluster_method }}
            </NTooltip>
          </template>
          <NSelect
            :value="clusterMethodValue"
            :options="clusterMethodOptions"
            @update:value="setClusterMethod"
          />
        </NFormItem>

        <!-- KMeans cluster count (raster only) -->
        <NFormItem
          v-if="isRaster && !useSlicCluster"
          label-placement="left"
          :label-width="simpleLabelWidth"
        >
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">聚类数</span>
              </template>
              {{ tooltips.cluster_count }}
            </NTooltip>
          </template>
          <div class="slider-input-row">
            <NSlider
              v-model:value="clusterCountValue"
              :min="0"
              :max="clusterCountUpperBound"
              :step="1"
              class="slider-input-row__slider"
            />
            <NInputNumber
              :value="clusterCountValue"
              :min="0"
              :max="clusterCountUpperBound"
              :step="1"
              :show-button="false"
              class="slider-input-row__input number-input-right"
              @update:value="setClusterCount"
            />
          </div>
        </NFormItem>

        <!-- SLIC target superpixels (raster only) -->
        <NFormItem
          v-if="isRaster && useSlicCluster"
          label-placement="left"
          :label-width="simpleLabelWidth"
        >
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">超像素数量</span>
              </template>
              {{ tooltips.slic_target_superpixels }}
            </NTooltip>
          </template>
          <div class="slider-input-row">
            <NSlider
              v-model:value="slicTargetSuperpixelsValue"
              :min="0"
              :max="slicTargetSuperpixelsUpperBound"
              :step="1"
              class="slider-input-row__slider"
            />
            <NInputNumber
              :value="slicTargetSuperpixelsValue"
              :min="0"
              :max="slicTargetSuperpixelsUpperBound"
              :step="1"
              :show-button="false"
              class="slider-input-row__input number-input-right"
              @update:value="setSlicTargetSuperpixels"
            />
          </div>
        </NFormItem>

        <!-- SLIC compactness (raster only) -->
        <NFormItem
          v-if="isRaster && useSlicCluster"
          label-placement="left"
          :label-width="simpleLabelWidth"
        >
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">紧凑度</span>
              </template>
              {{ tooltips.slic_compactness }}
            </NTooltip>
          </template>
          <div class="slider-input-row">
            <NSlider
              v-model:value="slicCompactnessValue"
              :min="0.1"
              :max="slicCompactnessUpperBound"
              :step="0.1"
              :format-tooltip="formatTooltip1Decimal"
              class="slider-input-row__slider"
            />
            <NInputNumber
              :value="slicCompactnessValue"
              :min="0.1"
              :max="slicCompactnessUpperBound"
              :step="0.1"
              :precision="1"
              :show-button="false"
              class="slider-input-row__input number-input-right"
              @update:value="setSlicCompactness"
            />
          </div>
        </NFormItem>

        <div v-if="isRaster" class="param-inline-row">
          <!-- Dither (raster only) -->
          <NFormItem
            class="param-inline-item"
            label-placement="left"
            :label-width="inlineLabelWidth"
          >
            <template #label>
              <NTooltip>
                <template #trigger>
                  <span class="tip-label">半色调抖动</span>
                </template>
                {{ tooltips.dither }}
              </NTooltip>
            </template>
            <NSelect
              :value="modelValue.dither ?? 'none'"
              :options="ditherOptions"
              :disabled="useSlicCluster"
              @update:value="(v: string) => update({ dither: v })"
            />
          </NFormItem>

          <!-- Model enable -->
          <NFormItem
            class="param-inline-item"
            label-placement="left"
            :label-width="inlineLabelWidth"
          >
            <template #label>
              <NTooltip>
                <template #trigger>
                  <span class="tip-label">启用模型</span>
                </template>
                {{
                  modelPackAvailable ? tooltips.model_enable : '仅 BambooLab PLA 数据库支持模型增强'
                }}
              </NTooltip>
            </template>
            <NSwitch
              :value="modelValue.model_enable"
              :disabled="!modelPackAvailable"
              @update:value="(v: boolean) => update({ model_enable: v })"
            />
          </NFormItem>
        </div>

        <NFormItem v-else-if="supportsModelGate" label-placement="left" :label-width="simpleLabelWidth">
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">启用模型</span>
              </template>
              {{
                modelPackAvailable ? tooltips.model_enable : '仅 BambooLab PLA 数据库支持模型增强'
              }}
            </NTooltip>
          </template>
          <NSwitch
            :value="modelValue.model_enable"
            :disabled="!modelPackAvailable"
            @update:value="(v: boolean) => update({ model_enable: v })"
          />
        </NFormItem>

        <NFormItem
          v-if="isRaster && modelValue.dither && modelValue.dither !== 'none'"
          label-placement="left"
          :label-width="simpleLabelWidth"
        >
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">抖动强度</span>
              </template>
              {{ tooltips.dither_strength }}
            </NTooltip>
          </template>
          <NSlider
            :value="modelValue.dither_strength ?? 0.8"
            :min="0"
            :max="1"
            :step="0.05"
            :tooltip="true"
            :format-tooltip="formatTooltip2Decimals"
            @update:value="(v: number) => update({ dither_strength: roundTo(v, 2) })"
          />
        </NFormItem>

        <!-- Gradient dither (vector only) -->
        <NFormItem v-if="isVector" label-placement="left" :label-width="simpleLabelWidth">
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">渐变抖动</span>
              </template>
              {{ tooltips.gradient_dither }}
            </NTooltip>
          </template>
          <NSelect
            :value="modelValue.gradient_dither ?? 'none'"
            :options="gradientDitherOptions"
            @update:value="(v: string) => update({ gradient_dither: v })"
          />
        </NFormItem>

        <NFormItem
          v-if="isVector && modelValue.gradient_dither && modelValue.gradient_dither !== 'none'"
          label-placement="left"
          :label-width="simpleLabelWidth"
        >
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">渐变抖动强度</span>
              </template>
              {{ tooltips.gradient_dither_strength }}
            </NTooltip>
          </template>
          <NSlider
            :value="modelValue.gradient_dither_strength ?? 0.8"
            :min="0"
            :max="1"
            :step="0.05"
            :tooltip="true"
            :format-tooltip="formatTooltip2Decimals"
            @update:value="(v: number) => update({ gradient_dither_strength: roundTo(v, 2) })"
          />
        </NFormItem>

        <ChannelSelector
          collapse-name="channels"
          :tooltip-text="tooltips.allowed_channels"
          :selected-keys="selectedChannelKeys"
          :available-channels="availableChannels"
          :applicable-presets="applicablePresets"
          :active-preset="activeChannelPreset"
          :is-all-selected="isAllChannelsSelected"
          @update:selected-keys="handleChannelKeysChange"
          @apply:preset="applyChannelPreset"
        />

        <!-- Output info (raster only) -->
        <NAlert
          v-if="isRaster && simpleOutputInfo"
          type="info"
          :bordered="false"
          style="margin-top: 4px"
        >
          <div style="font-size: 12px; line-height: 1.6">
            <div>
              输出约
              <strong>{{ simpleOutputInfo.actualPxW }}×{{ simpleOutputInfo.actualPxH }}</strong>
              像素
              <span v-if="imageDimensions"
                >(原图 {{ imageDimensions.width }}×{{ imageDimensions.height }})</span
              >
            </div>
            <div>
              实际尺寸
              <strong
                >{{ simpleOutputInfo.actualWidthMm }}×{{ simpleOutputInfo.actualHeightMm }}</strong
              >
              mm
            </div>
          </div>
        </NAlert>
        <NAlert
          v-if="isRaster && simpleOutputInfo?.upscaleRatio"
          type="warning"
          :bordered="false"
          style="margin-top: 4px"
        >
          <div style="font-size: 12px">
            图像分辨率较低，将放大 {{ simpleOutputInfo.upscaleRatio.toFixed(1) }}x，可能导致输出模糊
          </div>
        </NAlert>
      </ParamSimpleSection>

      <!-- ==================== ADVANCED MODE ==================== -->
      <ParamAdvancedSection v-else :disabled="disabled || loading">
        <!-- Image processing group (raster has more items) -->
        <NCollapse default-expanded-names="imgproc" style="margin-bottom: 8px">
          <NCollapseItem :title="isRaster ? '图像处理' : '尺寸设置'" name="imgproc">
            <NFormItem v-if="isRaster">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">缩放倍率</span>
                  </template>
                  {{ tooltips.scale }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="modelValue.scale"
                :min="0.01"
                :max="10"
                :step="0.1"
                :precision="1"
                @update:value="
                  (v: number | null) =>
                    update({ scale: v === null ? undefined : roundTo(v, 1) })
                "
              />
            </NFormItem>

            <NFormItem v-if="isRaster">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">最大宽度 (px)</span>
                  </template>
                  {{ tooltips.max_width }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="modelValue.max_width"
                :min="0"
                :max="4096"
                @update:value="(v: number | null) => update({ max_width: v ?? undefined })"
              />
            </NFormItem>

            <NFormItem v-if="isRaster">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">最大高度 (px)</span>
                  </template>
                  {{ tooltips.max_height }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="modelValue.max_height"
                :min="0"
                :max="4096"
                @update:value="(v: number | null) => update({ max_height: v ?? undefined })"
              />
            </NFormItem>

            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">目标宽度 (mm)</span>
                  </template>
                  {{ tooltips.target_width_mm }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="modelValue.target_width_mm"
                :min="0"
                :max="targetDimensionUpperBound"
                :step="1"
                @update:value="(v: number | null) => update({ target_width_mm: v ?? 0 })"
              />
            </NFormItem>

            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">目标高度 (mm)</span>
                  </template>
                  {{ tooltips.target_height_mm }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="modelValue.target_height_mm"
                :min="0"
                :max="targetDimensionUpperBound"
                :step="1"
                @update:value="(v: number | null) => update({ target_height_mm: v ?? 0 })"
              />
            </NFormItem>
          </NCollapseItem>
        </NCollapse>

        <!-- Vector parameters group (vector only) -->
        <NCollapse
          v-if="isVector"
          default-expanded-names="vector-params"
          style="margin-bottom: 8px"
        >
          <NCollapseItem title="矢量图参数" name="vector-params">
            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">三角化容差 (mm)</span>
                  </template>
                  {{ tooltips.tessellation_tolerance_mm }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="modelValue.tessellation_tolerance_mm ?? 0.1"
                :min="0.01"
                :max="1"
                :step="0.01"
                :precision="2"
                @update:value="
                  (v: number | null) => update({ tessellation_tolerance_mm: roundTo(v ?? 0.1, 2) })
                "
              />
            </NFormItem>

            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">渐变抖动</span>
                  </template>
                  {{ tooltips.gradient_dither }}
                </NTooltip>
              </template>
              <NSelect
                :value="modelValue.gradient_dither ?? 'none'"
                :options="gradientDitherOptions"
                @update:value="(v: string) => update({ gradient_dither: v })"
              />
            </NFormItem>

            <NFormItem v-if="modelValue.gradient_dither && modelValue.gradient_dither !== 'none'">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">渐变抖动强度</span>
                  </template>
                  {{ tooltips.gradient_dither_strength }}
                </NTooltip>
              </template>
              <NSlider
                :value="modelValue.gradient_dither_strength ?? 0.8"
                :min="0"
                :max="1"
                :step="0.05"
                :tooltip="true"
                :format-tooltip="formatTooltip2Decimals"
                @update:value="(v: number) => update({ gradient_dither_strength: roundTo(v, 2) })"
              />
            </NFormItem>
          </NCollapseItem>
        </NCollapse>

        <!-- Geometry group -->
        <NCollapse default-expanded-names="geometry" style="margin-bottom: 8px">
          <NCollapseItem title="几何参数" name="geometry">
            <NFormItem v-if="isRaster">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">像素尺寸 (mm)</span>
                  </template>
                  {{ tooltips.pixel_mm }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="modelValue.pixel_mm"
                :min="0"
                :max="10"
                :step="0.01"
                :precision="2"
                @update:value="
                  (v: number | null) =>
                    update({ pixel_mm: v === null ? undefined : roundTo(v, 2) })
                "
              />
            </NFormItem>

            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">层高 (mm)</span>
                  </template>
                  {{ tooltips.layer_height_mm }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="modelValue.layer_height_mm"
                :min="0"
                :max="1"
                :step="0.01"
                :precision="2"
                @update:value="
                  (v: number | null) =>
                    update({ layer_height_mm: v === null ? undefined : roundTo(v, 2) })
                "
              />
            </NFormItem>

            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">垂直翻转</span>
                  </template>
                  {{ tooltips.flip_y }}
                </NTooltip>
              </template>
              <NSwitch
                :value="modelValue.flip_y"
                @update:value="(v: boolean) => update({ flip_y: v })"
              />
            </NFormItem>
          </NCollapseItem>
        </NCollapse>

        <!-- Color matching group -->
        <NCollapse default-expanded-names="matching" style="margin-bottom: 8px">
          <NCollapseItem title="颜色匹配" name="matching">
            <!-- 打印模式暂不使用，先注释掉选择控件 -->
            <!--
            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">打印模式</span>
                  </template>
                  {{ tooltips.print_mode }}
                </NTooltip>
              </template>
              <NSelect
                :value="modelValue.print_mode"
                :options="printModeOptions"
                @update:value="(v: string) => update({ print_mode: v })"
              />
            </NFormItem>
            -->

            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">色彩空间</span>
                  </template>
                  {{ tooltips.color_space }}
                </NTooltip>
              </template>
              <NSelect
                :value="modelValue.color_space"
                :options="colorSpaceOptions"
                @update:value="(v: string) => update({ color_space: v })"
              />
            </NFormItem>

            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">K 候选数</span>
                  </template>
                  {{ tooltips.k_candidates }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="modelValue.k_candidates"
                :min="1"
                :max="64"
                @update:value="(v: number | null) => update({ k_candidates: v ?? undefined })"
              />
            </NFormItem>

            <NFormItem v-if="isRaster">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">聚类方法</span>
                  </template>
                  {{ tooltips.cluster_method }}
                </NTooltip>
              </template>
              <NSelect
                :value="clusterMethodValue"
                :options="clusterMethodOptions"
                @update:value="setClusterMethod"
              />
            </NFormItem>

            <NFormItem v-if="isRaster && !useSlicCluster">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">聚类数</span>
                  </template>
                  {{ tooltips.cluster_count }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="clusterCountValue"
                :min="0"
                :max="clusterCountUpperBound"
                @update:value="setClusterCount"
              />
            </NFormItem>

            <NFormItem v-if="isRaster && useSlicCluster">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">超像素数量</span>
                  </template>
                  {{ tooltips.slic_target_superpixels }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="slicTargetSuperpixelsValue"
                :min="0"
                :max="slicTargetSuperpixelsUpperBound"
                :step="1"
                @update:value="setSlicTargetSuperpixels"
              />
            </NFormItem>

            <NFormItem v-if="isRaster && useSlicCluster">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">紧凑度</span>
                  </template>
                  {{ tooltips.slic_compactness }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="slicCompactnessValue"
                :min="0.1"
                :max="slicCompactnessUpperBound"
                :step="0.1"
                :precision="1"
                @update:value="setSlicCompactness"
              />
            </NFormItem>

            <NFormItem v-if="isRaster && useSlicCluster">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">迭代次数</span>
                  </template>
                  {{ tooltips.slic_iterations }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="slicIterationsValue"
                :min="1"
                :max="slicIterationsUpperBound"
                :step="1"
                @update:value="setSlicIterations"
              />
            </NFormItem>

            <NFormItem v-if="isRaster && useSlicCluster">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">小区域并入比例</span>
                  </template>
                  {{ tooltips.slic_min_region_ratio }}
                </NTooltip>
              </template>
              <NSlider
                :value="slicMinRegionRatioValue"
                :min="0"
                :max="1"
                :step="0.01"
                :tooltip="true"
                :format-tooltip="formatTooltip2Decimals"
                @update:value="setSlicMinRegionRatio"
              />
            </NFormItem>

            <NFormItem v-if="isRaster">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">半色调抖动</span>
                  </template>
                  {{ tooltips.dither }}
                </NTooltip>
              </template>
              <NSelect
                :value="modelValue.dither ?? 'none'"
                :options="ditherOptions"
                :disabled="useSlicCluster"
                @update:value="(v: string) => update({ dither: v })"
              />
            </NFormItem>

            <NFormItem v-if="isRaster && modelValue.dither && modelValue.dither !== 'none'">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">抖动强度</span>
                  </template>
                  {{ tooltips.dither_strength }}
                </NTooltip>
              </template>
              <NSlider
                :value="modelValue.dither_strength ?? 0.8"
                :min="0"
                :max="1"
                :step="0.05"
                :tooltip="true"
                :format-tooltip="formatTooltip2Decimals"
                @update:value="(v: number) => update({ dither_strength: roundTo(v, 2) })"
              />
            </NFormItem>

            <ColorDBSelector
              :material="selectedMaterial"
              :vendor="selectedVendor"
              :material-options="materialOptions"
              :vendor-options="vendorOptionsForMaterial"
              :db-names="modelValue.db_names"
              :db-options="filteredDBOptions"
              :db-tooltip="tooltips.db_names"
              :is-lumina-preset="isLuminaPreset"
              @update:material="
                (v: string) => {
                  selectedMaterial = v
                }
              "
              @update:vendor="
                (v: string) => {
                  selectedVendor = v
                }
              "
              @update:db-names="(v: string[]) => update({ db_names: v })"
            />

            <ChannelSelector
              collapse-name="channels-adv"
              :tooltip-text="tooltips.allowed_channels"
              :selected-keys="selectedChannelKeys"
              :available-channels="availableChannels"
              :applicable-presets="applicablePresets"
              :active-preset="activeChannelPreset"
              :is-all-selected="isAllChannelsSelected"
              @update:selected-keys="handleChannelKeysChange"
              @apply:preset="applyChannelPreset"
            />
          </NCollapseItem>
        </NCollapse>

        <!-- Model gate group -->
        <NCollapse v-if="supportsModelGate" style="margin-bottom: 8px">
          <NCollapseItem title="模型门控" name="model-gate">
            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">启用模型</span>
                  </template>
                  {{
                    modelPackAvailable
                      ? tooltips.model_enable
                      : '仅 BambooLab PLA 数据库支持模型增强'
                  }}
                </NTooltip>
              </template>
              <NSwitch
                :value="modelValue.model_enable"
                :disabled="!modelPackAvailable"
                @update:value="(v: boolean) => update({ model_enable: v })"
              />
            </NFormItem>

            <NFormItem v-if="modelValue.model_enable">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">仅使用模型</span>
                  </template>
                  {{ tooltips.model_only }}
                </NTooltip>
              </template>
              <NSwitch
                :value="modelValue.model_only"
                @update:value="(v: boolean) => update({ model_only: v })"
              />
            </NFormItem>

            <NFormItem v-if="modelValue.model_enable">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">模型阈值</span>
                  </template>
                  {{ tooltips.model_threshold }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="modelValue.model_threshold"
                :step="0.5"
                :precision="1"
                @update:value="
                  (v: number | null) =>
                    update({ model_threshold: v === null ? undefined : roundTo(v, 1) })
                "
              />
            </NFormItem>

            <NFormItem v-if="modelValue.model_enable">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">模型边距</span>
                  </template>
                  {{ tooltips.model_margin }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="modelValue.model_margin"
                :step="0.5"
                :precision="1"
                @update:value="
                  (v: number | null) =>
                    update({ model_margin: v === null ? undefined : roundTo(v, 1) })
                "
              />
            </NFormItem>
          </NCollapseItem>
        </NCollapse>

        <!-- Output options group -->
        <NCollapse>
          <NCollapseItem title="输出选项" name="output">
            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">生成预览图</span>
                  </template>
                  {{ tooltips.generate_preview }}
                </NTooltip>
              </template>
              <NSwitch
                :value="modelValue.generate_preview"
                @update:value="(v: boolean) => update({ generate_preview: v })"
              />
            </NFormItem>

            <NFormItem v-if="isRaster">
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">生成源掩码</span>
                  </template>
                  {{ tooltips.generate_source_mask }}
                </NTooltip>
              </template>
              <NSwitch
                :value="modelValue.generate_source_mask"
                @update:value="(v: boolean) => update({ generate_source_mask: v })"
              />
            </NFormItem>
          </NCollapseItem>
        </NCollapse>
      </ParamAdvancedSection>
    </NSpin>
  </NCard>
</template>

<style scoped>
.tip-label {
  cursor: help;
  border-bottom: 1px dashed var(--n-text-color-3);
}

.param-inline-row {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  column-gap: 8px;
}

.param-inline-item {
  min-width: 0;
}

.slider-input-row {
  display: flex;
  align-items: center;
  gap: 10px;
  width: 100%;
}

.slider-input-row__slider {
  flex: 1;
  min-width: 0;
}

.slider-input-row__input {
  width: 104px;
}

.pixel-size-row {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
}

.pixel-size-row__select {
  flex: 1;
  min-width: 0;
}

.pixel-size-row__input {
  width: 112px;
}

.number-input-right :deep(.n-input__input-el) {
  text-align: right;
}

@media (max-width: 920px) {
  .param-inline-row {
    grid-template-columns: 1fr;
  }

  .slider-input-row__input {
    width: 96px;
  }

  .pixel-size-row__input {
    width: 100px;
  }
}
</style>
