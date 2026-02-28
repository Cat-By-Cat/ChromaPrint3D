import { getElectronApi } from './platform'

export async function getStorageItem(key: string): Promise<string | null> {
  const electronStorage = getElectronApi()?.storage
  if (electronStorage?.getItem) {
    return electronStorage.getItem(key)
  }
  if (typeof localStorage === 'undefined') return null
  return localStorage.getItem(key)
}

export async function setStorageItem(key: string, value: string): Promise<void> {
  const electronStorage = getElectronApi()?.storage
  if (electronStorage?.setItem) {
    await electronStorage.setItem(key, value)
    return
  }
  if (typeof localStorage === 'undefined') return
  localStorage.setItem(key, value)
}
