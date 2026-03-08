<script setup lang="ts">
import { computed } from 'vue'
import { NButton, NSpace } from 'naive-ui'

type ActionButtonType =
  | 'default'
  | 'tertiary'
  | 'primary'
  | 'info'
  | 'success'
  | 'warning'
  | 'error'

const props = withDefaults(
  defineProps<{
    primaryLabel: string
    primaryType?: ActionButtonType
    primaryDisabled?: boolean
    primaryShowProgress?: boolean
    primaryProgressPercent?: number
    secondaryVisible?: boolean
    secondaryLabel?: string
    secondaryType?: ActionButtonType
    secondaryDisabled?: boolean
    secondaryLoading?: boolean
  }>(),
  {
    primaryType: 'primary',
    primaryDisabled: false,
    primaryShowProgress: false,
    primaryProgressPercent: 0,
    secondaryVisible: false,
    secondaryLabel: '',
    secondaryType: 'success',
    secondaryDisabled: false,
    secondaryLoading: false,
  },
)

const emit = defineEmits<{
  (e: 'primary-click'): void
  (e: 'secondary-click'): void
}>()

const primaryProgressWidth = computed(() => {
  if (!props.primaryShowProgress) return '0%'
  const normalized = Math.max(0, Math.min(100, props.primaryProgressPercent))
  return `${Math.max(normalized, 6)}%`
})
</script>

<template>
  <NSpace vertical :size="8">
    <NButton
      :type="primaryType"
      block
      size="large"
      class="progress-action-button"
      :disabled="primaryDisabled"
      @click="emit('primary-click')"
    >
      <span
        v-if="primaryShowProgress"
        class="progress-action-button__progress"
        :style="{ width: primaryProgressWidth }"
      />
      <span class="progress-action-button__content">{{ primaryLabel }}</span>
    </NButton>

    <NButton
      v-if="secondaryVisible"
      :type="secondaryType"
      block
      size="large"
      :disabled="secondaryDisabled"
      :loading="secondaryLoading"
      @click="emit('secondary-click')"
    >
      {{ secondaryLabel }}
    </NButton>
  </NSpace>
</template>

<style scoped>
.progress-action-button {
  position: relative;
  overflow: hidden;
}

.progress-action-button__progress {
  position: absolute;
  inset: 0 auto 0 0;
  background: rgba(255, 255, 255, 0.24);
  pointer-events: none;
  transition: width 0.25s ease;
}

.progress-action-button__content {
  position: relative;
  z-index: 1;
  display: inline-flex;
  width: 100%;
  align-items: center;
  justify-content: center;
}
</style>
