import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useTransferStore } from '../transfer'
import { useDownload, saveBlobAsFile } from '@/composables/useDownload'

vi.mock('@/api/file', () => ({
  initUpload: vi.fn(),
  uploadChunk: vi.fn(),
  completeUpload: vi.fn(),
  cancelUpload: vi.fn(),
}))

vi.mock('@/composables/useDownload', () => ({
  useDownload: vi.fn(),
  saveBlobAsFile: vi.fn(),
}))

describe('useTransferStore downloads', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    let id = 0
    vi.stubGlobal('crypto', { randomUUID: () => `task-${++id}` })
  })

  it('delegates large downloads to disk streaming and keeps payload bytes out of store state', async () => {
    const payload = new Uint8Array(10_000_000)
    const startDownload = vi.fn(async (_fileId, options) => {
      options.onProgress?.(5_000_000, 10_000_000, 50)
      options.onProgress?.(10_000_000, 10_000_000, 100)
      return {
        filename: 'large.bin',
        info: {
          file_id: 1,
          filename: 'large.bin',
          file_size: 10_000_000,
          file_hash: 'hash',
          mime_type: 'application/octet-stream',
          supports_range: true,
        },
        savedToDisk: true,
      }
    })
    vi.mocked(useDownload).mockReturnValue({ fetchInfo: vi.fn(), startDownload })

    const store = useTransferStore()
    store.addDownloadTask(1, 'large.bin', 10_000_000)
    await Promise.resolve()
    await Promise.resolve()

    const task = store.downloads[0]!
    expect(startDownload).toHaveBeenCalledWith(
      1,
      expect.objectContaining({ saveToDisk: true, signal: expect.any(AbortSignal) }),
    )
    expect(task.status).toBe('completed')
    expect(task.progress).toBe(100)
    expect(Object.values(task)).not.toContain(payload)
    expect(Object.prototype.hasOwnProperty.call(task, 'chunks')).toBe(false)
    expect(Object.prototype.hasOwnProperty.call(task, 'blob')).toBe(false)
    expect(saveBlobAsFile).not.toHaveBeenCalled()
  })

  it('uses the Blob saver only for fallback downloads', async () => {
    const blob = new Blob(['fallback'])
    vi.mocked(useDownload).mockReturnValue({
      fetchInfo: vi.fn(),
      startDownload: vi.fn(async (_fileId, options) => {
        options.onProgress?.(8, 8, 100)
        return {
          blob,
          filename: 'fallback.txt',
          info: {
            file_id: 1,
            filename: 'fallback.txt',
            file_size: 8,
            file_hash: 'hash',
            mime_type: 'text/plain',
            supports_range: false,
          },
          savedToDisk: false,
        }
      }),
    })

    const store = useTransferStore()
    store.addDownloadTask(1, 'fallback.txt', 8)
    await Promise.resolve()
    await Promise.resolve()

    const task = store.downloads[0]!
    expect(task.status).toBe('completed')
    expect(task.progress).toBe(100)
    expect(saveBlobAsFile).toHaveBeenCalledWith(blob, 'fallback.txt')
    expect(Object.prototype.hasOwnProperty.call(task, 'blob')).toBe(false)
  })

  it('surfaces terminal download failure feedback', async () => {
    vi.mocked(useDownload).mockReturnValue({
      fetchInfo: vi.fn(),
      startDownload: vi.fn(async () => {
        throw new Error('下载失败: HTTP 416')
      }),
    })

    const store = useTransferStore()
    store.addDownloadTask(1, 'broken.bin', 100)
    await Promise.resolve()
    await Promise.resolve()

    const task = store.downloads[0]!
    expect(task.status).toBe('failed')
    expect(task.error).toBe('下载失败: HTTP 416')
    expect(saveBlobAsFile).not.toHaveBeenCalled()
  })
})
