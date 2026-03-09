import { computed, onMounted, ref, watch } from 'vue'
import { storeToRefs } from 'pinia'
import type { SelectOption } from 'naive-ui'
import { createInitialConvertParams } from '../domain/params/convertDefaults'
import { formatFloat, roundTo } from '../runtime/number'
import { useAppStore } from '../stores/app'
import { PIXEL_SIZE_PRESETS, type ColorDBInfo, type ConvertAnyParams, type DefaultConfig, type PaletteChannel } from '../types'
import { fetchAvailableColorDBs, fetchParamPanelBootstrap } from '../services/paramService'

interface ChannelOption {
  key: string
  color: string
  material: string
}

interface ChannelPreset {
  label: string
  value: string
  colors: string[] | null
}

const CHANNEL_PRESETS: ChannelPreset[] = [
  { label: '全部', value: 'all', colors: null },
  { label: 'RYBW', value: 'rybw', colors: ['Red', 'Yellow', 'Blue', 'White'] },
  { label: 'CMYW', value: 'cmyw', colors: ['Cyan', 'Magenta', 'Yellow', 'White'] },
]

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
  base_layers:
    '底板层数。留空表示沿用数据库默认；设置为 0 表示不生成底板；大于 0 将显式覆盖默认底板层数',
  double_sided: '双面生成。开启后会在底板两侧生成镜像颜色层，形成双面可视结构',
  transparent_layer_mm:
    '透明镀层厚度。仅在观赏面朝下时可用，在颜色层最下方打印一层透明材料以获得透亮效果',
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
    '矢量曲线三角化时的容差（毫米），值越小曲线越平滑但网格越多。默认 0.03，推荐 0.03-0.12',
  gradient_dither: '对 SVG 渐变区域进行抖动处理，改善渐变在有限调色板下的过渡效果',
  gradient_dither_strength: '渐变区域的抖动强度。值越大渐变过渡越平滑，但可能产生颗粒感',
} as const

function channelKey(ch: PaletteChannel): string {
  return `${ch.color}|${ch.material}`
}

function buildDBOptions(dbs: ColorDBInfo[]): SelectOption[] {
  return dbs.map((db) => {
    const suffix = db.source === 'session' ? ' (自定义)' : ''
    return {
      label: `${db.name} (${db.num_entries} 色, ${db.num_channels} 通道)${suffix}`,
      value: db.name,
    }
  })
}

