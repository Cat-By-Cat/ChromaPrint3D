<script setup lang="ts">
import { NRadioGroup, NRadioButton } from 'naive-ui'

const props = defineProps<{
  modelValue: 'simple' | 'advanced'
}>()

const emit = defineEmits<{
  'update:modelValue': [value: 'simple' | 'advanced']
}>()
</script>

<template>
  <NRadioGroup
    :value="props.modelValue"
    size="small"
    @update:value="(v: 'simple' | 'advanced') => emit('update:modelValue', v)"
  >
    <NRadioButton value="simple">简易</NRadioButton>
    <NRadioButton value="advanced">高级</NRadioButton>
  </NRadioGroup>
</template>
