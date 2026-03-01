import { getElectronApi, isElectronRuntime } from './platform'

const DEFAULT_ELECTRON_API_BASE = 'http://127.0.0.1:18080'

export type ApiBaseSource = 'electron-env' | 'vite-env' | 'electron-default' | 'same-origin'

type ApiBaseResolution = {
  base: string
  source: ApiBaseSource
}

function normalizeBase(base: string): string {
  return base.replace(/\/+$/, '')
}

function resolveApiBase(): ApiBaseResolution {
  const electronBase = getElectronApi()?.env?.apiBase
  const envBase = import.meta.env.VITE_API_BASE

  if (electronBase?.trim()) {
    return {
      base: normalizeBase(electronBase.trim()),
      source: 'electron-env',
    }
  }
  if (envBase?.trim()) {
    return {
      base: normalizeBase(envBase.trim()),
      source: 'vite-env',
    }
  }
  if (isElectronRuntime()) {
    return {
      base: DEFAULT_ELECTRON_API_BASE,
      source: 'electron-default',
    }
  }
  return {
    base: '',
    source: 'same-origin',
  }
}

export function getApiBase(): string {
  return resolveApiBase().base
}

export function getApiBaseSource(): ApiBaseSource {
  return resolveApiBase().source
}

export function buildApiUrl(path: string): string {
  return `${getApiBase()}${path}`
}
