import { describe, it, expect, beforeEach, vi } from 'vitest'
import type { AxiosResponse, InternalAxiosRequestConfig } from 'axios'
import { ApiError } from '../client'

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
  let store: Record<string, string>

  beforeEach(() => {
    store = {}
    const storage = {
      getItem: (key: string) => store[key] ?? null,
      setItem: (key: string, value: string) => { store[key] = value },
      removeItem: (key: string) => { delete store[key] },
      clear: () => { store = {} },
      get length() { return Object.keys(store).length },
      key: (_index: number) => null,
    }
    vi.stubGlobal('localStorage', storage)
  })

  async function importClient() {
    return import('../client?cachebust=' + Date.now())
  }

  it('setTokens stores access and refresh tokens', async () => {
    const { setTokens } = await importClient()
    setTokens('my-access', 'my-refresh')
    expect(store['access_token']).toBe('my-access')
    expect(store['refresh_token']).toBe('my-refresh')
  })

  it('getAccessToken returns stored token', async () => {
    store['access_token'] = 'stored-at'
    const { getAccessToken } = await importClient()
    expect(getAccessToken()).toBe('stored-at')
  })

  it('getAccessToken returns null when not set', async () => {
    const { getAccessToken } = await importClient()
    expect(getAccessToken()).toBeNull()
  })

  it('getRefreshToken returns stored token', async () => {
    store['refresh_token'] = 'stored-rt'
    const { getRefreshToken } = await importClient()
    expect(getRefreshToken()).toBe('stored-rt')
  })

  it('clearTokens removes both tokens', async () => {
    store['access_token'] = 'at'
    store['refresh_token'] = 'rt'
    const { clearTokens } = await importClient()
    clearTokens()
    expect(store['access_token']).toBeUndefined()
    expect(store['refresh_token']).toBeUndefined()
  })
})

describe('apiClient interceptors', () => {
  it('request interceptor adds Bearer token when available', async () => {
    const store: Record<string, string> = { access_token: 'test-token' }
    vi.stubGlobal('localStorage', {
      getItem: (key: string) => store[key] ?? null,
      setItem: (key: string, value: string) => { store[key] = value },
      removeItem: (key: string) => { delete store[key] },
      clear: () => Object.keys(store).forEach((k) => delete store[k]),
      get length() { return Object.keys(store).length },
      key: () => null,
    })

    const mod = await import('../client?cachebust=' + Date.now())
    const config: Partial<InternalAxiosRequestConfig> = { headers: { Authorization: '' } }
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
