<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import {
  NCard,
  NForm,
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
  NCheckbox,
  NCheckboxGroup,
  NSpace,
  NRadioGroup,
  NRadioButton,
} from 'naive-ui'
import type { SelectOption } from 'naive-ui'
import { fetchDefaults, fetchColorDBs } from '../api'
import type { ConvertParams, ColorDBInfo, DefaultConfig, PaletteChannel } from '../types'
import { BED_PRESETS, PIXEL_SIZE_PRESETS } from '../types'
import type { ImageDimensions } from './ImageUpload.vue'

const props = defineProps<{
  modelValue: ConvertParams
  disabled?: boolean
  refreshTrigger?: number
  imageDimensions?: ImageDimensions | null
}>()

const emit = defineEmits<{
  'update:modelValue': [params: ConvertParams]
}>()

const loading = ref(true)
const error = ref<string | null>(null)
const colorDBs = ref<ColorDBInfo[]>([])
const defaults = ref<DefaultConfig | null>(null)

const mode = ref<'simple' | 'advanced'>('simple')

// --- Material / Vendor filter ---
const selectedMaterial = ref('PLA')
const selectedVendor = ref('BambooLab')

// --- Simple mode local state ---
const selectedBedIndex = ref(1) // default: Bambu Lab A1/P1/X1
const targetWidthMm = ref(200)
const targetHeightMm = ref(200)
const selectedPixelPresetIndex = ref(1) // default: 0.42mm
const customPixelMm = ref(0.42)

const bedMaxWidth = computed(() => BED_PRESETS[selectedBedIndex.value]?.width ?? 0)
const bedMaxHeight = computed(() => BED_PRESETS[selectedBedIndex.value]?.height ?? 0)
const isCustomBed = computed(() => bedMaxWidth.value === 0 && bedMaxHeight.value === 0)

const effectivePixelMm = computed(() => {
  const preset = PIXEL_SIZE_PRESETS[selectedPixelPresetIndex.value]
  return preset && preset.value > 0 ? preset.value : customPixelMm.value
})
const isCustomPixel = computed(() => {
  const preset = PIXEL_SIZE_PRESETS[selectedPixelPresetIndex.value]
  return !preset || preset.value === 0
})

const bedOptions = computed<SelectOption[]>(() =>
  BED_PRESETS.map((p, i) => ({
    label: p.width > 0 ? `${p.label} (${p.width}×${p.height} mm)` : p.label,
    value: i,
  })),
)

const pixelPresetOptions = computed<SelectOption[]>(() =>
  PIXEL_SIZE_PRESETS.map((p, i) => ({
    label: p.label,
    value: i,
  })),
)

// Clamp target dimensions when bed preset changes
watch(selectedBedIndex, () => {
  if (!isCustomBed.value) {
    if (targetWidthMm.value > bedMaxWidth.value) targetWidthMm.value = bedMaxWidth.value
    if (targetHeightMm.value > bedMaxHeight.value) targetHeightMm.value = bedMaxHeight.value
  }
})

// Sync simple mode state to ConvertParams
watch(
  [targetWidthMm, targetHeightMm, effectivePixelMm],
  () => {
    if (mode.value === 'simple') {
      update({
        target_width_mm: targetWidthMm.value,
        target_height_mm: targetHeightMm.value,
        pixel_mm: effectivePixelMm.value,
        max_width: 0,
        max_height: 0,
        scale: 1.0,
      })
    }
  },
)

