import { onMounted, onUnmounted, watch } from 'vue'
import { useAppStore } from '../../stores/app'

export function useAppLifecycle() {
  const appStore = useAppStore()
  let healthTimer: ReturnType<typeof setInterval> | null = null

  onMounted(async () => {
    await appStore.initTheme()
    await appStore.checkHealth()
    healthTimer = setInterval(() => {
      void appStore.checkHealth()
    }, 15000)
  })

  onUnmounted(() => {
    if (healthTimer) clearInterval(healthTimer)
  })

  watch(
    () => appStore.isDark,
    (dark) => {
      void appStore.persistTheme(dark)
    },
  )
}
