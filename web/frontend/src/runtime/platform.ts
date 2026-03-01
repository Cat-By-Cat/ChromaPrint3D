export type RuntimeKind = 'electron' | 'browser'

export function getElectronApi() {
  if (typeof window === 'undefined') return undefined
  return window.electron
}

function hasElectronUserAgent(): boolean {
  if (typeof navigator === 'undefined') return false
  return /Electron/i.test(navigator.userAgent)
}

function hasElectronProcessVersion(): boolean {
  return Boolean(
    (globalThis as { process?: { versions?: { electron?: string } } }).process?.versions?.electron,
  )
}

export function getRuntimeKind(): RuntimeKind {
  if (typeof window === 'undefined') return 'browser'
  if (window.electron) return 'electron'
  if (hasElectronUserAgent()) return 'electron'
  if (hasElectronProcessVersion()) return 'electron'
  return 'browser'
}

export function isElectronRuntime(): boolean {
  return getRuntimeKind() === 'electron'
}

export function isBrowserRuntime(): boolean {
  return getRuntimeKind() === 'browser'
}
