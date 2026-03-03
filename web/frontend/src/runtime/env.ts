import { getElectronApi, isElectronRuntime } from './platform'

const DEFAULT_ELECTRON_API_BASE = 'http://127.0.0.1:18080'
const DEFAULT_ICP_RECORD_URL = 'https://beian.miit.gov.cn/'
const DEFAULT_PUBLIC_SECURITY_RECORD_URL = 'https://beian.mps.gov.cn/'

export type ApiBaseSource = 'electron-env' | 'vite-env' | 'electron-default' | 'same-origin'

type ApiBaseResolution = {
  base: string
  source: ApiBaseSource
}

function normalizeBase(base: string): string {
  return base.replace(/\/+$/, '')
}

function normalizeText(raw: string | undefined): string {
  return raw?.trim() ?? ''
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

export function getSiteIcpNumber(): string {
  return normalizeText(import.meta.env.VITE_SITE_ICP_NUMBER)
}

export function getSiteIcpUrl(): string {
  const customUrl = normalizeText(import.meta.env.VITE_SITE_ICP_URL)
  return customUrl || DEFAULT_ICP_RECORD_URL
}

export function getSitePublicSecurityRecordNumber(): string {
  return normalizeText(import.meta.env.VITE_SITE_PUBLIC_SECURITY_RECORD_NUMBER)
}

export function getSitePublicSecurityRecordUrl(): string {
  const customUrl = normalizeText(import.meta.env.VITE_SITE_PUBLIC_SECURITY_RECORD_URL)
  return customUrl || DEFAULT_PUBLIC_SECURITY_RECORD_URL
}
