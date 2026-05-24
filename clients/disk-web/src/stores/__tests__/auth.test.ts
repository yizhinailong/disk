import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useAuthStore } from '../auth'
import * as authApi from '@/api/auth'
import { setTokens, clearTokens, getAccessToken, getRefreshToken } from '@/api/client'

vi.mock('@/api/auth')
vi.mock('@/api/client', () => ({
  setTokens: vi.fn(),
  clearTokens: vi.fn(),
  getAccessToken: vi.fn(() => null),
  getRefreshToken: vi.fn(() => null),
}))

function makeJwt(payload: object): string {
  const header = btoa(JSON.stringify({ alg: 'HS256', typ: 'JWT' }))
  const body = btoa(JSON.stringify(payload))
  return `${header}.${body}.fake-signature`
}

describe('useAuthStore', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
  })

  describe('initial state', () => {
    it('starts unauthenticated', () => {
      const store = useAuthStore()
      expect(store.isAuthenticated).toBe(false)
      expect(store.accessToken).toBeNull()
      expect(store.user).toBeNull()
      expect(store.isAdmin).toBe(false)
    })
  })

  describe('login', () => {
    it('stores tokens and user on successful login', async () => {
      const fakeUser = {
        id: 1,
        username: 'testuser',
        email: 'test@example.com',
        nickname: 'Test',
        avatar: null,
        storage_used: 0,
        storage_quota: 10737418240,
      }
      const exp = Math.floor(Date.now() / 1000) + 7200
      const accessToken = makeJwt({ user_id: 1, username: 'testuser', role: 0, exp, iat: 1 })
      const refreshToken = 'fake-refresh-token'

      vi.mocked(authApi.login).mockResolvedValue({
        access_token: accessToken,
        refresh_token: refreshToken,
        token_type: 'Bearer',
        expires_in: 7200,
        user: fakeUser,
      })

      const store = useAuthStore()
      await store.login('testuser', 'password123')

      expect(setTokens).toHaveBeenCalledWith(accessToken, refreshToken)
      expect(store.accessToken).toBe(accessToken)
      expect(store.refreshToken).toBe(refreshToken)
      expect(store.user).toEqual(fakeUser)
      expect(store.isAuthenticated).toBe(true)
      expect(store.isAdmin).toBe(false)
    })

    it('detects admin role from JWT', async () => {
      const exp = Math.floor(Date.now() / 1000) + 7200
      const accessToken = makeJwt({ user_id: 1, username: 'admin', role: 1, exp, iat: 1 })
      vi.mocked(authApi.login).mockResolvedValue({
        access_token: accessToken,
        refresh_token: 'rt',
        token_type: 'Bearer',
        expires_in: 7200,
        user: {
          id: 1,
          username: 'admin',
          email: 'admin@example.com',
          nickname: 'Admin',
          avatar: null,
          storage_used: 0,
          storage_quota: 10737418240,
        },
      })

      const store = useAuthStore()
      await store.login('admin', 'password')
      expect(store.isAdmin).toBe(true)
    })

    it('propagates login failure', async () => {
      vi.mocked(authApi.login).mockRejectedValue(new Error('Invalid credentials'))

      const store = useAuthStore()
      await expect(store.login('bad', 'creds')).rejects.toThrow('Invalid credentials')
      expect(store.isAuthenticated).toBe(false)
    })
  })

  describe('logout', () => {
    it('clears auth state even when API fails', async () => {
      const exp = Math.floor(Date.now() / 1000) + 7200
      const accessToken = makeJwt({ user_id: 1, username: 'test', role: 0, exp, iat: 1 })

      const store = useAuthStore()
      store.accessToken = accessToken
      store.refreshToken = 'rt'
      store.user = { id: 1, username: 'test', email: 't@t.com', nickname: 'T', avatar: null, storage_used: 0, storage_quota: 100 }

      vi.mocked(authApi.logout).mockRejectedValue(new Error('Network error'))

      await store.logout()

      expect(store.accessToken).toBeNull()
      expect(store.user).toBeNull()
      expect(clearTokens).toHaveBeenCalled()
    })
  })

  describe('refreshAccessToken', () => {
    it('refreshes tokens using refresh token', async () => {
      const newExp = Math.floor(Date.now() / 1000) + 7200
      const newAccess = makeJwt({ user_id: 1, username: 'test', role: 0, exp: newExp, iat: 1 })
      const newRefresh = 'new-rt'

      vi.mocked(getRefreshToken).mockReturnValue('old-rt')
      vi.mocked(authApi.refreshToken).mockResolvedValue({
        access_token: newAccess,
        refresh_token: newRefresh,
        expires_in: 7200,
      })

      const store = useAuthStore()
      const result = await store.refreshAccessToken()

      expect(result).toBe(newAccess)
      expect(setTokens).toHaveBeenCalledWith(newAccess, newRefresh)
    })

    it('returns null when no refresh token available', async () => {
      vi.mocked(getRefreshToken).mockReturnValue(null)

      const store = useAuthStore()
      const result = await store.refreshAccessToken()

      expect(result).toBeNull()
    })
  })

  describe('clearAuth', () => {
    it('resets all auth state', () => {
      const store = useAuthStore()
      store.accessToken = 'at'
      store.refreshToken = 'rt'
      store.user = { id: 1, username: 'test', email: 't@t.com', nickname: 'T', avatar: null, storage_used: 0, storage_quota: 100 }
      store.userRole = 1
      store.tokenExpiry = 9999999999

      store.clearAuth()

      expect(store.accessToken).toBeNull()
      expect(store.refreshToken).toBeNull()
      expect(store.user).toBeNull()
      expect(store.userRole).toBe(0)
      expect(store.tokenExpiry).toBe(0)
    })
  })

  describe('setUser', () => {
    it('updates user data', () => {
      const store = useAuthStore()
      const user = { id: 2, username: 'new', email: 'n@n.com', nickname: 'New', avatar: null, storage_used: 0, storage_quota: 100 }
      store.setUser(user)
      expect(store.user).toEqual(user)
    })
  })

  describe('isAuthenticated', () => {
    it('returns false when token is expired', () => {
      const store = useAuthStore()
      store.accessToken = 'some-token'
      store.tokenExpiry = Math.floor(Date.now() / 1000) - 100
      expect(store.isAuthenticated).toBe(false)
    })

    it('returns true when token is valid and not expired', () => {
      const store = useAuthStore()
      store.accessToken = 'some-token'
      store.tokenExpiry = Math.floor(Date.now() / 1000) + 3600
      expect(store.isAuthenticated).toBe(true)
    })
  })

  describe('loadTokensFromStorage', () => {
    it('loads tokens from localStorage into state', () => {
      const exp = Math.floor(Date.now() / 1000) + 7200
      const access = makeJwt({ user_id: 1, username: 'test', role: 0, exp, iat: 1 })
      const refresh = 'stored-rt'

      vi.mocked(getAccessToken).mockReturnValue(access)
      vi.mocked(getRefreshToken).mockReturnValue(refresh)

      const store = useAuthStore()
      store.loadTokensFromStorage()

      expect(store.accessToken).toBe(access)
      expect(store.refreshToken).toBe(refresh)
      expect(store.tokenExpiry).toBe(exp)
    })

    it('does nothing when no tokens in storage', () => {
      vi.mocked(getAccessToken).mockReturnValue(null)
      vi.mocked(getRefreshToken).mockReturnValue(null)

      const store = useAuthStore()
      store.loadTokensFromStorage()

      expect(store.accessToken).toBeNull()
    })
  })
})
