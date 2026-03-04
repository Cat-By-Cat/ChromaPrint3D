import { contextBridge, ipcRenderer } from 'electron'

const API_BASE_ARG_PREFIX = '--chromaprint3d-api-base='
const IPC_GET_API_BASE = 'electron:getApiBase'
const IPC_GET_UPLOAD_LIMITS = 'electron:getUploadLimits'
const IPC_PICK_SINGLE_FILE = 'electron:pickSingleFile'
const IPC_SET_WINDOW_BACKGROUND = 'electron:setWindowBackground'
const IPC_MODELS_GET_STATUS = 'electron:modelsGetStatus'
const IPC_MODELS_CHECK_CONNECTIVITY = 'electron:modelsCheckConnectivity'
const IPC_MODELS_START_DOWNLOAD = 'electron:modelsStartDownload'
const IPC_MODELS_CANCEL_DOWNLOAD = 'electron:modelsCancelDownload'
const IPC_MODELS_RESTART_APP = 'electron:modelsRestartApp'
const IPC_MODELS_PROGRESS_EVENT = 'electron:modelsProgress'
const DARK_MODE_MEDIA_QUERY = '(prefers-color-scheme: dark)'

type PickedFilePayload = {
  name: string
  mimeType: string
  bytesBase64: string
}

type UploadLimitsPayload = {
  uploadMaxMb?: unknown
  uploadMaxPixels?: unknown
}

type ModelSourcePayload = {
  name: string
  baseUrl: string
}

type ModelFileStatusPayload = {
  path: string
  sizeBytes: number
  expectedSha256: string
  state: 'installed' | 'missing' | 'invalid'
  exists: boolean
  message?: string
}

type ModelDownloadStatusPayload = {
  repository: string
  dataDir: string
  modelDir: string
  manifestPath: string
  sources: ModelSourcePayload[]
  models: ModelFileStatusPayload[]
  totalModels: number
  installedModels: number
  missingModels: number
  invalidModels: number
  running: boolean
  currentFilePath?: string
  lastError?: string
}

type ModelSourceConnectivityPayload = {
  name: string
  baseUrl: string
  ok: boolean
  checkedModels: number
  reachableModels: number
  statusCode?: number
  responseTimeMs: number
  message: string
}

type ModelConnectivityReportPayload = {
  repository: string
  checkedAtMs: number
  totalSources: number
  availableSources: number
  checkedModels: number
  sources: ModelSourceConnectivityPayload[]
}

type ModelDownloadProgressPayload = {
  type:
    | 'start'
    | 'file-start'
    | 'file-progress'
    | 'retry'
    | 'source-fallback'
    | 'completed'
    | 'cancelled'
    | 'error'
  message: string
  downloadedBytes: number
  totalBytes: number
  percent: number
  filePath?: string
  fileDownloadedBytes?: number
  fileTotalBytes?: number
  sourceName?: string
  sourceUrl?: string
  attempt?: number
  speedBytesPerSec?: number
}

let modelProgressListener: ((event: Electron.IpcRendererEvent, payload: ModelDownloadProgressPayload) => void) | null =
  null

function readApiBaseFromProcessArgs(): string {
  const arg = process.argv.find((token) => token.startsWith(API_BASE_ARG_PREFIX))
  if (!arg) return ''
  return arg.slice(API_BASE_ARG_PREFIX.length)
}

function readApiBaseFromMainProcess(): string {
  try {
    const value = ipcRenderer.sendSync(IPC_GET_API_BASE)
    return typeof value === 'string' ? value : ''
  } catch {
    return ''
  }
}

function parsePositiveInteger(raw: unknown): number | undefined {
  if (typeof raw === 'number') {
    if (Number.isInteger(raw) && raw > 0) return raw
    return undefined
  }
  if (typeof raw !== 'string') return undefined
  const normalized = raw.trim()
  if (!normalized) return undefined
  const parsed = Number(normalized)
  if (!Number.isInteger(parsed) || parsed <= 0) return undefined
  return parsed
}

function readUploadLimitsFromMainProcess(): { uploadMaxMb?: number; uploadMaxPixels?: number } {
  try {
    const value = ipcRenderer.sendSync(IPC_GET_UPLOAD_LIMITS) as UploadLimitsPayload | null
    if (!value || typeof value !== 'object') return {}
    return {
      uploadMaxMb: parsePositiveInteger(value.uploadMaxMb),
      uploadMaxPixels: parsePositiveInteger(value.uploadMaxPixels),
    }
  } catch {
    return {}
  }
}

function base64ToArrayBuffer(base64: string): ArrayBuffer {
  const binary = atob(base64)
  const output = new ArrayBuffer(binary.length)
  const bytes = new Uint8Array(output)
  for (let i = 0; i < binary.length; i += 1) {
    bytes[i] = binary.charCodeAt(i)
  }
  return output
}

