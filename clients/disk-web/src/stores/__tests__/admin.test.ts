import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useAdminStore } from '../admin'
import * as adminApi from '@/api/admin'
import type { AdminUserDetailResponse, AdminUserItem } from '@/types'

vi.mock('@/api/admin')
vi.mock('element-plus', () => ({
  ElMessage: {
    success: vi.fn(),
    error: vi.fn(),
  },
}))

const baseUser: AdminUserItem = {
  id: 7,
  username: 'alice',
  email: 'alice@example.test',
  nickname: 'Alice',
  status: 1,
  role: 0,
  storage_used: 2 * 1024 ** 3,
  storage_reserved: 1024 ** 3,
  storage_quota: 5 * 1024 ** 3,
  created_at: '2026-01-01 00:00:00',
  updated_at: '2026-01-01 00:00:00',
}

const updatedUser: AdminUserDetailResponse = {
  id: 7,
  username: 'alice',
  email: 'alice@example.test',
  nickname: 'Alice',
  avatar: '',
  status: 1,
  role: 0,
  storage_used: 2 * 1024 ** 3,
  storage_reserved: 1024 ** 3,
  storage_quota: 6 * 1024 ** 3,
  created_at: '2026-01-01 00:00:00',
  updated_at: '2026-01-02 00:00:00',
}

describe('useAdminStore quota updates', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
  })

  it('updates list and detail state after successful available-space change', async () => {
    vi.mocked(adminApi.changeUserAvailableSpace).mockResolvedValue({ user: updatedUser })
    vi.mocked(adminApi.listUsers).mockResolvedValue({
      items: [{ ...baseUser, storage_quota: updatedUser.storage_quota }],
      pagination: { page: 1, page_size: 20, total: 1, total_pages: 1 },
    })
    vi.mocked(adminApi.getUserDetail).mockResolvedValue(updatedUser)

    const store = useAdminStore()
    store.users = [{ ...baseUser }]
    store.currentUserDetail = { ...baseUser, avatar: '' }

    const result = await store.changeUserAvailableSpace(7, 3)
    await store.fetchUsers({ page: 1, page_size: 20 })
    await store.fetchUserDetail(7)

    expect(adminApi.changeUserAvailableSpace).toHaveBeenCalledWith(7, { available_space_g: 3 })
    expect(result).toEqual(updatedUser)
    expect(store.users[0]).toMatchObject({
      id: 7,
      storage_used: updatedUser.storage_used,
      storage_reserved: updatedUser.storage_reserved,
      storage_quota: updatedUser.storage_quota,
    })
    expect(store.currentUserDetail).toEqual(updatedUser)
  })

  it('does not show unsaved quota values when backend rejects the update', async () => {
    vi.mocked(adminApi.changeUserAvailableSpace).mockRejectedValue(new Error('quota below used space'))

    const store = useAdminStore()
    store.users = [{ ...baseUser }]
    store.currentUserDetail = { ...baseUser, avatar: '' }

    await expect(store.changeUserAvailableSpace(7, 0)).rejects.toThrow('quota below used space')

    expect(store.users[0]).toMatchObject({
      storage_used: baseUser.storage_used,
      storage_reserved: baseUser.storage_reserved,
      storage_quota: baseUser.storage_quota,
    })
    expect(store.currentUserDetail).toMatchObject({
      storage_used: baseUser.storage_used,
      storage_reserved: baseUser.storage_reserved,
      storage_quota: baseUser.storage_quota,
    })
  })
})
