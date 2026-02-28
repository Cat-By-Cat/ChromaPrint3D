import { onUnmounted } from 'vue'
import { createBlobUrl, revokeBlobUrl } from '../runtime/blob'

export function useObjectUrlLifecycle() {
  const managedUrls = new Set<string>()

  function registerUrl(url: string): string {
    managedUrls.add(url)
    return url
  }

  function createUrl(blob: Blob): string {
    return registerUrl(createBlobUrl(blob))
  }

  function revokeUrl(url: string | null | undefined): void {
    if (!url) return
    managedUrls.delete(url)
    revokeBlobUrl(url)
  }

  function revokeAllUrls(): void {
    for (const url of managedUrls) {
      revokeBlobUrl(url)
    }
    managedUrls.clear()
  }

  onUnmounted(revokeAllUrls)

  return {
    registerUrl,
    createUrl,
    revokeUrl,
    revokeAllUrls,
  }
}
