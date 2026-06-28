import axios from 'axios'
import type { AxiosInstance, AxiosError, InternalAxiosRequestConfig } from 'axios'
import type { ApiResponse } from '@/types'

// ==================== 错误码中文映射 ====================

const ERROR_MESSAGE_MAP: Record<number, string> = {
  10001: '参数无效',
  10002: '数据校验失败',
  10003: '资源不存在',
  10004: '资源冲突',
  10005: '请求过于频繁',
  10006: '服务器内部错误',
  40001: '用户名已存在',
  40002: '邮箱已被注册',
  40100: '用户不存在',
  40101: '用户名或密码错误',
  40102: '账户已被锁定',
  40103: '账户已被禁用',
  40104: '登录已过期，请重新登录',
  40105: '登录已过期，请重新登录',
  40106: '登录已过期，请重新登录',
  40107: '登录已过期，请重新登录',
  40108: '登录已过期，请重新登录',
  40109: '登录已过期，请重新登录',
  40110: '登录已过期，请重新登录',
  40111: '登录已过期，请重新登录',
  50001: '文件名无效',
  50004: '存储空间不足',
  50005: '文件不存在',
  50006: '文件夹不存在',
  50007: '文件已存在',
  60001: '分享不存在',
  60002: '分享已过期',
  60003: '分享密码错误',
  60004: '分享访问被拒绝',
  80001: '需要管理员权限',
}

/** 认证相关错误码范围 */
function isAuthErrorCode(code: number): boolean {
  return code >= 40104 && code <= 40111
}

/** 获取错误码对应的中文消息 */
function getErrorMessage(code: number, fallback: string): string {
  return ERROR_MESSAGE_MAP[code] ?? fallback
}

function isBinaryResponse(data: unknown): data is Blob | ArrayBuffer {
  return data instanceof Blob || data instanceof ArrayBuffer
}

async function parseBlobApiResponse(blob: Blob): Promise<ApiResponse<unknown> | undefined> {
  if (!blob.type.includes('application/json')) {
    return undefined
  }

  try {
    return JSON.parse(await blob.text()) as ApiResponse<unknown>
  } catch {
    return undefined
  }
}

function isApiResponse(value: unknown): value is ApiResponse<unknown> {
  if (!value || typeof value !== 'object') {
    return false
  }

  const maybe = value as Partial<ApiResponse<unknown>>
  return typeof maybe.code === 'number' && typeof maybe.message === 'string'
}

function unwrapApiResponse(responseData: unknown): unknown | Promise<unknown> {
  if (isBinaryResponse(responseData)) {
    return responseData
  }

  const body = responseData as ApiResponse<unknown>
  if (body.code === 0) {
    return body.data
  }

  const message = getErrorMessage(body.code, body.message)
  return Promise.reject(new ApiError(body.code, message))
}

async function rejectApiError(error: AxiosError<ApiResponse<unknown> | Blob>): Promise<never> {
  if (error.response) {
    let body: ApiResponse<unknown> | undefined
    if (error.response.data instanceof Blob) {
      body = await parseBlobApiResponse(error.response.data)
    } else if (isApiResponse(error.response.data)) {
      body = error.response.data
    }

    if (body && body.code !== 0) {
      const message = getErrorMessage(body.code, body.message)
      return Promise.reject(new ApiError(body.code, message))
    }
  }

  return Promise.reject(
    new ApiError(
      error.response?.status ?? -1,
      error.message ?? '网络请求失败',
    ),
  )
}

// ==================== ApiError ====================

/** 业务错误 */
export class ApiError extends Error {
  /** 业务错误码 */
  readonly code: number
  /** 是否为认证错误（token 过期/无效等） */
  readonly isAuthError: boolean

  constructor(code: number, message: string) {
    super(message)
    this.name = 'ApiError'
    this.code = code
    this.isAuthError = isAuthErrorCode(code)
  }
}

// ==================== localStorage 工具 ====================

const ACCESS_TOKEN_KEY = 'access_token'
const REFRESH_TOKEN_KEY = 'refresh_token'

function getAccessToken(): string | null {
  return localStorage.getItem(ACCESS_TOKEN_KEY)
}

function getRefreshToken(): string | null {
  return localStorage.getItem(REFRESH_TOKEN_KEY)
}

