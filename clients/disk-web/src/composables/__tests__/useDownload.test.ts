import { describe, it, expect, beforeEach, vi } from 'vitest'
import { useDownload, saveBlobAsFile } from '../../composables/useDownload'
import * as fileApi from '@/api/file'
import { getAccessToken, refreshAccessToken } from '@/api/client'

vi.mock('@/api/file')
vi.mock('@/api/client', () => ({
  getAccessToken: vi.fn(() => 'fake-token'),
  getRefreshToken: vi.fn(() => 'fake-refresh'),
  refreshAccessToken: vi.fn(),
  setTokens: vi.fn(),
  clearTokens: vi.fn(),
}))

function mockDownloadInfo(overrides = {}) {
  return {
    file_id: 1,
    filename: 'doc.txt',
    file_size: 100,
    file_hash: 'hash',
    mime_type: 'text/plain',
    supports_range: false,
    ...overrides,
  }
}

function mockResponse(options: {
  status?: number
  ok?: boolean
  headers?: HeadersInit
  blob?: Blob
  body?: ReadableStream<Uint8Array> | null
} = {}) {
  const status = options.status ?? 200
  return {
    ok: options.ok ?? (status >= 200 && status < 300),
    status,
    headers: new Headers(options.headers),
    body: options.body ?? null,
    blob: async () => options.blob ?? new Blob(['content']),
  } as Response
}

describe('useDownload', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    vi.mocked(getAccessToken).mockReturnValue('fake-token')
    vi.mocked(refreshAccessToken).mockResolvedValue('new-token')
  })

  describe('fetchInfo', () => {
    it('calls getDownloadInfo with file id', async () => {
      const mockInfo = mockDownloadInfo({
        file_id: 42,
        filename: 'test.pdf',
        file_size: 1024,
        file_hash: 'abc123',
        mime_type: 'application/pdf',
      })
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockInfo)

      const { fetchInfo } = useDownload()
      const result = await fetchInfo(42)

      expect(fileApi.getDownloadInfo).toHaveBeenCalledWith(42)
      expect(result).toEqual(mockInfo)
    })
  })

  describe('startDownload', () => {
    it('downloads file with correct headers', async () => {
      const mockInfo = mockDownloadInfo()
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockInfo)

      const mockBlob = new Blob(['content'], { type: 'text/plain' })
      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue(mockResponse({
        headers: { 'Content-Length': '100' },
        blob: mockBlob,
      }))

      const { startDownload } = useDownload()
      const result = await startDownload(1)

      expect(result.blob).toBe(mockBlob)
      expect(result.filename).toBe('doc.txt')
      expect(result.info).toEqual(mockInfo)
      expect(globalThis.fetch).toHaveBeenCalledWith(
        '/api/file/download/1',
        expect.objectContaining({
          method: 'GET',
          headers: expect.objectContaining({
            Authorization: 'Bearer fake-token',
          }),
        }),
      )

      globalThis.fetch = originalFetch
    })

    it('throws on non-ok response', async () => {
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockDownloadInfo())

      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue(mockResponse({ status: 500, ok: false }))

      const { startDownload } = useDownload()
      await expect(startDownload(1)).rejects.toThrow('下载失败: HTTP 500')

      globalThis.fetch = originalFetch
    })

    it('refreshes token and retries once on 401 response', async () => {
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockDownloadInfo())
      vi.mocked(refreshAccessToken).mockResolvedValue('refreshed-token')

      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn()
        .mockResolvedValueOnce(mockResponse({ status: 401, ok: false }))
        .mockResolvedValueOnce(mockResponse({ headers: { 'Content-Length': '100' }, blob: new Blob(['ok']) }))

      const { startDownload } = useDownload()
      await startDownload(1)

      expect(refreshAccessToken).toHaveBeenCalledTimes(1)
      expect(globalThis.fetch).toHaveBeenNthCalledWith(
        2,
        '/api/file/download/1',
        expect.objectContaining({
          headers: expect.objectContaining({ Authorization: 'Bearer refreshed-token' }),
        }),
      )

      globalThis.fetch = originalFetch
    })

    it('propagates refresh failure after 401 response', async () => {
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockDownloadInfo())
      vi.mocked(refreshAccessToken).mockRejectedValue(new Error('refresh failed'))

      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue(mockResponse({ status: 401, ok: false }))

      const { startDownload } = useDownload()
      await expect(startDownload(1)).rejects.toThrow('refresh failed')
      expect(globalThis.fetch).toHaveBeenCalledTimes(1)

      globalThis.fetch = originalFetch
    })

    it('includes Range header for resume when startByte > 0', async () => {
      const mockInfo = mockDownloadInfo({
        filename: 'big.zip',
        file_size: 10000,
        mime_type: 'application/zip',
        supports_range: true,
      })
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockInfo)

      const mockBlob = new Blob(['partial'])
      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue(mockResponse({
        status: 206,
        ok: false,
        headers: { 'Content-Range': 'bytes 500-9999/10000' },
        blob: mockBlob,
      }))

      const { startDownload } = useDownload()
      await startDownload(1, { startByte: 500 })

      expect(globalThis.fetch).toHaveBeenCalledWith(
        '/api/file/download/1',
        expect.objectContaining({
          headers: expect.objectContaining({
            Range: 'bytes=500-',
          }),
        }),
      )

      globalThis.fetch = originalFetch
    })

    it('rejects resumed response when Content-Range does not match requested offset', async () => {
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockDownloadInfo({ supports_range: true }))

      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue(mockResponse({
        status: 206,
        ok: false,
        headers: { 'Content-Range': 'bytes 0-99/100' },
      }))

      const { startDownload } = useDownload()
      await expect(startDownload(1, { startByte: 50 })).rejects.toThrow('续传失败: 响应范围不匹配')

      globalThis.fetch = originalFetch
    })

    it('uses fallback filename when info has none', async () => {
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockDownloadInfo({ filename: '' }))

      const mockBlob = new Blob(['data'])
      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue(mockResponse({
        headers: { 'Content-Length': '100' },
        blob: mockBlob,
      }))

      const { startDownload } = useDownload()
      const result = await startDownload(1)
      expect(result.filename).toBe('file_1')

      globalThis.fetch = originalFetch
    })

    it('sends request without Authorization when no token', async () => {
      vi.mocked(getAccessToken).mockReturnValue(null)
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockDownloadInfo())

      const mockBlob = new Blob(['content'])
      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue(mockResponse({
        headers: { 'Content-Length': '100' },
        blob: mockBlob,
      }))

      const { startDownload } = useDownload()
      await startDownload(1)

      const callArgs = (globalThis.fetch as ReturnType<typeof vi.fn>).mock.calls[0]!
      const headers = callArgs[1]!.headers as Record<string, string>
      expect(headers.Authorization).toBeUndefined()

      globalThis.fetch = originalFetch
    })
  })
})