function createFileObject(payload: PickedFilePayload): File {
  const data = base64ToArrayBuffer(payload.bytesBase64)
  if (typeof File !== 'undefined') {
    return new File([data], payload.name, { type: payload.mimeType, lastModified: Date.now() })
  }

  const blob = new Blob([data], { type: payload.mimeType }) as Blob & {
    name?: string
    lastModified?: number
  }
  Object.defineProperty(blob, 'name', { value: payload.name })
  Object.defineProperty(blob, 'lastModified', { value: Date.now() })
  return blob as unknown as File
}

function triggerDownload(url: string, filename: string): void {
  const link = document.createElement('a')
  link.href = url
  link.download = filename
  link.style.display = 'none'
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
}

async function saveUrlAs(url: string, filename: string): Promise<void> {
  const response = await fetch(url, { credentials: 'include' })
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`)
  }
  const blob = await response.blob()
  const objectUrl = URL.createObjectURL(blob)
  try {
    triggerDownload(objectUrl, filename)
  } finally {
    setTimeout(() => URL.revokeObjectURL(objectUrl), 0)
  }
}

function saveObjectUrlAs(url: string, filename: string): void {
  triggerDownload(url, filename)
}

function getStorageItem(key: string): string | null {
  try {
    return localStorage.getItem(key)
  } catch {
    return null
  }
}

function setStorageItem(key: string, value: string): void {
  try {
    localStorage.setItem(key, value)
  } catch {
    // ignore storage failures in restricted environments
  }
}

function getSystemDarkMode(): boolean {
  return window.matchMedia?.(DARK_MODE_MEDIA_QUERY).matches ?? false
}

contextBridge.exposeInMainWorld('electron', {
  env: {
    apiBase: readApiBaseFromMainProcess() || readApiBaseFromProcessArgs(),
    ...readUploadLimitsFromMainProcess(),
  },
  storage: {
    getItem: async (key: string): Promise<string | null> => getStorageItem(key),
    setItem: async (key: string, value: string): Promise<void> => {
      setStorageItem(key, value)
    },
  },
  theme: {
    getSystemDarkMode: async (): Promise<boolean> => getSystemDarkMode(),
    setWindowBackground: async (dark: boolean): Promise<void> => {
      await ipcRenderer.invoke(IPC_SET_WINDOW_BACKGROUND, dark)
    },
  },
  download: {
    openExternal: async (url: string): Promise<void> => {
      await ipcRenderer.invoke('electron:openExternal', url)
    },
    saveUrlAs: async (url: string, filename: string): Promise<void> => {
      await saveUrlAs(url, filename)
    },
    saveObjectUrlAs: async (url: string, filename: string): Promise<void> => {
      saveObjectUrlAs(url, filename)
    },
  },
  file: {
    pickSingleFile: async (accept?: string): Promise<File | null> => {
      const payload = (await ipcRenderer.invoke(IPC_PICK_SINGLE_FILE, accept)) as
        | PickedFilePayload
        | null
      if (!payload) return null
      return createFileObject(payload)
    },
  },
  models: {
    getStatus: async (): Promise<ModelDownloadStatusPayload> => {
      return (await ipcRenderer.invoke(IPC_MODELS_GET_STATUS)) as ModelDownloadStatusPayload
    },
    checkConnectivity: async (): Promise<ModelConnectivityReportPayload> => {
      return (await ipcRenderer.invoke(
        IPC_MODELS_CHECK_CONNECTIVITY,
      )) as ModelConnectivityReportPayload
    },
    startDownload: async (): Promise<ModelDownloadStatusPayload> => {
      return (await ipcRenderer.invoke(IPC_MODELS_START_DOWNLOAD)) as ModelDownloadStatusPayload
    },
    cancelDownload: async (): Promise<boolean> => {
      const result = await ipcRenderer.invoke(IPC_MODELS_CANCEL_DOWNLOAD)
      return Boolean(result)
    },
    restartApp: async (): Promise<void> => {
      await ipcRenderer.invoke(IPC_MODELS_RESTART_APP)
    },
    onProgress: (listener: (payload: ModelDownloadProgressPayload) => void): void => {
      if (modelProgressListener) {
        ipcRenderer.removeListener(IPC_MODELS_PROGRESS_EVENT, modelProgressListener)
      }
      modelProgressListener = (
        _event: Electron.IpcRendererEvent,
        payload: ModelDownloadProgressPayload,
      ) => {
        listener(payload)
      }
      ipcRenderer.on(IPC_MODELS_PROGRESS_EVENT, modelProgressListener)
    },
    clearProgressListener: (): void => {
      if (!modelProgressListener) return
      ipcRenderer.removeListener(IPC_MODELS_PROGRESS_EVENT, modelProgressListener)
      modelProgressListener = null
    },
  },
})
