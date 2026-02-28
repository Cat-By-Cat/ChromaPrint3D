export function getElectronApi() {
  if (typeof window === 'undefined') return undefined
  return window.electron
}

export function isElectronRuntime(): boolean {
  if (typeof window === 'undefined') return false
  if (window.electron) return true
  return Boolean(
    (globalThis as { process?: { versions?: { electron?: string } } }).process?.versions?.electron,
  )
}

export function isBrowserRuntime(): boolean {
  return !isElectronRuntime()
}
