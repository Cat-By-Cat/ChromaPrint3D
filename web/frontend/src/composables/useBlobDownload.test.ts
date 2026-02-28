import { describe, expect, it, vi } from 'vitest'

vi.mock('../runtime/download', () => ({
  downloadFromUrl: vi.fn(),
  downloadObjectUrl: vi.fn(),
}))

import { useBlobDownload } from './useBlobDownload'
import { downloadFromUrl, downloadObjectUrl } from '../runtime/download'

describe('useBlobDownload', () => {
  it('should proxy successful URL and object-url downloads', async () => {
    vi.mocked(downloadFromUrl).mockResolvedValueOnce()
    vi.mocked(downloadObjectUrl).mockResolvedValueOnce()
    const onError = vi.fn()
    const helper = useBlobDownload(onError)

    await helper.downloadByUrl('/result.bin', 'result.bin')
    await helper.downloadByObjectUrl('blob:abc', 'result.svg')

    expect(downloadFromUrl).toHaveBeenCalledWith('/result.bin', 'result.bin')
    expect(downloadObjectUrl).toHaveBeenCalledWith('blob:abc', 'result.svg')
    expect(onError).not.toHaveBeenCalled()
  })

  it('should report and rethrow when download fails', async () => {
    vi.mocked(downloadFromUrl).mockRejectedValueOnce(new Error('network failed'))
    const onError = vi.fn()
    const helper = useBlobDownload(onError)

    await expect(helper.downloadByUrl('/x', 'x')).rejects.toThrow('network failed')
    expect(onError).toHaveBeenCalledWith('network failed')
  })
})
