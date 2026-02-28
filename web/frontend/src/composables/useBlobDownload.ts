import { downloadFromUrl, downloadObjectUrl } from '../runtime/download'

function toMessage(err: unknown, fallback: string): string {
  if (err instanceof Error && err.message) return err.message
  return fallback
}

export function useBlobDownload(onError?: (message: string) => void) {
  async function downloadByUrl(url: string, filename: string): Promise<void> {
    try {
      await downloadFromUrl(url, filename)
    } catch (err) {
      onError?.(toMessage(err, '下载失败，请重试'))
      throw err
    }
  }

  async function downloadByObjectUrl(url: string, filename: string): Promise<void> {
    try {
      await downloadObjectUrl(url, filename)
    } catch (err) {
      onError?.(toMessage(err, '下载失败，请重试'))
      throw err
    }
  }

  return {
    downloadByUrl,
    downloadByObjectUrl,
  }
}
