<script setup lang="ts">
import { NAlert, NFormItem, NSelect, NTooltip } from 'naive-ui'
import type { SelectOption } from 'naive-ui'

const props = defineProps<{
  material: string
  vendor: string
  materialOptions: SelectOption[]
  vendorOptions: SelectOption[]
  dbNames?: string[]
  dbOptions: SelectOption[]
  dbTooltip: string
  isLuminaPreset: boolean
}>()

const emit = defineEmits<{
  'update:material': [value: string]
  'update:vendor': [value: string]
  'update:dbNames': [value: string[]]
}>()
</script>

<template>
  <NFormItem label="材质类型">
    <NSelect
      :value="props.material"
      :options="props.materialOptions"
      @update:value="(v: string) => emit('update:material', v)"
    />
  </NFormItem>

  <NFormItem label="厂商">
    <NSelect
      :value="props.vendor"
      :options="props.vendorOptions"
      @update:value="(v: string) => emit('update:vendor', v)"
    />
  </NFormItem>

  <NFormItem>
    <template #label>
      <NTooltip>
        <template #trigger>
          <span class="tip-label">颜色数据库</span>
        </template>
        {{ props.dbTooltip }}
      </NTooltip>
    </template>
    <NSelect
      :value="props.dbNames"
      :options="props.dbOptions"
      multiple
      placeholder="选择颜色数据库"
      @update:value="(v: string[]) => emit('update:dbNames', v)"
    />
  </NFormItem>

  <NAlert
    v-if="props.isLuminaPreset"
    type="info"
    :bordered="false"
    style="margin-bottom: 12px; font-size: 12px"
  >
    当前预设来自
    <a href="https://github.com/MOVIBALE/Lumina-Layers" target="_blank" rel="noopener"
      >Lumina-Layers</a
    >
    开源项目
  </NAlert>
</template>

<style scoped>
.tip-label {
  cursor: help;
  border-bottom: 1px dashed var(--n-text-color-3);
}
</style>