export function useParamPanelState() {
  const appStore = useAppStore()
  const { params: modelValue, inputType, imageDimensions, colordbVersion } = storeToRefs(appStore)

  const loading = ref(true)
  const error = ref<string | null>(null)
  const colorDBs = ref<ColorDBInfo[]>([])
  const defaults = ref<DefaultConfig | null>(null)
  const mode = ref<'simple' | 'advanced'>('simple')
  const selectedMaterial = ref('PLA')
  const selectedVendor = ref('BambooLab')
  const targetWidthMm = ref(200)
  const targetHeightMm = ref(200)
  const selectedPixelPresetIndex = ref(1)
  const customPixelMm = ref(0.42)
  const selectedChannelKeys = ref<string[]>([])

  const targetDimensionUpperBound = 2000
  const clusterCountUpperBound = 256
  const slicTargetSuperpixelsUpperBound = 4096
  const slicCompactnessUpperBound = 100
  const slicIterationsUpperBound = 50
  const simpleLabelWidth = 112
  const inlineLabelWidth = 96
  const formatTooltip1Decimal = (value: number) => formatFloat(value, 1)
  const formatTooltip2Decimals = (value: number) => formatFloat(value, 2)

  const isRaster = computed(() => inputType.value === 'raster')
  const isVector = computed(() => inputType.value === 'vector')
  const supportsModelGate = computed(() => isRaster.value || isVector.value)

  const effectivePixelMm = computed(() => {
    const preset = PIXEL_SIZE_PRESETS[selectedPixelPresetIndex.value]
    return preset && preset.value > 0 ? preset.value : customPixelMm.value
  })

  const isCustomPixel = computed(() => {
    const preset = PIXEL_SIZE_PRESETS[selectedPixelPresetIndex.value]
    return !preset || preset.value === 0
  })

  const pixelPresetOptions = computed<SelectOption[]>(() =>
    PIXEL_SIZE_PRESETS.map((preset, index) => ({
      label: preset.label,
      value: index,
    })),
  )

  function update(partial: Partial<ConvertAnyParams>) {
    appStore.setParams({ ...modelValue.value, ...partial })
  }

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
      const scale = Math.min(maxPxW / dims.width, maxPxH / dims.height)
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
      return
    }

    if (isRaster.value) {
      update({
        target_width_mm: 0,
        target_height_mm: 0,
      })
    }
  })

  watch(
    () => inputType.value,
    (newType) => {
      if (newType === 'vector') {
        update({
          tessellation_tolerance_mm: modelValue.value.tessellation_tolerance_mm ?? 0.03,
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

  const availableChannels = computed<ChannelOption[]>(() => {
    const selectedNames = modelValue.value.db_names ?? []
    const seen = new Set<string>()
    const channels: ChannelOption[] = []
    for (const db of colorDBs.value) {
      if (!selectedNames.includes(db.name)) continue
      for (const channel of db.palette) {
        const key = channelKey(channel)
        if (seen.has(key)) continue
        seen.add(key)
        channels.push({ key, color: channel.color, material: channel.material })
      }
    }
    return channels
  })

  const isAllChannelsSelected = computed(
    () =>
      availableChannels.value.length > 0 &&
      selectedChannelKeys.value.length === availableChannels.value.length,
  )

  function emitChannelUpdate() {
    const allKeys = new Set(availableChannels.value.map((channel) => channel.key))
    const selectedSet = new Set(selectedChannelKeys.value)
    if (
      selectedChannelKeys.value.length >= availableChannels.value.length &&
      [...allKeys].every((key) => selectedSet.has(key))
    ) {
      update({ allowed_channels: undefined })
      return
    }

    const channels = availableChannels.value
      .filter((channel) => selectedSet.has(channel.key))
      .map((channel) => ({ color: channel.color, material: channel.material }))
    update({ allowed_channels: channels })
  }

  function handleChannelKeysChange(keys: string[]) {
    selectedChannelKeys.value = keys
    emitChannelUpdate()
  }

  const applicablePresets = computed(() => {
    const availableColors = new Set(availableChannels.value.map((channel) => channel.color))
    return CHANNEL_PRESETS.filter((preset) => {
      if (!preset.colors) return true
      return preset.colors.every((color) => availableColors.has(color))
    })
  })

  const activeChannelPreset = computed<string | null>(() => {
    if (isAllChannelsSelected.value) return 'all'
    const selectedColors = new Set(
      availableChannels.value
        .filter((channel) => selectedChannelKeys.value.includes(channel.key))
        .map((channel) => channel.color),
    )
    for (const preset of CHANNEL_PRESETS) {
      if (!preset.colors) continue
      if (
        preset.colors.length === selectedColors.size &&
        preset.colors.every((color) => selectedColors.has(color))
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
      update({ cluster_count: Math.min(clusterCountUpperBound, Math.max(0, Math.round(next))) })
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
      update({
        slic_target_superpixels: Math.min(
          slicTargetSuperpixelsUpperBound,
          Math.max(0, Math.round(next)),
        ),
      })
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
      update({
        slic_compactness: Math.min(slicCompactnessUpperBound, Math.max(0.1, roundTo(Number(next), 1))),
      })
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
      update({ slic_iterations: Math.min(slicIterationsUpperBound, Math.max(1, Math.round(next))) })
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
      update({ slic_min_region_ratio: Math.min(1, Math.max(0, roundTo(Number(next), 2))) })
    },
  })

  function setSlicMinRegionRatio(value: number) {
    slicMinRegionRatioValue.value = roundTo(value, 2)
  }

  function applyChannelPreset(value: string) {
    const preset = CHANNEL_PRESETS.find((item) => item.value === value)
    if (!preset) return
    if (!preset.colors) {
      selectedChannelKeys.value = availableChannels.value.map((channel) => channel.key)
    } else {
      const target = new Set(preset.colors)
      selectedChannelKeys.value = availableChannels.value
        .filter((channel) => target.has(channel.color))
        .map((channel) => channel.key)
    }
    emitChannelUpdate()
  }

  watch(availableChannels, (newChannels, oldChannels) => {
    const oldKeys = new Set((oldChannels ?? []).map((channel) => channel.key))
    const newKeys = new Set(newChannels.map((channel) => channel.key))

    if (oldKeys.size === newKeys.size && [...oldKeys].every((key) => newKeys.has(key))) return

    let updated = selectedChannelKeys.value.filter((key) => newKeys.has(key))
    for (const channel of newChannels) {
      if (!oldKeys.has(channel.key) && !updated.includes(channel.key)) {
        updated.push(channel.key)
      }
    }

    if (updated.length === 0 && newChannels.length > 0) {
      updated = newChannels.map((channel) => channel.key)
    }

    selectedChannelKeys.value = updated
    emitChannelUpdate()
  })

  const materialOptions = computed<SelectOption[]>(() => {
    const materials = [
      ...new Set(
        colorDBs.value
          .filter((db) => db.source !== 'session' && db.material_type)
          .map((db) => db.material_type!),
      ),
    ]
    materials.sort()
    return materials.map((material) => ({ label: material, value: material }))
  })

  const vendorOptionsForMaterial = computed<SelectOption[]>(() => {
    const vendors = [
      ...new Set(
        colorDBs.value
          .filter(
            (db) =>
              db.source !== 'session' &&
              db.material_type === selectedMaterial.value &&
              db.vendor,
          )
          .map((db) => db.vendor!),
      ),
    ]
    vendors.sort()
    return vendors.map((vendor) => ({ label: vendor, value: vendor }))
  })

  const filteredDBs = computed(() =>
    colorDBs.value.filter((db) => {
      if (db.source === 'session') return true
      if (db.material_type !== selectedMaterial.value) return false
      return db.vendor === selectedVendor.value
    }),
  )

  const isLuminaPreset = computed(
    () => !(selectedMaterial.value === 'PLA' && selectedVendor.value === 'BambooLab'),
  )

  const filteredDBOptions = computed<SelectOption[]>(() => buildDBOptions(filteredDBs.value))
  const modelPackAvailable = computed(
    () => selectedMaterial.value === 'PLA' && selectedVendor.value === 'BambooLab',
  )

  function selectAllFilteredDBs() {
    update({ db_names: filteredDBs.value.map((db) => db.name) })
  }

  watch(selectedMaterial, () => {
    const available = vendorOptionsForMaterial.value.map((option) => option.value as string)
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

  function resetParams() {
    if (!defaults.value) return
    targetWidthMm.value = 200
    targetHeightMm.value = 200
    mode.value = 'simple'
    selectedPixelPresetIndex.value = 1
    customPixelMm.value = 0.42
    const currentDbNames = modelValue.value.db_names ?? filteredDBs.value.map((db) => db.name)
    appStore.setParams(
      createInitialConvertParams({
        defaults: defaults.value,
        targetWidthMm: targetWidthMm.value,
        targetHeightMm: targetHeightMm.value,
        pixelMm: effectivePixelMm.value,
        dbNames: currentDbNames,
      }),
    )
    selectedChannelKeys.value = availableChannels.value.map((channel) => channel.key)
  }

  async function loadColorDBs() {
    try {
      colorDBs.value = await fetchAvailableColorDBs()
    } catch {
      // Keep existing options on refresh failure
    }
  }

  watch(
    () => colordbVersion.value,
    () => {
      void loadColorDBs()
    },
  )

  onMounted(async () => {
    try {
      const bootstrap = await fetchParamPanelBootstrap()
      defaults.value = bootstrap.defaults
      colorDBs.value = bootstrap.colorDBs

      const globalDBs = bootstrap.colorDBs.filter((db) => db.source !== 'session')
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
          defaults: bootstrap.defaults,
          targetWidthMm: targetWidthMm.value,
          targetHeightMm: targetHeightMm.value,
          pixelMm: effectivePixelMm.value,
          dbNames: initialDbNames,
        }),
      )
    } catch (nextError: unknown) {
      error.value = nextError instanceof Error ? nextError.message : '加载配置失败'
    } finally {
      loading.value = false
    }
  })

  return {
    activeChannelPreset,
    applicablePresets,
    availableChannels,
    clusterCountUpperBound,
    clusterCountValue,
    clusterMethodOptions,
    clusterMethodValue,
    colorSpaceOptions,
    customPixelMm,
    ditherOptions,
    error,
    filteredDBOptions,
    formatTooltip1Decimal,
    formatTooltip2Decimals,
    gradientDitherOptions,
    handleChannelKeysChange,
    imageDimensions,
    inlineLabelWidth,
    inputType,
    isAllChannelsSelected,
    isCustomPixel,
    isLuminaPreset,
    isRaster,
    isVector,
    loading,
    materialOptions,
    mode,
    modelPackAvailable,
    modelValue,
    pixelPresetOptions,
    resetParams,
    selectedChannelKeys,
    selectedMaterial,
    selectedPixelPresetIndex,
    selectedVendor,
    setClusterCount,
    setClusterMethod,
    setSlicCompactness,
    setSlicIterations,
    setSlicMinRegionRatio,
    setSlicTargetSuperpixels,
    simpleLabelWidth,
    simpleOutputInfo,
    slicCompactnessUpperBound,
    slicCompactnessValue,
    slicIterationsUpperBound,
    slicIterationsValue,
    slicMinRegionRatioValue,
    slicTargetSuperpixelsUpperBound,
    slicTargetSuperpixelsValue,
    supportsModelGate,
    targetDimensionUpperBound,
    targetHeightMm,
    targetWidthMm,
    tooltips,
    roundTo,
    update,
    useSlicCluster,
    vendorOptionsForMaterial,
    applyChannelPreset,
  }
}
