import { afterEach, describe, expect, it, vi } from 'vitest'

const electronRuntime = vi.hoisted(() => ({
  download: {
    saveObjectUrlAs: vi.fn(),
  },
}))

vi.mock('./platform', () => ({
  getElectronApi: () => ({
    download: electronRuntime.download,
  }),
  isElectronRuntime: () => false,
}))

vi.mock('./blob', () => ({
  createBlobUrl: vi.fn(() => 'blob:test-download'),
  revokeBlobUrl: vi.fn(),
}))

import { downloadFromUrl } from './download'
import { getSessionHeaderName, setSessionToken } from './session'
import { revokeBlobUrl } from './blob'

describe('runtime download', () => {
  afterEach(() => {
    vi.clearAllMocks()
    vi.unstubAllGlobals()
    setSessionToken(null)
  })

  it('在 Electron 中下载受保护资源时应通过带会话头的 fetch 获取 blob', async () => {
    setSessionToken('session-download')
    const fetchMock = vi.fn().mockResolvedValue(new Response('3mf-bytes', { status: 200 }))
    vi.stubGlobal('fetch', fetchMock)
    electronRuntime.download.saveObjectUrlAs.mockResolvedValue(undefined)

    await downloadFromUrl('/api/v1/tasks/task-1/artifacts/result', 'result.3mf')

    expect(fetchMock).toHaveBeenCalledTimes(1)
    const [, requestInit] = fetchMock.mock.calls[0] as [string, RequestInit]
    expect(new Headers(requestInit.headers).get(getSessionHeaderName())).toBe('session-download')
    expect(electronRuntime.download.saveObjectUrlAs).toHaveBeenCalledWith(
      'blob:test-download',
      'result.3mf',
    )
    expect(revokeBlobUrl).toHaveBeenCalledWith('blob:test-download')
  })
})
