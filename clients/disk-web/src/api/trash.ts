import { apiClient } from './client'
import type {
  TrashListQuery,
  TrashListResponse,
  TrashRestoreRequest,
  TrashRestoreResponse,
  TrashDeleteRequest,
  TrashDeleteResponse,
  TrashDeleteAllResponse,
} from '@/types'

export function listTrash(params: TrashListQuery): Promise<TrashListResponse> {
  return apiClient.get('/trash', { params }) as Promise<TrashListResponse>
}

export function restoreTrash(data: TrashRestoreRequest): Promise<TrashRestoreResponse> {
  return apiClient.post('/trash/restore', data) as Promise<TrashRestoreResponse>
}

export function deleteTrash(data: TrashDeleteRequest): Promise<TrashDeleteResponse> {
  return apiClient.delete('/trash', { data }) as Promise<TrashDeleteResponse>
}

export function deleteAllTrash(): Promise<TrashDeleteAllResponse> {
  return apiClient.delete('/trash/all') as Promise<TrashDeleteAllResponse>
}
