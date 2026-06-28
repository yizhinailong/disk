import { describe, it, expect, vi, beforeEach } from 'vitest'

const apiClient = {
  post: vi.fn(),
  delete: vi.fn(),
  get: vi.fn(),
  put: vi.fn(),
}

vi.mock('../client', () => ({
  apiClient,
}))

describe('file API upload contract', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('uploads chunks as raw binary with backend query parameters', async () => {
    const { uploadChunk } = await import('../file')
    const blob = new Blob(['chunk-data'], { type: 'application/octet-stream' })

    apiClient.post.mockResolvedValue({ chunk_index: 2, uploaded: true })

    await uploadChunk('upload-123', 2, '0123456789abcdef0123456789abcdef', blob)

    expect(apiClient.post).toHaveBeenCalledWith('/file/upload/chunk', blob, {
      params: {
        upload_id: 'upload-123',
        chunk_index: 2,
        chunk_hash: '0123456789abcdef0123456789abcdef',
      },
      headers: { 'Content-Type': 'application/octet-stream' },
      timeout: 120000,
    })
  })

  it('uses the authenticated owner client for upload lifecycle calls', async () => {
    const { initUpload, completeUpload, cancelUpload } = await import('../file')

    await initUpload({
      filename: 'doc.txt',
      file_size: 12,
      file_hash: '0123456789abcdef0123456789abcdef',
      parent_id: 0,
    })
    await completeUpload({ upload_id: 'upload-123' })
    await cancelUpload('upload-123')

    expect(apiClient.post).toHaveBeenCalledWith('/file/upload/init', {
      filename: 'doc.txt',
      file_size: 12,
      file_hash: '0123456789abcdef0123456789abcdef',
      parent_id: 0,
    })
    expect(apiClient.post).toHaveBeenCalledWith('/file/upload/complete', { upload_id: 'upload-123' })
    expect(apiClient.delete).toHaveBeenCalledWith('/file/upload/upload-123')
  })
})
