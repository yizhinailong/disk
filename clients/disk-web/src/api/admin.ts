import { apiClient } from './client'
import type {
  AdminListUsersQuery,
  AdminUserListResponse,
  AdminUserDetailResponse,
  AdminChangeStatusRequest,
  AdminChangeStatusResponse,
  AdminChangeRoleRequest,
  AdminChangeRoleResponse,
  AdminChangeAvailableSpaceRequest,
  AdminChangeAvailableSpaceResponse,
  AdminDeleteUserResponse,
  AdminStorageStatsResponse,
  AdminShareListResponse,
  AdminListSharesQuery,
  AdminShareDetailResponse,
  AdminDeleteShareResponse,
  AdminStatsOverviewResponse,
  AdminStatsSystemResponse,
  AdminLogListQuery,
  AdminLogListResponse,
  LogsQuery,
  LogsResponse,
} from '@/types'

export function listUsers(params: AdminListUsersQuery): Promise<AdminUserListResponse> {
  return apiClient.get('/admin/users', { params }) as Promise<AdminUserListResponse>
}

export function getUserDetail(userId: number): Promise<AdminUserDetailResponse> {
  return apiClient.get(`/admin/users/${userId}`) as Promise<AdminUserDetailResponse>
}

export function changeUserStatus(
  userId: number,
  data: AdminChangeStatusRequest,
): Promise<AdminChangeStatusResponse> {
  return apiClient.put(`/admin/users/${userId}/status`, data) as Promise<AdminChangeStatusResponse>
}

export function changeUserRole(
  userId: number,
  data: AdminChangeRoleRequest,
): Promise<AdminChangeRoleResponse> {
  return apiClient.put(`/admin/users/${userId}/role`, data) as Promise<AdminChangeRoleResponse>
}

export function changeUserAvailableSpace(
  userId: number,
  data: AdminChangeAvailableSpaceRequest,
): Promise<AdminChangeAvailableSpaceResponse> {
  return apiClient.put(`/admin/users/${userId}/available-space`, data) as Promise<AdminChangeAvailableSpaceResponse>
}

export function deleteUser(userId: number): Promise<AdminDeleteUserResponse> {
  return apiClient.delete(`/admin/users/${userId}`) as Promise<AdminDeleteUserResponse>
}

export function getStorageStats(): Promise<AdminStorageStatsResponse> {
  return apiClient.get('/admin/storage/stats') as Promise<AdminStorageStatsResponse>
}

export function listShares(params: AdminListSharesQuery): Promise<AdminShareListResponse> {
  return apiClient.get('/admin/shares', { params }) as Promise<AdminShareListResponse>
}

export function getShareDetail(shareId: string): Promise<AdminShareDetailResponse> {
  return apiClient.get(`/admin/shares/${encodeURIComponent(shareId)}`) as Promise<AdminShareDetailResponse>
}

export function deleteShare(shareId: string): Promise<AdminDeleteShareResponse> {
  return apiClient.delete(`/admin/shares/${encodeURIComponent(shareId)}`) as Promise<AdminDeleteShareResponse>
}

export function getStatsOverview(): Promise<AdminStatsOverviewResponse> {
  return apiClient.get('/admin/stats/overview') as Promise<AdminStatsOverviewResponse>
}

export function getStatsSystem(): Promise<AdminStatsSystemResponse> {
  return apiClient.get('/admin/stats/system') as Promise<AdminStatsSystemResponse>
}

export function getAdminLogs(params: AdminLogListQuery): Promise<AdminLogListResponse> {
  return apiClient.get('/admin/logs', { params }) as Promise<AdminLogListResponse>
}

export function listLogs(params: LogsQuery): Promise<LogsResponse> {
  return apiClient.get('/logs', { params }) as Promise<LogsResponse>
}
