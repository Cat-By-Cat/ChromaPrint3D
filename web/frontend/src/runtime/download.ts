import { getElectronApi } from './platform'
import { createBlobUrl, revokeBlobUrl } from './blob'
import { applySessionHeader } from './session'

function clickDownloadLink(url: string, filename: string): void {
  const link = document.createElement('a')
  link.href = url
  link.download = filename
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
}

export async function openExternalUrl(url: string): Promise<void> {
  const electronDownload = getElectronApi()?.download
  if (electronDownload?.openExternal) {
    await electronDownload.openExternal(url)
    return
  }
  window.open(url, '_blank')
}

export async function downloadFromUrl(url: string, filename: string): Promise<void> {
  const electronDownload = getElectronApi()?.download
  if (electronDownload?.saveUrlAs) {
    await electronDownload.saveUrlAs(url, filename)
    return
  }

  const headers = new Headers()
  applySessionHeader(headers)
  const res = await fetch(url, { credentials: 'include', headers })
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
  const blob = await res.blob()
  const blobUrl = createBlobUrl(blob)
  clickDownloadLink(blobUrl, filename)
  revokeBlobUrl(blobUrl)
}

export async function downloadObjectUrl(url: string, filename: string): Promise<void> {
  const electronDownload = getElectronApi()?.download
  if (electronDownload?.saveObjectUrlAs) {
    await electronDownload.saveObjectUrlAs(url, filename)
    return
  }
  clickDownloadLink(url, filename)
}
