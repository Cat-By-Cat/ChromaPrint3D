<script setup lang="ts">
import { computed } from 'vue'
import { usePanZoom } from '../../composables/usePanZoom'
import type { PanZoomController } from '../../composables/usePanZoom'

const props = withDefaults(
  defineProps<{
    src?: string | null
    alt?: string
    height?: number | string
    checkerboard?: boolean
    controller?: PanZoomController
    showImage?: boolean
  }>(),
  {
    src: '',
    alt: 'preview',
    height: 300,
    checkerboard: true,
    controller: undefined,
    showImage: true,
  },
)

const internalPanZoom = usePanZoom()
const panZoom = computed<PanZoomController>(() => props.controller ?? internalPanZoom)
const transformValue = computed(() => panZoom.value.previewTransform.value)
const imageSrc = computed(() => props.src ?? '')
const viewportStyle = computed(() => {
  const value = props.height
  return {
    height: typeof value === 'number' ? `${value}px` : value,
  }
})

const viewportClass = computed(() => ({
  'transparent-checkerboard-bg': props.checkerboard,
}))

function handleWheel(event: WheelEvent): void {
  panZoom.value.handleWheel(event)
}

function handleMouseDown(event: MouseEvent): void {
  panZoom.value.handleMouseDown(event)
}

function handleMouseMove(event: MouseEvent): void {
  panZoom.value.handleMouseMove(event)
}

function handleMouseUp(): void {
  panZoom.value.handleMouseUp()
}

function resetView(): void {
  panZoom.value.resetView()
}

defineExpose({
  resetView,
})
</script>

<template>
  <div
    class="zoomable-image-viewport"
    :class="viewportClass"
    :style="viewportStyle"
    @wheel="handleWheel"
    @mousedown="handleMouseDown"
    @mousemove="handleMouseMove"
    @mouseup="handleMouseUp"
    @mouseleave="handleMouseUp"
  >
    <img
      v-if="showImage && imageSrc"
      :src="imageSrc"
      class="zoomable-image-viewport__img"
      :style="{ transform: transformValue }"
      draggable="false"
      :alt="alt"
    />
    <slot :transform="transformValue" />
  </div>
</template>

<style scoped>
.zoomable-image-viewport {
  position: relative;
  width: 100%;
  overflow: hidden;
  border: 1px solid var(--image-viewport-border-color, var(--n-border-color));
  border-radius: 4px;
  cursor: grab;
  user-select: none;
}

.zoomable-image-viewport:active {
  cursor: grabbing;
}

.zoomable-image-viewport__img {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  object-fit: contain;
  transform-origin: 0 0;
  pointer-events: none;
}

:slotted(.zoomable-image-viewport__layer) {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  object-fit: contain;
  transform-origin: 0 0;
  pointer-events: none;
}
</style>
