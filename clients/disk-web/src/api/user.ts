import { apiClient } from './client'
import type {
  UserProfileResponse,
  UpdateProfileRequest,
  UpdateProfileResponse,
  ChangePasswordRequest,
  StorageResponse,
} from '@/types'

export function getProfile(): Promise<UserProfileResponse> {
  return apiClient.get('/user/profile') as Promise<UserProfileResponse>
}

export function updateProfile(data: UpdateProfileRequest): Promise<UpdateProfileResponse> {
  return apiClient.patch('/user/profile', data) as Promise<UpdateProfileResponse>
}

export function changePassword(data: ChangePasswordRequest): Promise<void> {
  return apiClient.put('/user/password', data) as Promise<void>
}

export function getStorageStats(): Promise<StorageResponse> {
  return apiClient.get('/user/storage') as Promise<StorageResponse>
}
