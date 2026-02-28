import { getElectronApi } from './platform'

function normalizeBase(base: string): string {
  return base.replace(/\/+$/, '')
}

export function getApiBase(): string {
  const electronBase = getElectronApi()?.env?.apiBase
  const envBase = import.meta.env.VITE_API_BASE
  const raw = (electronBase ?? envBase ?? '').trim()
  return normalizeBase(raw)
}

export function buildApiUrl(path: string): string {
  return `${getApiBase()}${path}`
}
