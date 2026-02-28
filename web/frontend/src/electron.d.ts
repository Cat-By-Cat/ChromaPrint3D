export {}

type ElectronEnvApi = {
  apiBase?: string
}

type ElectronStorageApi = {
  getItem?: (key: string) => Promise<string | null>
  setItem?: (key: string, value: string) => Promise<void>
}

type ElectronThemeApi = {
  getSystemDarkMode?: () => Promise<boolean>
}

type ElectronDownloadApi = {
  openExternal?: (url: string) => Promise<void>
  saveUrlAs?: (url: string, filename: string) => Promise<void>
  saveObjectUrlAs?: (url: string, filename: string) => Promise<void>
}

type ElectronFileApi = {
  pickSingleFile?: (accept?: string) => Promise<File | null>
}

type ElectronRendererApi = {
  env?: ElectronEnvApi
  storage?: ElectronStorageApi
  theme?: ElectronThemeApi
  download?: ElectronDownloadApi
  file?: ElectronFileApi
}

declare global {
  interface Window {
    electron?: ElectronRendererApi
  }
}