describe('saveBlobAsFile', () => {
  it('creates anchor element and triggers download', () => {
    const blob = new Blob(['test'], { type: 'text/plain' })
    const createObjectURLSpy = vi.spyOn(URL, 'createObjectURL').mockReturnValue('blob:test')
    const revokeObjectURLSpy = vi.spyOn(URL, 'revokeObjectURL').mockImplementation(() => {})

    const anchorMock = {
      href: '',
      download: '',
      style: { display: '' },
      click: vi.fn(),
    }
    const createElementSpy = vi.spyOn(document, 'createElement').mockReturnValue(anchorMock as unknown as HTMLAnchorElement)
    const appendSpy = vi.spyOn(document.body, 'appendChild').mockImplementation(() => null as unknown as HTMLAnchorElement)
    const removeSpy = vi.spyOn(document.body, 'removeChild').mockImplementation(() => null as unknown as HTMLAnchorElement)

    saveBlobAsFile(blob, 'test.txt')

    expect(createObjectURLSpy).toHaveBeenCalledWith(blob)
    expect(anchorMock.download).toBe('test.txt')
    expect(anchorMock.click).toHaveBeenCalled()

    createObjectURLSpy.mockRestore()
    revokeObjectURLSpy.mockRestore()
    createElementSpy.mockRestore()
    appendSpy.mockRestore()
    removeSpy.mockRestore()
  })
})
