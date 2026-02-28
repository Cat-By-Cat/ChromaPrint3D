import { getElectronApi } from './platform'

export async function detectSystemDarkMode(): Promise<boolean> {
  const electronTheme = getElectronApi()?.theme
  if (electronTheme?.getSystemDarkMode) {
    return electronTheme.getSystemDarkMode()
  }
  return window.matchMedia?.('(prefers-color-scheme: dark)').matches ?? false
}
