<script setup lang="ts">
import { NForm } from 'naive-ui'

defineProps<{
  disabled: boolean
}>()
</script>

<template>
  <NForm label-placement="left" label-width="auto" :disabled="disabled" size="small">
    <slot />
  </NForm>
</template>