// Output info for simple mode
const simpleOutputInfo = computed(() => {
  const px = effectivePixelMm.value
  if (px <= 0) return null
  const maxPxW = Math.floor(targetWidthMm.value / px)
  const maxPxH = Math.floor(targetHeightMm.value / px)

  const dims = props.imageDimensions
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

const printModeOptions: SelectOption[] = [
  { label: '0.08mm × 5 层', value: '0.08x5' },
  { label: '0.04mm × 10 层', value: '0.04x10' },
]

const colorSpaceOptions: SelectOption[] = [
  { label: 'Lab', value: 'lab' },
  { label: 'RGB', value: 'rgb' },
]

const ditherOptions: SelectOption[] = [
  { label: '关闭', value: 'none' },
  { label: '蓝噪声（推荐）', value: 'blue_noise' },
  { label: 'Floyd-Steinberg', value: 'floyd_steinberg' },
]

const tooltips: Record<string, string> = {
  print_mode:
    '决定色层数和层高。0.08mm x 5 层: 层高 0.08mm，5 层叠色；0.04mm x 10 层: 层高 0.04mm，10 层叠色。两者总叠色厚度相同 (0.4mm)',
  color_space:
    '颜色匹配时使用的色彩空间。Lab 更符合人眼感知（推荐）；RGB 为直接 RGB 距离计算',
  max_width:
    '图像缩放后的最大宽度（像素），超出时等比缩小。值越小处理越快，输出模型越小',
  max_height:
    '图像缩放后的最大高度（像素），超出时等比缩小。值越小处理越快，输出模型越小',
  target_width_mm:
    '输出 3MF 模型的目标物理宽度（毫米），图像将被缩放以适应此尺寸',
  target_height_mm:
    '输出 3MF 模型的目标物理高度（毫米），图像将被缩放以适应此尺寸',
  bed_size:
    '选择打印机热床尺寸，用于限制目标宽度和高度的上限',
  pixel_mm_simple:
    '每个像素对应的物理线宽（毫米），由打印机喷嘴尺寸决定。0.4mm 喷嘴推荐 0.42mm，0.2mm 喷嘴推荐 0.22mm',
  cluster_count:
    '对图像像素进行 K-Means 聚类后再匹配颜色。值越大颜色越精细，0 或 1 表示不聚类（逐像素匹配）',
  dither:
    '半色调抖动通过在相邻像素间交替使用不同配方来模拟更丰富的颜色过渡，能有效消除渐变区域的色阶断裂。蓝噪声方法可并行处理，速度快；Floyd-Steinberg 质量略高但速度较慢',
  dither_strength:
    '抖动强度，控制颜色偏移幅度。值越大抖动效果越明显，但过高可能产生颗粒感。推荐 0.6-0.9',
  db_names:
    '用于颜色匹配的 ColorDB，支持多选。匹配时会在所有选中的数据库中寻找最佳配方',
  allowed_channels:
    '选择参与配方生成的颜色通道。未选中的通道将被排除，默认使用全部通道',
  scale:
    '在最大宽高限制之前先对图像进行等比缩放，1.0 表示不缩放',
  k_candidates:
    '颜色匹配时从 KD-Tree 中取 K 个最近邻候选，再从中选择最优。值越大匹配越精确但越慢',
  flip_y:
    '构建 3D 模型时翻转 Y 轴方向，开启时图像顶部对应模型顶部',
  pixel_mm:
    '每个像素对应的实际尺寸（毫米），决定输出模型的物理大小。0 表示自动从 ColorDB 配置推导',
  layer_height_mm:
    '3D 打印的层高（毫米），决定模型 Z 方向分辨率。0 表示自动从打印模式推导',
  model_enable:
    '是否启用神经网络模型辅助匹配。开启后，当 ColorDB 匹配质量不佳时会尝试用模型预测更优配方',
  model_only:
    '跳过 ColorDB 匹配，完全使用模型预测配方。需要加载模型包',
  model_threshold:
    '色差 (DeltaE) 阈值，仅当 ColorDB 匹配色差超过此值时才启用模型。负值使用模型包默认值',
  model_margin:
    '模型结果需优于 ColorDB 结果至少此色差值才会被采用。负值使用模型包默认值',
  generate_preview:
    '生成匹配后的颜色预览图（PNG），展示每个像素最终匹配到的颜色',
  generate_source_mask:
    '生成来源掩码图（PNG），白色像素表示使用了模型预测的配方，黑色表示使用了 ColorDB 匹配',
}

function update(partial: Partial<ConvertParams>) {
  emit('update:modelValue', { ...props.modelValue, ...partial })
}

// When switching to simple mode, push target_mm fields;
// when switching to advanced, clear target_mm so backend uses max_width/max_height
watch(mode, (newMode) => {
  if (newMode === 'simple') {
    update({
      target_width_mm: targetWidthMm.value,
      target_height_mm: targetHeightMm.value,
      pixel_mm: effectivePixelMm.value,
      max_width: 0,
      max_height: 0,
      scale: 1.0,
    })
  } else {
    update({
      target_width_mm: 0,
      target_height_mm: 0,
    })
  }
})

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
  const selectedNames = props.modelValue.db_names ?? []
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

function handleChannelKeysChange(keys: (string | number)[]) {
  selectedChannelKeys.value = keys.map(String)
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

watch(
  availableChannels,
  (newChannels, oldChannels) => {
    const oldKeys = new Set((oldChannels ?? []).map((c) => c.key))
    const newKeys = new Set(newChannels.map((c) => c.key))

    if (
      oldKeys.size === newKeys.size &&
      [...oldKeys].every((k) => newKeys.has(k))
    ) {
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
  },
)

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
        .filter((db) => db.source !== 'session' && db.material_type === selectedMaterial.value && db.vendor)
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
  update({ model_enable: available })
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
  () => props.refreshTrigger,
  () => { loadColorDBs() },
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
        globalDBs.filter((db) => db.material_type === selectedMaterial.value).map((db) => db.vendor).filter(Boolean),
      ),
    ]
    if (vendors.includes('BambooLab')) {
      selectedVendor.value = 'BambooLab'
    } else if (vendors.length > 0) {
      selectedVendor.value = vendors[0]!
    }

    const initialDbNames = filteredDBs.value.map((db) => db.name)

    emit('update:modelValue', {
      print_mode: defaultsData.print_mode,
      color_space: defaultsData.color_space,
      max_width: 0,
      max_height: 0,
      target_width_mm: targetWidthMm.value,
      target_height_mm: targetHeightMm.value,
      scale: defaultsData.scale,
      k_candidates: defaultsData.k_candidates,
      cluster_count: defaultsData.cluster_count,
      dither: defaultsData.dither,
      dither_strength: defaultsData.dither_strength,
      model_enable: defaultsData.model_enable,
      model_only: defaultsData.model_only,
      model_threshold: defaultsData.model_threshold,
      model_margin: defaultsData.model_margin,
      flip_y: defaultsData.flip_y,
      pixel_mm: effectivePixelMm.value,
      layer_height_mm: defaultsData.layer_height_mm,
      generate_preview: defaultsData.generate_preview,
      generate_source_mask: defaultsData.generate_source_mask,
      db_names: initialDbNames,
    })
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
      <NRadioGroup v-model:value="mode" size="small">
        <NRadioButton value="simple">简易</NRadioButton>
        <NRadioButton value="advanced">高级</NRadioButton>
      </NRadioGroup>
    </template>

    <NSpin :show="loading">
      <NAlert v-if="error" type="error" :title="error" style="margin-bottom: 12px" />

      <!-- ==================== SIMPLE MODE ==================== -->
      <NForm
        v-if="mode === 'simple'"
        label-placement="left"
        label-width="auto"
        :disabled="disabled || loading"
        size="small"
      >
        <!-- Bed size preset -->
        <NFormItem>
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">打印板尺寸</span>
              </template>
              {{ tooltips.bed_size }}
            </NTooltip>
          </template>
          <NSelect
            :value="selectedBedIndex"
            :options="bedOptions"
            @update:value="(v: number) => { selectedBedIndex = v }"
          />
        </NFormItem>

        <!-- Target width mm -->
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
            v-model:value="targetWidthMm"
            :min="1"
            :max="isCustomBed ? 1000 : bedMaxWidth"
            :step="1"
          />
        </NFormItem>

        <!-- Target height mm -->
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
            v-model:value="targetHeightMm"
            :min="1"
            :max="isCustomBed ? 1000 : bedMaxHeight"
            :step="1"
          />
        </NFormItem>

        <!-- Pixel size preset -->
        <NFormItem>
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">像素尺寸 (mm)</span>
              </template>
              {{ tooltips.pixel_mm_simple }}
            </NTooltip>
          </template>
          <NSpace vertical :size="4" style="width: 100%">
            <NSelect
              :value="selectedPixelPresetIndex"
              :options="pixelPresetOptions"
              @update:value="(v: number) => { selectedPixelPresetIndex = v }"
            />
            <NInputNumber
              v-if="isCustomPixel"
              v-model:value="customPixelMm"
              :min="0.05"
              :max="5"
              :step="0.01"
              placeholder="自定义像素尺寸"
            />
          </NSpace>
        </NFormItem>

        <!-- Print mode -->
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

        <!-- Material filter -->
        <NFormItem label="材质类型">
          <NSelect
            :value="selectedMaterial"
            :options="materialOptions"
            @update:value="(v: string) => { selectedMaterial = v }"
          />
        </NFormItem>

        <!-- Vendor filter -->
        <NFormItem label="厂商">
          <NSelect
            :value="selectedVendor"
            :options="vendorOptionsForMaterial"
            @update:value="(v: string) => { selectedVendor = v }"
          />
        </NFormItem>

        <!-- ColorDB -->
        <NFormItem>
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">颜色数据库</span>
              </template>
              {{ tooltips.db_names }}
            </NTooltip>
          </template>
          <NSelect
            :value="modelValue.db_names"
            :options="filteredDBOptions"
            multiple
            placeholder="选择颜色数据库"
            @update:value="(v: string[]) => update({ db_names: v })"
          />
        </NFormItem>

        <NAlert v-if="isLuminaPreset" type="info" :bordered="false" style="margin-bottom: 12px; font-size: 12px">
          当前预设来自
          <a href="https://github.com/MOVIBALE/Lumina-Layers" target="_blank" rel="noopener">Lumina-Layers</a>
          开源项目
        </NAlert>

        <!-- Model enable -->
        <NFormItem>
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">启用模型</span>
              </template>
              {{ modelPackAvailable ? tooltips.model_enable : '仅 BambooLab PLA 数据库支持模型增强' }}
            </NTooltip>
          </template>
          <NSwitch
            :value="modelValue.model_enable"
            :disabled="!modelPackAvailable"
            @update:value="(v: boolean) => update({ model_enable: v })"
          />
        </NFormItem>

        <!-- Cluster count -->
        <NFormItem>
          <template #label>
            <NTooltip>
              <template #trigger>
                <span class="tip-label">聚类数</span>
              </template>
              {{ tooltips.cluster_count }}
            </NTooltip>
          </template>
          <NInputNumber
            :value="modelValue.cluster_count"
            :min="0"
            :max="65536"
            @update:value="(v: number | null) => update({ cluster_count: v ?? undefined })"
          />
        </NFormItem>

        <NFormItem>
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
            @update:value="(v: string) => update({ dither: v })"
          />
        </NFormItem>

        <NFormItem v-if="modelValue.dither && modelValue.dither !== 'none'">
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
            @update:value="(v: number) => update({ dither_strength: v })"
          />
        </NFormItem>

        <!-- Channel filtering -->
        <NCollapse v-if="availableChannels.length > 0" style="margin-bottom: 12px">
          <NCollapseItem name="channels">
            <template #header>
              <NTooltip>
                <template #trigger>
                  <span class="tip-label">颜色通道筛选</span>
                </template>
                {{ tooltips.allowed_channels }}
              </NTooltip>
            </template>
            <template #header-extra>
              <span style="font-size: 12px; color: var(--n-text-color-3)">
                {{ isAllChannelsSelected ? '全部' : `${selectedChannelKeys.length}/${availableChannels.length}` }}
              </span>
            </template>
            <div style="width: 100%">
              <NRadioGroup
                v-if="applicablePresets.length > 1"
                :value="activeChannelPreset"
                size="small"
                style="margin-bottom: 8px"
                @update:value="(v: string) => applyChannelPreset(v)"
              >
                <NRadioButton v-for="p in applicablePresets" :key="p.value" :value="p.value">
                  {{ p.label }}
                </NRadioButton>
              </NRadioGroup>
              <NCheckboxGroup
                :value="selectedChannelKeys"
                @update:value="handleChannelKeysChange"
              >
                <NSpace item-style="display: flex">
                  <NCheckbox
                    v-for="ch in availableChannels"
                    :key="ch.key"
                    :value="ch.key"
                    :label="`${ch.color} (${ch.material})`"
                  />
                </NSpace>
              </NCheckboxGroup>
            </div>
          </NCollapseItem>
        </NCollapse>

        <!-- Output info -->
        <NAlert v-if="simpleOutputInfo" type="info" :bordered="false" style="margin-top: 4px">
          <div style="font-size: 12px; line-height: 1.6">
            <div>
              输出约 <strong>{{ simpleOutputInfo.actualPxW }}×{{ simpleOutputInfo.actualPxH }}</strong> 像素
              <span v-if="imageDimensions">(原图 {{ imageDimensions.width }}×{{ imageDimensions.height }})</span>
            </div>
            <div>
              实际尺寸 <strong>{{ simpleOutputInfo.actualWidthMm }}×{{ simpleOutputInfo.actualHeightMm }}</strong> mm
            </div>
          </div>
        </NAlert>
        <NAlert
          v-if="simpleOutputInfo?.upscaleRatio"
          type="warning"
          :bordered="false"
          style="margin-top: 4px"
        >
          <div style="font-size: 12px">
            图像分辨率较低，将放大 {{ simpleOutputInfo.upscaleRatio.toFixed(1) }}x，可能导致输出模糊
          </div>
        </NAlert>
      </NForm>

      <!-- ==================== ADVANCED MODE ==================== -->
      <NForm
        v-else
        label-placement="left"
        label-width="auto"
        :disabled="disabled || loading"
        size="small"
      >
        <!-- Image processing group -->
        <NCollapse default-expanded-names="imgproc" style="margin-bottom: 8px">
          <NCollapseItem title="图像处理" name="imgproc">
            <NFormItem>
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
                @update:value="(v: number | null) => update({ scale: v ?? undefined })"
              />
            </NFormItem>

            <NFormItem>
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

            <NFormItem>
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
                :max="1000"
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
                :max="1000"
                :step="1"
                @update:value="(v: number | null) => update({ target_height_mm: v ?? 0 })"
              />
            </NFormItem>
          </NCollapseItem>
        </NCollapse>

        <!-- Geometry group -->
        <NCollapse default-expanded-names="geometry" style="margin-bottom: 8px">
          <NCollapseItem title="几何参数" name="geometry">
            <NFormItem>
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
                @update:value="(v: number | null) => update({ pixel_mm: v ?? undefined })"
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
                @update:value="(v: number | null) => update({ layer_height_mm: v ?? undefined })"
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

            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">聚类数</span>
                  </template>
                  {{ tooltips.cluster_count }}
                </NTooltip>
              </template>
              <NInputNumber
                :value="modelValue.cluster_count"
                :min="0"
                :max="65536"
                @update:value="(v: number | null) => update({ cluster_count: v ?? undefined })"
              />
            </NFormItem>

            <NFormItem>
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
                @update:value="(v: string) => update({ dither: v })"
              />
            </NFormItem>

            <NFormItem v-if="modelValue.dither && modelValue.dither !== 'none'">
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
                @update:value="(v: number) => update({ dither_strength: v })"
              />
            </NFormItem>

            <!-- Material filter -->
            <NFormItem label="材质类型">
              <NSelect
                :value="selectedMaterial"
                :options="materialOptions"
                @update:value="(v: string) => { selectedMaterial = v }"
              />
            </NFormItem>

            <!-- Vendor filter -->
            <NFormItem label="厂商">
              <NSelect
                :value="selectedVendor"
                :options="vendorOptionsForMaterial"
                @update:value="(v: string) => { selectedVendor = v }"
              />
            </NFormItem>

            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">颜色数据库</span>
                  </template>
                  {{ tooltips.db_names }}
                </NTooltip>
              </template>
              <NSelect
                :value="modelValue.db_names"
                :options="filteredDBOptions"
                multiple
                placeholder="选择颜色数据库"
                @update:value="(v: string[]) => update({ db_names: v })"
              />
            </NFormItem>

            <NAlert v-if="isLuminaPreset" type="info" :bordered="false" style="margin-bottom: 12px; font-size: 12px">
              当前预设来自
              <a href="https://github.com/MOVIBALE/Lumina-Layers" target="_blank" rel="noopener">Lumina-Layers</a>
              开源项目
            </NAlert>

            <NCollapse v-if="availableChannels.length > 0" style="margin-bottom: 12px">
              <NCollapseItem name="channels-adv">
                <template #header>
                  <NTooltip>
                    <template #trigger>
                      <span class="tip-label">颜色通道筛选</span>
                    </template>
                    {{ tooltips.allowed_channels }}
                  </NTooltip>
                </template>
                <template #header-extra>
                  <span style="font-size: 12px; color: var(--n-text-color-3)">
                    {{ isAllChannelsSelected ? '全部' : `${selectedChannelKeys.length}/${availableChannels.length}` }}
                  </span>
                </template>
                <div style="width: 100%">
                  <NRadioGroup
                    v-if="applicablePresets.length > 1"
                    :value="activeChannelPreset"
                    size="small"
                    style="margin-bottom: 8px"
                    @update:value="(v: string) => applyChannelPreset(v)"
                  >
                    <NRadioButton v-for="p in applicablePresets" :key="p.value" :value="p.value">
                      {{ p.label }}
                    </NRadioButton>
                  </NRadioGroup>
                  <NCheckboxGroup
                    :value="selectedChannelKeys"
                    @update:value="handleChannelKeysChange"
                  >
                    <NSpace item-style="display: flex">
                      <NCheckbox
                        v-for="ch in availableChannels"
                        :key="ch.key"
                        :value="ch.key"
                        :label="`${ch.color} (${ch.material})`"
                      />
                    </NSpace>
                  </NCheckboxGroup>
                </div>
              </NCollapseItem>
            </NCollapse>
          </NCollapseItem>
        </NCollapse>

        <!-- Model gate group -->
        <NCollapse style="margin-bottom: 8px">
          <NCollapseItem title="模型门控" name="model-gate">
            <NFormItem>
              <template #label>
                <NTooltip>
                  <template #trigger>
                    <span class="tip-label">启用模型</span>
                  </template>
                  {{ modelPackAvailable ? tooltips.model_enable : '仅 BambooLab PLA 数据库支持模型增强' }}
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
                @update:value="(v: number | null) => update({ model_threshold: v ?? undefined })"
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
                @update:value="(v: number | null) => update({ model_margin: v ?? undefined })"
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

            <NFormItem>
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
      </NForm>
    </NSpin>
  </NCard>
</template>

<style scoped>
.tip-label {
  cursor: help;
  border-bottom: 1px dashed #999;
}
</style>
