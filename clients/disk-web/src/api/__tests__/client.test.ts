import axios, { AxiosHeaders } from 'axios'
import { describe, it, expect, beforeEach, vi } from 'vitest'
import type { AxiosError, AxiosResponse, InternalAxiosRequestConfig } from 'axios'
import { ApiError } from '../client'

function createStorage(initial: Record<string, string> = {}) {
  let store = { ...initial }
  return {
    getItem: (key: string) => store[key] ?? null,
    setItem: (key: string, value: string) => { store[key] = value },
    removeItem: (key: string) => { delete store[key] },
    clear: () => { store = {} },
    get length() { return Object.keys(store).length },
    key: (_index: number) => null,
    store,
  }
}

describe('ApiError', () => {
  it('creates error with code and message', () => {
    const err = new ApiError(40101, '用户名或密码错误')
    expect(err.name).toBe('ApiError')
    expect(err.code).toBe(40101)
    expect(err.message).toBe('用户名或密码错误')
    expect(err.isAuthError).toBe(false)
  })

  it('identifies auth error codes (40104-40111)', () => {
    expect(new ApiError(40104, '').isAuthError).toBe(true)
    expect(new ApiError(40107, '').isAuthError).toBe(true)
    expect(new ApiError(40111, '').isAuthError).toBe(true)
    expect(new ApiError(40101, '').isAuthError).toBe(false)
    expect(new ApiError(40103, '').isAuthError).toBe(false)
  })

  it('non-auth codes are not flagged', () => {
    expect(new ApiError(50001, '').isAuthError).toBe(false)
    expect(new ApiError(10001, '').isAuthError).toBe(false)
    expect(new ApiError(80001, '').isAuthError).toBe(false)
  })
})

describe('token storage helpers', () => {
  let storage: ReturnType<typeof createStorage>

  beforeEach(() => {
    storage = createStorage()
    vi.stubGlobal('localStorage', storage)
  })

  async function importClient() {
    return import('../client?cachebust=' + Date.now())
  }

  it('setTokens stores access and refresh tokens', async () => {
    const { setTokens } = await importClient()
    setTokens('my-access', 'my-refresh')
    expect(storage.store['access_token']).toBe('my-access')
    expect(storage.store['refresh_token']).toBe('my-refresh')
  })

  it('getAccessToken returns stored token', async () => {
    storage.store['access_token'] = 'stored-at'
    const { getAccessToken } = await importClient()
    expect(getAccessToken()).toBe('stored-at')
  })

  it('getAccessToken returns null when not set', async () => {
    const { getAccessToken } = await importClient()
    expect(getAccessToken()).toBeNull()
  })

  it('getRefreshToken returns stored token', async () => {
    storage.store['refresh_token'] = 'stored-rt'
    const { getRefreshToken } = await importClient()
    expect(getRefreshToken()).toBe('stored-rt')
  })

  it('clearTokens removes both tokens', async () => {
    storage.store['access_token'] = 'at'
    storage.store['refresh_token'] = 'rt'
    const { clearTokens } = await importClient()
    clearTokens()
    expect(storage.store['access_token']).toBeUndefined()
    expect(storage.store['refresh_token']).toBeUndefined()
  })
})

