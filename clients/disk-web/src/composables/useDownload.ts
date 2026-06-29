import { getAccessToken, refreshAccessToken } from '@/api/client'
import { getDownloadInfo } from '@/api/file'
import type { DownloadInfoResponse } from '@/types'

export type DownloadProgressCallback = (loaded: number, total: number, progress: number) => void
export type DownloadChunkCallback = (chunk: Uint8Array) => void

export interface DownloadResult {
  blob?: Blob
  filename: string
  info: DownloadInfoResponse
  savedToDisk: boolean
}

interface FileSystemWritableFileStreamLike {
  write(chunk: BufferSource | Blob | string): Promise<void>
  close(): Promise<void>
  abort?(reason?: unknown): Promise<void>
}

interface FileSystemFileHandleLike {
  createWritable(): Promise<FileSystemWritableFileStreamLike>
}

interface SaveFilePickerWindow extends Window {
  showSaveFilePicker?: (options?: {
    suggestedName?: string
    types?: Array<{
      description: string
      accept: Record<string, string[]>
    }>
  }) => Promise<FileSystemFileHandleLike>
}

interface DownloadOptions {
  signal?: AbortSignal
  onProgress?: DownloadProgressCallback
  onChunk?: DownloadChunkCallback
  onInfo?: (info: DownloadInfoResponse) => void
  startByte?: number
  saveToDisk?: boolean
}

export function useDownload() {
  async function fetchInfo(fileId: number): Promise<DownloadInfoResponse> {
    return getDownloadInfo(fileId)
  }

  async function startDownload(
    fileId: number,
    options: DownloadOptions = {},
  ): Promise<DownloadResult> {
    const { signal, onProgress, onChunk, onInfo, startByte = 0, saveToDisk = false } = options

    const info = await fetchInfo(fileId)
    onInfo?.(info)
    const filename = info.filename || `file_${fileId}`
    const rangeStart = startByte > 0 && info.supports_range ? startByte : 0

    const response = await fetchOwnerDownload(fileId, info, rangeStart, signal)
    validateDownloadResponse(response, rangeStart)

    const totalSize = resolveTotalSize(response, info.file_size, rangeStart)

    if (saveToDisk) {
      const saved = await saveResponseToDisk(response, filename, totalSize, rangeStart, onProgress, signal)
      if (saved) {
        return { filename, info, savedToDisk: true }
      }
    }

    const blob = await readBodyWithProgress(response, totalSize, rangeStart, onProgress, onChunk, signal)

    return { blob, filename, info, savedToDisk: false }
  }

  return { startDownload, fetchInfo }
}

async function fetchOwnerDownload(
  fileId: number,
  info: DownloadInfoResponse,
  startByte: number,
  signal?: AbortSignal,
): Promise<Response> {
  const first = await fetchDownloadRequest(fileId, info, startByte, getAccessToken(), signal)
  if (first.status !== 401) {
    return first
  }

  const refreshedToken = await refreshAccessToken()
  return fetchDownloadRequest(fileId, info, startByte, refreshedToken, signal)
}

function fetchDownloadRequest(
  fileId: number,
  info: DownloadInfoResponse,
  startByte: number,
  token: string | null,
  signal?: AbortSignal,
): Promise<Response> {
  const headers: Record<string, string> = {}
  if (token) {
    headers['Authorization'] = `Bearer ${token}`
  }
  if (startByte > 0 && info.supports_range) {
    headers['Range'] = `bytes=${startByte}-`
  }

  return fetch(`/api/file/download/${fileId}`, {
    method: 'GET',
    headers,
    signal,
  })
}

function validateDownloadResponse(response: Response, startByte: number): void {
  if (!response.ok && response.status !== 206) {
    throw new Error(`下载失败: HTTP ${response.status}`)
  }

  if (startByte === 0) {
    return
  }

  if (response.status !== 206) {
    throw new Error('续传失败: 服务器未返回部分内容')
  }

  const contentRange = response.headers.get('Content-Range')
  const rangeStart = contentRange?.match(/^bytes\s+(\d+)-/i)?.[1]
  if (rangeStart === undefined || Number(rangeStart) !== startByte) {
    throw new Error('续传失败: 响应范围不匹配')
  }
}

function resolveTotalSize(response: Response, fallbackSize: number, rangeStart: number): number {
  const contentLength = response.headers.get('Content-Length')
  const contentRange = response.headers.get('Content-Range')
  let totalSize = fallbackSize

  // Content-Range format: "bytes START-END/TOTAL"
  if (contentRange) {
    const match = contentRange.match(/\/(\d+)/)
    if (match) {
      totalSize = Number(match[1])
    }
  } else if (contentLength && rangeStart === 0) {
    totalSize = Number(contentLength)
  }

  return totalSize
}

async function saveResponseToDisk(
  response: Response,
  filename: string,
  totalSize: number,
  startByte: number,
  onProgress?: DownloadProgressCallback,
  signal?: AbortSignal,
): Promise<boolean> {
  const picker = (window as SaveFilePickerWindow).showSaveFilePicker
  const body = response.body
  if (!picker || !body || startByte > 0) {
    return false
  }

  let writable: FileSystemWritableFileStreamLike | null = null
  const reader = body.getReader()
  let loaded = startByte

  try {
    const handle = await picker({
      suggestedName: filename,
      types: [{ description: '下载文件', accept: { 'application/octet-stream': ['.*'] } }],
    })
    writable = await handle.createWritable()

    while (true) {
      if (signal?.aborted) {
        await reader.cancel()
        throw new DOMException('Aborted', 'AbortError')
      }

      const { done, value } = await reader.read()
      if (done) break

      await writable.write(value)
      loaded += value.length
      const progress = totalSize > 0 ? Math.round((loaded / totalSize) * 100) : 100
      onProgress?.(loaded, totalSize, Math.min(progress, 100))
    }

    await writable.close()
    return true
  } catch (err) {
    await writable?.abort?.(err)
    if (err instanceof DOMException && err.name === 'AbortError') {
      throw err
    }
    return false
  } finally {
    reader.releaseLock()
  }
}

async function readBodyWithProgress(
  response: Response,
  totalSize: number,
  startByte: number,
  onProgress?: DownloadProgressCallback,
  onChunk?: DownloadChunkCallback,
  signal?: AbortSignal,
): Promise<Blob> {
  const body = response.body
  if (!body || !totalSize) {
    const blob = await response.blob()
    if (onChunk) {
      onChunk(new Uint8Array(await blob.arrayBuffer()))
    }
    onProgress?.(startByte + blob.size, totalSize, totalSize > 0 ? Math.round(((startByte + blob.size) / totalSize) * 100) : 100)
    return blob
  }

  const reader = body.getReader()
  const chunks: Uint8Array[] = []
  let loaded = startByte

  try {
    while (true) {
      if (signal?.aborted) {
        await reader.cancel()
        throw new DOMException('Aborted', 'AbortError')
      }

      const { done, value } = await reader.read()
      if (done) break

      chunks.push(value)
      onChunk?.(value)
      loaded += value.length
      const progress = Math.round((loaded / totalSize) * 100)
      onProgress?.(loaded, totalSize, Math.min(progress, 100))
    }
  } finally {
    reader.releaseLock()
  }

  return new Blob(chunks as BlobPart[])
}

export function saveBlobAsFile(blob: Blob, filename: string): void {
  const url = URL.createObjectURL(blob)
  const anchor = document.createElement('a')
  anchor.href = url
  anchor.download = filename
  anchor.style.display = 'none'
  document.body.appendChild(anchor)
  anchor.click()
  document.body.removeChild(anchor)
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}
