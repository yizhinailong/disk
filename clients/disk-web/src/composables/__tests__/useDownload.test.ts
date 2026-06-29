import { describe, it, expect, beforeEach, vi } from 'vitest'
import { useDownload, saveBlobAsFile } from '../../composables/useDownload'
import * as fileApi from '@/api/file'
import { getAccessToken, refreshAccessToken } from '@/api/client'
import type { DownloadInfoResponse } from '@/types'

vi.mock('@/api/file')
vi.mock('@/api/client', () => ({
  getAccessToken: vi.fn(() => 'fake-token'),
  getRefreshToken: vi.fn(() => 'fake-refresh'),
  refreshAccessToken: vi.fn(),
  setTokens: vi.fn(),
  clearTokens: vi.fn(),
}))

function mockDownloadInfo(overrides: Partial<DownloadInfoResponse> = {}): DownloadInfoResponse {
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

function streamFromChunks(chunks: Uint8Array[]): ReadableStream<Uint8Array> {
  return new ReadableStream({
    start(controller) {
      chunks.forEach((chunk) => controller.enqueue(chunk))
      controller.close()
    },
  })
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
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        headers: new Headers({ 'Content-Length': '100' }),
        body: null,
        blob: async () => mockBlob,
      })

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

    it('streams response to the browser file picker when saveToDisk is enabled', async () => {
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockDownloadInfo({
        filename: 'large.bin',
        file_size: 6,
        mime_type: 'application/octet-stream',
      }))

      const chunks = [new Uint8Array([1, 2, 3]), new Uint8Array([4, 5, 6])]
      const write = vi.fn(async () => undefined)
      const close = vi.fn(async () => undefined)
      const createWritable = vi.fn(async () => ({ write, close }))
      const picker = vi.fn(async () => ({ createWritable }))
      const originalPicker = (window as unknown as { showSaveFilePicker?: unknown }).showSaveFilePicker
      Object.defineProperty(window, 'showSaveFilePicker', { value: picker, configurable: true })

      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        headers: new Headers({ 'Content-Length': '6' }),
        body: streamFromChunks(chunks),
        blob: async () => new Blob(chunks as BlobPart[]),
      })
      const onProgress = vi.fn()

      const { startDownload } = useDownload()
      const result = await startDownload(1, { saveToDisk: true, onProgress })

      expect(result.savedToDisk).toBe(true)
      expect(result.blob).toBeUndefined()
      expect(picker).toHaveBeenCalledWith(expect.objectContaining({ suggestedName: 'large.bin' }))
      expect(write).toHaveBeenNthCalledWith(1, chunks[0])
      expect(write).toHaveBeenNthCalledWith(2, chunks[1])
      expect(close).toHaveBeenCalled()
      expect(onProgress).toHaveBeenLastCalledWith(6, 6, 100)

      globalThis.fetch = originalFetch
      Object.defineProperty(window, 'showSaveFilePicker', { value: originalPicker, configurable: true })
    })

    it('throws on non-ok response', async () => {
      const mockInfo = mockDownloadInfo()
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockInfo)

      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: false,
        status: 500,
        headers: new Headers(),
      })

      const { startDownload } = useDownload()
      await expect(startDownload(1)).rejects.toThrow('下载失败: HTTP 500')

      globalThis.fetch = originalFetch
    })


    it('refreshes token and retries once on 401 response', async () => {
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockDownloadInfo())
      vi.mocked(refreshAccessToken).mockResolvedValue('refreshed-token')

      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn()
        .mockResolvedValueOnce({
          ok: false,
          status: 401,
          headers: new Headers(),
        })
        .mockResolvedValueOnce({
          ok: true,
          status: 200,
          headers: new Headers({ 'Content-Length': '100' }),
          body: null,
          blob: async () => new Blob(['ok']),
        })

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
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: false,
        status: 401,
        headers: new Headers(),
      })

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
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: false,
        status: 206,
        headers: new Headers({
          'Content-Range': 'bytes 500-9999/10000',
        }),
        body: null,
        blob: async () => mockBlob,
      })

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
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: false,
        status: 206,
        headers: new Headers({ 'Content-Range': 'bytes 0-99/100' }),
      })

      const { startDownload } = useDownload()
      await expect(startDownload(1, { startByte: 50 })).rejects.toThrow('续传失败: 响应范围不匹配')

      globalThis.fetch = originalFetch
    })

    it('uses fallback filename when info has none', async () => {
      const mockInfo = mockDownloadInfo({ filename: '' })
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockInfo)

      const mockBlob = new Blob(['data'])
      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        headers: new Headers({ 'Content-Length': '100' }),
        body: null,
        blob: async () => mockBlob,
      })

      const { startDownload } = useDownload()
      const result = await startDownload(1)
      expect(result.filename).toBe('file_1')

      globalThis.fetch = originalFetch
    })

    it('sends request without Authorization when no token', async () => {
      vi.mocked(getAccessToken).mockReturnValue(null)
      const mockInfo = mockDownloadInfo()
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockInfo)

      const mockBlob = new Blob(['content'])
      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        headers: new Headers({ 'Content-Length': '100' }),
        body: null,
        blob: async () => mockBlob,
      })

      const { startDownload } = useDownload()
      await startDownload(1)

      const callArgs = (globalThis.fetch as ReturnType<typeof vi.fn>).mock.calls[0]!
      const headers = callArgs[1]!.headers as Record<string, string>
      expect(headers.Authorization).toBeUndefined()

      globalThis.fetch = originalFetch
    })
    it('streams directly to browser file-system sink without returning a Blob when available', async () => {
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockDownloadInfo({ file_size: 6 }))

      const write = vi.fn(async () => undefined)
      const close = vi.fn(async () => undefined)
      const abort = vi.fn(async () => undefined)
      const showSaveFilePicker = vi.fn(async () => ({
        createWritable: async () => ({ write, close, abort }),
      }))
      const originalPicker = (window as typeof window & { showSaveFilePicker?: unknown }).showSaveFilePicker
      ;(window as typeof window & { showSaveFilePicker?: unknown }).showSaveFilePicker = showSaveFilePicker

      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        headers: new Headers({ 'Content-Length': '6' }),
        body: streamFromChunks([new Uint8Array([1, 2]), new Uint8Array([3, 4, 5, 6])]),
      })
      const progress = vi.fn()

      const { startDownload } = useDownload()
      const result = await startDownload(1, { saveToDisk: true, onProgress: progress })

      expect(result.savedToDisk).toBe(true)
      expect(result.blob).toBeUndefined()
      expect(showSaveFilePicker).toHaveBeenCalledWith(expect.objectContaining({ suggestedName: 'doc.txt' }))
      expect(write).toHaveBeenCalledTimes(2)
      expect(close).toHaveBeenCalledTimes(1)
      expect(abort).not.toHaveBeenCalled()
      expect(progress).toHaveBeenLastCalledWith(6, 6, 100)

      globalThis.fetch = originalFetch
      ;(window as typeof window & { showSaveFilePicker?: unknown }).showSaveFilePicker = originalPicker
    })

    it('falls back to Blob download and surfaces progress when streaming save is unavailable', async () => {
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockDownloadInfo({ file_size: 4 }))

      const originalPicker = (window as typeof window & { showSaveFilePicker?: unknown }).showSaveFilePicker
      ;(window as typeof window & { showSaveFilePicker?: unknown }).showSaveFilePicker = undefined

      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        headers: new Headers({ 'Content-Length': '4' }),
        body: streamFromChunks([new Uint8Array([1]), new Uint8Array([2, 3, 4])]),
      })
      const progress = vi.fn()

      const { startDownload } = useDownload()
      const result = await startDownload(1, { saveToDisk: true, onProgress: progress })

      expect(result.savedToDisk).toBe(false)
      expect(result.blob).toBeInstanceOf(Blob)
      expect(result.blob?.size).toBe(4)
      expect(progress).toHaveBeenLastCalledWith(4, 4, 100)

      globalThis.fetch = originalFetch
      ;(window as typeof window & { showSaveFilePicker?: unknown }).showSaveFilePicker = originalPicker
    })

    it('throws abort errors from the streaming save path as terminal interruption feedback', async () => {
      vi.mocked(fileApi.getDownloadInfo).mockResolvedValue(mockDownloadInfo({ file_size: 4 }))

      const controller = new AbortController()
      const write = vi.fn(async () => {
        controller.abort()
      })
      const abort = vi.fn(async () => undefined)
      const originalPicker = (window as typeof window & { showSaveFilePicker?: unknown }).showSaveFilePicker
      ;(window as typeof window & { showSaveFilePicker?: unknown }).showSaveFilePicker = vi.fn(async () => ({
        createWritable: async () => ({ write, close: vi.fn(async () => undefined), abort }),
      }))

      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        headers: new Headers({ 'Content-Length': '4' }),
        body: streamFromChunks([new Uint8Array([1]), new Uint8Array([2])]),
      })

      const { startDownload } = useDownload()
      await expect(startDownload(1, { saveToDisk: true, signal: controller.signal })).rejects.toThrow(DOMException)
      expect(abort).toHaveBeenCalled()

      globalThis.fetch = originalFetch
      ;(window as typeof window & { showSaveFilePicker?: unknown }).showSaveFilePicker = originalPicker
    })
  })

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
