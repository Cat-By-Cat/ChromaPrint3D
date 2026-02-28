export function createBlobUrl(blob: Blob): string {
  return URL.createObjectURL(blob)
}

export function revokeBlobUrl(url: string | null | undefined): void {
  if (!url) return
  URL.revokeObjectURL(url)
}