describe('apiClient interceptors', () => {
  beforeEach(() => {
    vi.restoreAllMocks()
  })

  it('request interceptor adds Bearer token when available', async () => {
    const storage = createStorage({ access_token: 'test-token' })
    vi.stubGlobal('localStorage', storage)

    const mod = await import('../client?cachebust=' + Date.now())
    const config: Partial<InternalAxiosRequestConfig> = { headers: new AxiosHeaders({ Authorization: '' }) }
    const handlers = mod.apiClient.interceptors.request as { handlers: Array<{ fulfilled: (config: InternalAxiosRequestConfig) => InternalAxiosRequestConfig }> }
    const result = handlers.handlers[0].fulfilled(config as InternalAxiosRequestConfig)

    expect(result.headers.Authorization).toBe('Bearer test-token')
    vi.unstubAllGlobals()
  })

  it('response interceptor unwraps ApiResponse on code 0', async () => {
    const mod = await import('../client?cachebust=' + Date.now())
    const response = {
      data: { code: 0, message: 'ok', data: { id: 1 } },
      status: 200,
      statusText: 'OK',
      headers: {},
      config: {} as InternalAxiosRequestConfig,
    }

    const handlers = mod.apiClient.interceptors.response as { handlers: Array<{ fulfilled: (response: AxiosResponse) => Promise<unknown> }> }
    const result = await handlers.handlers[0].fulfilled(response as AxiosResponse)
    expect(result).toEqual({ id: 1 })
  })

  it('response interceptor preserves Blob responses without envelope parsing', async () => {
    const mod = await import('../client?cachebust=' + Date.now())
    const blob = new Blob(['file-content'], { type: 'application/octet-stream' })
    const response = {
      data: blob,
      status: 200,
      statusText: 'OK',
      headers: {},
      config: { responseType: 'blob' } as InternalAxiosRequestConfig,
    }

    const handlers = mod.apiClient.interceptors.response as { handlers: Array<{ fulfilled: (response: AxiosResponse) => Promise<unknown> }> }
    const result = await handlers.handlers[0].fulfilled(response as AxiosResponse)
    expect(result).toBe(blob)
  })

  it('response interceptor rejects with ApiError on non-zero code', async () => {
    const mod = await import('../client?cachebust=' + Date.now())
    const response = {
      data: { code: 40101, message: 'Invalid credentials', data: null },
      status: 200,
      statusText: 'OK',
      headers: {},
      config: {} as InternalAxiosRequestConfig,
    }

    const handlers = mod.apiClient.interceptors.response as { handlers: Array<{ fulfilled: (response: AxiosResponse) => Promise<unknown> }> }
    await expect(handlers.handlers[0].fulfilled(response)).rejects.toThrow('用户名或密码错误')
  })

  it('rejects queued requests when shared token refresh fails', async () => {
    const storage = createStorage({ access_token: 'expired-at', refresh_token: 'expired-rt' })
    vi.stubGlobal('localStorage', storage)
    const mod = await import('../client?cachebust=' + Date.now())
    const handlers = mod.apiClient.interceptors.response as {
      handlers: Array<{
        rejected: (error: AxiosError) => Promise<unknown>
      }>
    }

    let rejectRefresh!: (error: Error) => void
    const refreshPromise = new Promise<never>((_resolve, reject) => {
      rejectRefresh = reject
    })
    vi.spyOn(axios, 'post').mockReturnValueOnce(refreshPromise)

    const first = handlers.handlers[0].rejected({
      config: { headers: {} },
      response: { status: 401, data: { code: 40108, message: 'expired', data: null } },
      message: 'Unauthorized',
    } as AxiosError)
    const second = handlers.handlers[0].rejected({
      config: { headers: {} },
      response: { status: 401, data: { code: 40108, message: 'expired', data: null } },
      message: 'Unauthorized',
    } as AxiosError)

    rejectRefresh(new Error('refresh failed'))

    await expect(first).rejects.toMatchObject({ code: 40108, message: '登录已过期，请重新登录' })
    await expect(second).rejects.toMatchObject({ code: 40108, message: '登录已过期，请重新登录' })
    expect(storage.store['access_token']).toBeUndefined()
    expect(storage.store['refresh_token']).toBeUndefined()
    expect(axios.post).toHaveBeenCalledTimes(1)
    vi.unstubAllGlobals()
  })
})

describe('publicClient interceptors', () => {
  it('unwraps ApiResponse on code 0', async () => {
    const mod = await import('../client?cachebust=' + Date.now())
    const response = {
      data: { code: 0, message: 'ok', data: 'hello' },
      status: 200,
      statusText: 'OK',
      headers: {},
      config: {} as InternalAxiosRequestConfig,
    }

    const handlers = mod.publicClient.interceptors.response as { handlers: Array<{ fulfilled: (response: AxiosResponse) => Promise<unknown> }> }
    const result = await handlers.handlers[0].fulfilled(response as AxiosResponse)
    expect(result).toBe('hello')
  })

  it('rejects with ApiError on non-zero code', async () => {
    const mod = await import('../client?cachebust=' + Date.now())
    const response = {
      data: { code: 40001, message: 'Username exists', data: null },
      status: 200,
      statusText: 'OK',
      headers: {},
      config: {} as InternalAxiosRequestConfig,
    }

    const handlers = mod.publicClient.interceptors.response as { handlers: Array<{ fulfilled: (response: AxiosResponse) => Promise<unknown> }> }
    await expect(handlers.handlers[0].fulfilled(response)).rejects.toThrow('用户名已存在')
  })
})

describe('createShareClient', () => {
  it('creates client with X-Share-Token header', async () => {
    const mod = await import('../client?cachebust=' + Date.now())
    const client = mod.createShareClient('share-token-123')
    const headerValue =
      client.defaults.headers['X-Share-Token'] ??
      client.defaults.headers.common?.['X-Share-Token']
    expect(headerValue).toBe('share-token-123')
  })

  it('preserves Blob responses without envelope parsing', async () => {
    const mod = await import('../client?cachebust=' + Date.now())
    const client = mod.createShareClient('share-token-123')
    const blob = new Blob(['shared-file'], { type: 'application/octet-stream' })
    const response = {
      data: blob,
      status: 200,
      statusText: 'OK',
      headers: {},
      config: { responseType: 'blob' } as InternalAxiosRequestConfig,
    }

    const handlers = client.interceptors.response as { handlers: Array<{ fulfilled: (response: AxiosResponse) => Promise<unknown> }> }
    const result = await handlers.handlers[0].fulfilled(response as AxiosResponse)
    expect(result).toBe(blob)
  })

  it('parses Blob-wrapped JSON errors', async () => {
    const mod = await import('../client?cachebust=' + Date.now())
    const client = mod.createShareClient('share-token-123')
    const errorBlob = new Blob([
      JSON.stringify({ code: 60004, message: 'Share denied', data: null }),
    ], { type: 'application/json' })
    const error = {
      message: 'Request failed',
      response: {
        status: 403,
        data: errorBlob,
      },
    }

    const handlers = client.interceptors.response as { handlers: Array<{ rejected: (error: unknown) => Promise<unknown> }> }
    await expect(handlers.handlers[0].rejected(error)).rejects.toThrow('分享访问被拒绝')
  })
})
