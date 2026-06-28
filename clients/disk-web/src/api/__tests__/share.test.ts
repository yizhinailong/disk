import { describe, it, expect, vi, beforeEach } from 'vitest'

const apiClient = {
  post: vi.fn(),
  delete: vi.fn(),
  get: vi.fn(),
  put: vi.fn(),
}
const publicClient = {
  post: vi.fn(),
}
const shareClient = {
  get: vi.fn(),
}
const createShareClient = vi.fn(() => shareClient)

vi.mock('../client', () => ({
  apiClient,
  publicClient,
  createShareClient,
}))

describe('share API auth domains', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('routes owner share management through the authenticated owner client', async () => {
    const { createShare, listShares, getShareDetail, updateShare, cancelShares } = await import('../share')

    await createShare({ file_ids: [1], permission: 'download' })
    await listShares({ page: 1, page_size: 20 })
    await getShareDetail('share-123')
    await updateShare('share-123', { permission: 'view' })
    await cancelShares({ share_ids: ['share-123'] })

    expect(apiClient.post).toHaveBeenCalledWith('/share', { file_ids: [1], permission: 'download' })
    expect(apiClient.get).toHaveBeenCalledWith('/share', { params: { page: 1, page_size: 20 } })
    expect(apiClient.get).toHaveBeenCalledWith('/share/share-123')
    expect(apiClient.put).toHaveBeenCalledWith('/share/share-123', { permission: 'view' })
    expect(apiClient.delete).toHaveBeenCalledWith('/share', { data: { share_ids: ['share-123'] } })
    expect(publicClient.post).not.toHaveBeenCalledWith('/share', expect.anything())
  })

  it('keeps visitor share download on X-Share-Token client and requests Blob response', async () => {
    const { downloadShareFile } = await import('../share')
    const blob = new Blob(['shared-file'])
    shareClient.get.mockResolvedValue(blob)

    const result = await downloadShareFile('share-123', 42, 'share-token')

    expect(createShareClient).toHaveBeenCalledWith('share-token')
    expect(shareClient.get).toHaveBeenCalledWith('/share/download/share-123/42', {
      responseType: 'blob',
      timeout: 300000,
    })
    expect(result).toBe(blob)
  })
})
