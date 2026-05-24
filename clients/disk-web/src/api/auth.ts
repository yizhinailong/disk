import { publicClient, apiClient } from './client'
import type {
  RegisterRequest,
  RegisterResponse,
  LoginRequest,
  LoginResponse,
  RefreshTokenRequest,
  RefreshTokenResponse,
} from '@/types'

export function register(data: RegisterRequest): Promise<RegisterResponse> {
  return publicClient.post('/auth/register', data) as Promise<RegisterResponse>
}

export function login(data: LoginRequest): Promise<LoginResponse> {
  return publicClient.post('/auth/login', data) as Promise<LoginResponse>
}

export function refreshToken(data: RefreshTokenRequest): Promise<RefreshTokenResponse> {
  return publicClient.post('/auth/refresh', data) as Promise<RefreshTokenResponse>
}

export function logout(): Promise<void> {
  return apiClient.post('/auth/logout') as Promise<void>
}
