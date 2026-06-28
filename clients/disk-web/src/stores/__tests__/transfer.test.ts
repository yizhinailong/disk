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

  it('tracks received bytes and saves assembled chunks on completion', async () => {
    vi.mocked(useDownload).mockReturnValue({
      fetchInfo: vi.fn(),
      startDownload: vi.fn(async (_fileId, options) => {
        options.onInfo?.({
          file_id: 1,
          filename: 'file.txt',
          file_size: 6,
          file_hash: 'hash',
          mime_type: 'text/plain',
          supports_range: true,
        })
        options.onChunk?.(new Uint8Array([1, 2, 3]))
        options.onProgress?.(3, 6, 50)
        options.onChunk?.(new Uint8Array([4, 5, 6]))
        options.onProgress?.(6, 6, 100)
        return {
          blob: new Blob([new Uint8Array([1, 2, 3, 4, 5, 6])]),
          filename: 'file.txt',
          info: {
            file_id: 1,
            filename: 'file.txt',
            file_size: 6,
            file_hash: 'hash',
            mime_type: 'text/plain',
            supports_range: true,
          },
        }
      }),
    })

    const store = useTransferStore()
    store.addDownloadTask(1, 'file.txt', 6)
    await Promise.resolve()
    await Promise.resolve()

    const task = store.downloads[0]!
    expect(task.status).toBe('completed')
    expect(task.progress).toBe(100)
    expect(task.received_bytes).toBe(6)
    expect(task.chunks).toEqual([])
    expect(saveBlobAsFile).toHaveBeenCalledWith(expect.any(Blob), 'file.txt')
  })

  it('preserves partial bytes on pause and resumes from saved offset', async () => {
    let resolveFirst!: (value: unknown) => void
    const firstDownload = new Promise((resolve) => { resolveFirst = resolve })
    const startDownload = vi.fn()
      .mockImplementationOnce(async (_fileId, options) => {
        options.onInfo?.({
          file_id: 1,
          filename: 'file.txt',
          file_size: 10,
          file_hash: 'hash',
          mime_type: 'text/plain',
          supports_range: true,
        })
        options.onChunk?.(new Uint8Array([1, 2, 3, 4]))
        options.onProgress?.(4, 10, 40)
        await firstDownload
        throw new DOMException('Aborted', 'AbortError')
      })
      .mockImplementationOnce(async (_fileId, options) => {
        expect(options.startByte).toBe(4)
        options.onChunk?.(new Uint8Array([5, 6, 7, 8, 9, 10]))
        options.onProgress?.(10, 10, 100)
        return {
          blob: new Blob([new Uint8Array([5, 6, 7, 8, 9, 10])]),
          filename: 'file.txt',
          info: {
            file_id: 1,
            filename: 'file.txt',
            file_size: 10,
            file_hash: 'hash',
            mime_type: 'text/plain',
            supports_range: true,
          },
        }
      })
    vi.mocked(useDownload).mockReturnValue({ fetchInfo: vi.fn(), startDownload })

    const store = useTransferStore()
    store.addDownloadTask(1, 'file.txt', 10)
    await Promise.resolve()

    store.pauseDownloadTask('task-1')
    resolveFirst(undefined)
    await Promise.resolve()

    let task = store.downloads[0]!
    expect(task.status).toBe('paused')
    expect(task.received_bytes).toBe(4)
    expect(task.chunks).toHaveLength(1)

    store.resumeDownloadTask('task-1')
    await Promise.resolve()
    await Promise.resolve()

    task = store.downloads[0]!
    expect(task.status).toBe('completed')
    expect(startDownload).toHaveBeenNthCalledWith(
      2,
      1,
      expect.objectContaining({ startByte: 4 }),
    )
  })

  it('clears partial bytes for non-resumable paused downloads', async () => {
    let resolveFirst!: (value: unknown) => void
    const firstDownload = new Promise((resolve) => { resolveFirst = resolve })
    vi.mocked(useDownload).mockReturnValue({
      fetchInfo: vi.fn(),
      startDownload: vi.fn(async (_fileId, options) => {
        options.onInfo?.({
          file_id: 1,
          filename: 'file.txt',
          file_size: 10,
          file_hash: 'hash',
          mime_type: 'text/plain',
          supports_range: false,
        })
        options.onChunk?.(new Uint8Array([1, 2]))
        options.onProgress?.(2, 10, 20)
        await firstDownload
        throw new DOMException('Aborted', 'AbortError')
      }),
    })

    const store = useTransferStore()
    store.addDownloadTask(1, 'file.txt', 10)
    await Promise.resolve()

    store.pauseDownloadTask('task-1')
    resolveFirst(undefined)
    await Promise.resolve()

    const task = store.downloads[0]!
    expect(task.status).toBe('paused')
    expect(task.received_bytes).toBe(0)
    expect(task.chunks).toEqual([])
    expect(task.error).toContain('不支持断点续传')
  })

  it('clears partial bytes on range mismatch failure', async () => {
    vi.mocked(useDownload).mockReturnValue({
      fetchInfo: vi.fn(),
      startDownload: vi.fn(async () => {
        throw new Error('续传失败: 响应范围不匹配')
      }),
    })

    const store = useTransferStore()
    store.downloads.push({
      id: 'task-1',
      file_id: '1',
      file_name: 'file.txt',
      file_size: 10,
      status: 'paused',
      progress: 40,
      received_bytes: 4,
      total_size: 10,
      supports_range: true,
      chunks: [new Uint8Array([1, 2, 3, 4])],
    })

    store.resumeDownloadTask('task-1')
    await Promise.resolve()
    await Promise.resolve()

    const task = store.downloads[0]!
    expect(task.status).toBe('failed')
    expect(task.received_bytes).toBe(0)
    expect(task.progress).toBe(0)
    expect(task.chunks).toEqual([])
    expect(task.error).toBe('续传失败: 响应范围不匹配')
  })
})