function setTokens(accessToken: string, refreshToken: string): void {
  localStorage.setItem(ACCESS_TOKEN_KEY, accessToken)
  localStorage.setItem(REFRESH_TOKEN_KEY, refreshToken)
}

function clearTokens(): void {
  localStorage.removeItem(ACCESS_TOKEN_KEY)
  localStorage.removeItem(REFRESH_TOKEN_KEY)
}

// ==================== Token 刷新队列 ====================

let isRefreshing = false
let refreshSubscribers: Array<(token: string) => void> = []

function subscribeTokenRefresh(cb: (token: string) => void): void {
  refreshSubscribers.push(cb)
}

function onTokenRefreshed(token: string): void {
  for (const cb of refreshSubscribers) {
    cb(token)
  }
  refreshSubscribers = []
}

function onRefreshFailed(): void {
  refreshSubscribers = []
}

// ==================== 基础 Axios 实例 ====================

const baseConfig = {
  baseURL: '/api',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
}

/** 主 Axios 实例（Bearer Token 认证） */
export const apiClient: AxiosInstance = axios.create(baseConfig)

// 请求拦截器：注入 Bearer Token
apiClient.interceptors.request.use((config: InternalAxiosRequestConfig) => {
  const token = getAccessToken()
  if (token) {
    config.headers.Authorization = `Bearer ${token}`
  }
  return config
})

// 响应拦截器：解包 ApiResponse + 401 刷新
apiClient.interceptors.response.use(
  (response) => unwrapApiResponse(response.data) as typeof response.data,
  async (error: AxiosError<ApiResponse<unknown> | Blob>) => {
    const originalRequest = error.config as InternalAxiosRequestConfig & { _retry?: boolean }

    // 非 401 或已重试过，直接抛出
    if (error.response?.status !== 401 || originalRequest._retry) {
      return rejectApiError(error)
    }

    // 尝试刷新 token
    if (isRefreshing) {
      // 排队等待刷新完成
      return new Promise((resolve) => {
        subscribeTokenRefresh((token: string) => {
          originalRequest.headers.Authorization = `Bearer ${token}`
          originalRequest._retry = true
          resolve(apiClient(originalRequest))
        })
      })
    }

    isRefreshing = true
    originalRequest._retry = true

    try {
      const refreshToken = getRefreshToken()
      if (!refreshToken) {
        throw new Error('No refresh token')
      }

      // 直接用 axios 发起刷新，避免走拦截器
      const { data } = await axios.post<ApiResponse<{ access_token: string; refresh_token: string }>>(
        '/api/auth/refresh',
        { refresh_token: refreshToken },
        { headers: { 'Content-Type': 'application/json' } },
      )

      if (data.code !== 0 || !data.data) {
        throw new Error('Refresh failed')
      }

      const { access_token, refresh_token: new_refresh_token } = data.data
      setTokens(access_token, new_refresh_token)

      onTokenRefreshed(access_token)

      originalRequest.headers.Authorization = `Bearer ${access_token}`
      return apiClient(originalRequest)
    } catch {
      onRefreshFailed()
      clearTokens()
      window.location.href = '/login'
      return Promise.reject(new ApiError(40108, '登录已过期，请重新登录'))
    } finally {
      isRefreshing = false
    }
  },
)

// ==================== Share Token 客户端 ====================

/**
 * 创建使用 X-Share-Token 认证的 Axios 实例。
 * 用于分享浏览和下载，不会附带 Bearer Token。
 */
export function createShareClient(shareToken: string): AxiosInstance {
  const client = axios.create({
    ...baseConfig,
    headers: {
      ...baseConfig.headers,
      'X-Share-Token': shareToken,
    },
  })

  // 响应拦截器：解包 ApiResponse（不处理 401 刷新），保留 Blob 下载响应
  client.interceptors.response.use(
    (response) => unwrapApiResponse(response.data) as typeof response.data,
    (error: AxiosError<ApiResponse<unknown> | Blob>) => rejectApiError(error),
  )

  return client
}

// ==================== 无认证客户端（公开接口） ====================

/** 无需认证的 Axios 实例 */
export const publicClient: AxiosInstance = axios.create(baseConfig)

publicClient.interceptors.response.use(
  (response) => unwrapApiResponse(response.data) as typeof response.data,
  (error: AxiosError<ApiResponse<unknown> | Blob>) => rejectApiError(error),
)

// ==================== Token 工具导出 ====================

export { getAccessToken, getRefreshToken, setTokens, clearTokens }
