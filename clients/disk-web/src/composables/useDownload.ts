import { getAccessToken } from '@/api/client'
import { getDownloadInfo } from '@/api/file'
import type { DownloadInfoResponse } from '@/types'

export type DownloadProgressCallback = (loaded: number, total: number, progress: number) => void

export interface DownloadResult {
  blob: Blob
  filename: string
  info: DownloadInfoResponse
}

export function useDownload() {
  async function fetchInfo(fileId: number): Promise<DownloadInfoResponse> {
    return getDownloadInfo(fileId)
  }

  async function startDownload(
    fileId: number,
    options: {
      signal?: AbortSignal
      onProgress?: DownloadProgressCallback
      startByte?: number
    } = {},
  ): Promise<DownloadResult> {
    const { signal, onProgress, startByte = 0 } = options

    const info = await fetchInfo(fileId)
    const filename = info.filename || `file_${fileId}`

    const headers: Record<string, string> = {}
    const token = getAccessToken()
    if (token) {
      headers['Authorization'] = `Bearer ${token}`
    }
    if (startByte > 0 && info.supports_range) {
      headers['Range'] = `bytes=${startByte}-`
    }

    const response = await fetch(`/api/file/download/${fileId}`, {
      method: 'GET',
      headers,
      signal,
    })

    if (!response.ok && response.status !== 206) {
      throw new Error(`下载失败: HTTP ${response.status}`)
    }

    const contentLength = response.headers.get('Content-Length')
    const contentRange = response.headers.get('Content-Range')
    let totalSize = info.file_size

    // Content-Range format: "bytes START-END/TOTAL"
    if (contentRange) {
      const match = contentRange.match(/\/(\d+)/)
      if (match) {
        totalSize = Number(match[1])
      }
    } else if (contentLength) {
      totalSize = Number(contentLength)
    }

    const blob = await readBodyWithProgress(response, totalSize, startByte, onProgress, signal)

    return { blob, filename, info }
  }

  return { startDownload, fetchInfo }
}

async function readBodyWithProgress(
  response: Response,
  totalSize: number,
  startByte: number,
  onProgress?: DownloadProgressCallback,
  signal?: AbortSignal,
): Promise<Blob> {
  const body = response.body
  if (!body || !totalSize) {
    const blob = await response.blob()
    onProgress?.(startByte + blob.size, totalSize, totalSize > 0 ? Math.round(((startByte + blob.size) / totalSize) * 100) : 100)
    return blob
  }

  const reader = body.getReader()
  const chunks: Uint8Array[] = []
  let loaded = startByte

  try {
    while (true) {
      if (signal?.aborted) {
        reader.cancel()
        throw new DOMException('Aborted', 'AbortError')
      }

      const { done, value } = await reader.read()
      if (done) break

      chunks.push(value)
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
