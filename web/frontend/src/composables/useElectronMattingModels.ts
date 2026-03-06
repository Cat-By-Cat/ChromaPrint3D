import { computed, ref, type ComputedRef } from 'vue'
import { toErrorMessage } from '../runtime/error'
import { isElectronRuntime } from '../runtime/platform'

const CONNECTIVITY_CACHE_TTL_MS = 60_000

export function useElectronMattingModels(hasOnlyOpenCvMethod: ComputedRef<boolean>) {
  const isElectron = isElectronRuntime()
  const modelStatus = ref<ElectronModelDownloadStatus | null>(null)
  const modelStatusLoading = ref(false)
  const modelActionLoading = ref(false)
  const modelProgress = ref<ElectronModelDownloadProgress | null>(null)
  const modelError = ref<string | null>(null)
  const modelConnectivity = ref<ElectronModelConnectivityReport | null>(null)
  const modelConnectivityLoading = ref(false)
  const downloadSessionBaseBytes = ref(0)
  const downloadSessionTotalBytes = ref(0)

  const pendingModelCount = computed(
    () => (modelStatus.value?.missingModels ?? 0) + (modelStatus.value?.invalidModels ?? 0),
  )
  const showModelCard = computed(() => {
    if (!isElectron) return false
    if (modelStatus.value?.running) return true
    if (pendingModelCount.value > 0) return true
    if (hasOnlyOpenCvMethod.value) return true
    return Boolean(modelError.value)
  })
  const modelProgressPercent = computed(() => {
    if (modelProgress.value) return modelProgress.value.percent
    const total = modelStatus.value?.totalModels ?? 0
    if (total <= 0) return 0
    return Number((((modelStatus.value?.installedModels ?? 0) / total) * 100).toFixed(1))
  })
  const modelRunning = computed(
    () => Boolean(modelStatus.value?.running) || modelActionLoading.value,
  )
  const modelConnectivitySummary = computed(() => {
    if (!modelConnectivity.value) return ''
    const report = modelConnectivity.value
    return `连通性检查：可用源 ${report.availableSources}/${report.totalSources}（检测模型 ${report.checkedModels} 个）`
  })
  const showRestartHint = computed(() => {
    if (!isElectron || !modelStatus.value) return false
    return modelStatus.value.totalModels > 0 && pendingModelCount.value === 0 && hasOnlyOpenCvMethod.value
  })
  const requiredDownloadBytes = computed(() => {
    const models = modelStatus.value?.models ?? []
    return models
      .filter((item) => item.state !== 'installed')
      .reduce((sum, item) => sum + Math.max(0, item.sizeBytes), 0)
  })
  const effectiveDownloadTotalBytes = computed(() => {
    if (downloadSessionTotalBytes.value > 0) return downloadSessionTotalBytes.value
    return requiredDownloadBytes.value
  })
  const downloadedSessionBytes = computed(() => {
    const progress = modelProgress.value
    if (!progress) return 0
    return Math.max(0, progress.downloadedBytes - downloadSessionBaseBytes.value)
  })
  const currentDownloadSpeedBytesPerSec = computed(() => {
    const speed = modelProgress.value?.speedBytesPerSec
    if (typeof speed !== 'number' || !Number.isFinite(speed) || speed <= 0) return null
    return speed
  })

  function formatBytes(bytes: number): string {
    if (!Number.isFinite(bytes) || bytes <= 0) return '0 B'
    const units = ['B', 'KB', 'MB', 'GB', 'TB']
    let value = bytes
    let idx = 0
    while (value >= 1024 && idx < units.length - 1) {
      value /= 1024
      idx += 1
    }
    const fixed = value >= 10 || idx === 0 ? 0 : 1
    return `${value.toFixed(fixed)} ${units[idx]}`
  }

  function formatSpeed(bytesPerSec: number): string {
    return `${formatBytes(bytesPerSec)}/s`
  }

  function formatConnectivityCheckedAt(ts: number): string {
    return new Date(ts).toLocaleTimeString('zh-CN', { hour12: false })
  }

  function hasFreshConnectivityReport(report: ElectronModelConnectivityReport | null): boolean {
    if (!report) return false
    return Date.now() - report.checkedAtMs <= CONNECTIVITY_CACHE_TTL_MS
  }

  async function checkModelConnectivity(
    force = false,
  ): Promise<ElectronModelConnectivityReport | null> {
    if (!isElectron) return null
    if (!force && hasFreshConnectivityReport(modelConnectivity.value)) {
      return modelConnectivity.value
    }
    const checkConnectivity = window.electron?.models?.checkConnectivity
    if (!checkConnectivity) return null
    modelConnectivityLoading.value = true
    try {
      const report = await checkConnectivity()
      modelConnectivity.value = report
      if (report.availableSources <= 0) {
        modelError.value = '连通性检查未发现可用下载源，请检查网络后重试。'
      } else if (modelError.value?.includes('连通性检查')) {
        modelError.value = null
      }
      return report
    } catch (error: unknown) {
      modelError.value = toErrorMessage(error, '连通性检查失败')
      return null
    } finally {
      modelConnectivityLoading.value = false
    }
  }

  async function refreshModelStatus() {
    if (!isElectron) return
    const getStatus = window.electron?.models?.getStatus
    if (!getStatus) return
    modelStatusLoading.value = true
    try {
      modelStatus.value = await getStatus()
      if (modelStatus.value.lastError) {
        modelError.value = modelStatus.value.lastError
      }
    } catch (error: unknown) {
      modelError.value = toErrorMessage(error, '获取模型状态失败')
    } finally {
      modelStatusLoading.value = false
    }
  }

  function bindModelProgressListener() {
    if (!isElectron) return
    const modelsApi = window.electron?.models
    const onProgress = modelsApi?.onProgress
    if (!onProgress) return
    modelsApi?.clearProgressListener?.()
    onProgress((payload) => {
      modelProgress.value = payload
      if (payload.type === 'start') {
        downloadSessionBaseBytes.value = payload.downloadedBytes
        downloadSessionTotalBytes.value = Math.max(0, payload.totalBytes - payload.downloadedBytes)
      }
      if (payload.type === 'error') {
        modelError.value = payload.message
      }
      if (payload.type === 'completed' || payload.type === 'cancelled' || payload.type === 'error') {
        modelActionLoading.value = false
        void refreshModelStatus()
      }
    })
  }

  async function handleStartModelDownload() {
    const startDownload = window.electron?.models?.startDownload
    if (!startDownload) return
    modelError.value = null
    modelProgress.value = null
    downloadSessionBaseBytes.value = 0
    downloadSessionTotalBytes.value = requiredDownloadBytes.value
    try {
      const connectivity = await checkModelConnectivity()
      if (!connectivity || connectivity.availableSources <= 0) {
        throw new Error('下载源不可达，请先执行连通性检查并确认至少一个下载源可用。')
      }
      modelActionLoading.value = true
      modelStatus.value = await startDownload()
    } catch (error: unknown) {
      modelError.value = toErrorMessage(error, '模型下载失败')
    } finally {
      modelActionLoading.value = false
      await refreshModelStatus()
    }
  }

  async function handleCancelModelDownload() {
    const cancelDownload = window.electron?.models?.cancelDownload
    if (!cancelDownload) return
    try {
      await cancelDownload()
    } catch (error: unknown) {
      modelError.value = toErrorMessage(error, '取消下载失败')
    }
  }

  async function handleRestartApp() {
    const restartApp = window.electron?.models?.restartApp
    if (!restartApp) return
    try {
      await restartApp()
    } catch (error: unknown) {
      modelError.value = toErrorMessage(error, '重启应用失败')
    }
  }

  function clearModelProgressListener() {
    window.electron?.models?.clearProgressListener?.()
  }

  return {
    bindModelProgressListener,
    checkModelConnectivity,
    clearModelProgressListener,
    currentDownloadSpeedBytesPerSec,
    downloadedSessionBytes,
    effectiveDownloadTotalBytes,
    formatBytes,
    formatConnectivityCheckedAt,
    formatSpeed,
    handleCancelModelDownload,
    handleRestartApp,
    handleStartModelDownload,
    isElectron,
    modelActionLoading,
    modelConnectivity,
    modelConnectivityLoading,
    modelConnectivitySummary,
    modelError,
    modelProgress,
    modelProgressPercent,
    modelRunning,
    modelStatus,
    modelStatusLoading,
    pendingModelCount,
    refreshModelStatus,
    showModelCard,
    showRestartHint,
  }
}
