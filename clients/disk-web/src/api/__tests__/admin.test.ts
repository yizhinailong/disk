import { beforeEach, describe, expect, it, vi } from 'vitest'

const apiClient = {
  delete: vi.fn(),
  get: vi.fn(),
  post: vi.fn(),
  put: vi.fn(),
}

vi.mock('../client', () => ({ apiClient }))

describe('admin share API identifiers', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('uses an encoded external share_id for detail and cancellation', async () => {
    const { deleteShare, getShareDetail } = await import('../admin')

    await getShareDetail('share/id with space')
    await deleteShare('share/id with space')

    expect(apiClient.get).toHaveBeenCalledWith('/admin/shares/share%2Fid%20with%20space')
    expect(apiClient.delete).toHaveBeenCalledWith('/admin/shares/share%2Fid%20with%20space')
  })
})
