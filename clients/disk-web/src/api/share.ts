import { publicClient, createShareClient, apiClient } from './client'
import type {
  CreateShareRequest,
  CreateShareResponse,
  ShareListQuery,
  ShareListResponse,
  ShareDetailResponse,
  UpdateShareRequest,
  UpdateShareResponse,
  CancelShareRequest,
  CancelShareResponse,
  AccessShareRequest,
  AccessShareResponse,
  BrowseShareQuery,
  BrowseShareResponse,
  SaveShareRequest,
  SaveShareResponse,
} from '@/types'

export function createShare(data: CreateShareRequest): Promise<CreateShareResponse> {
  return apiClient.post('/share', data) as Promise<CreateShareResponse>
}

export function listShares(params: ShareListQuery): Promise<ShareListResponse> {
  return apiClient.get('/share', { params }) as Promise<ShareListResponse>
}

export function getShareDetail(shareId: string): Promise<ShareDetailResponse> {
  return apiClient.get(`/share/${encodeURIComponent(shareId)}`) as Promise<ShareDetailResponse>
}

export function updateShare(shareId: string, data: UpdateShareRequest): Promise<UpdateShareResponse> {
  return apiClient.put(`/share/${encodeURIComponent(shareId)}`, data) as Promise<UpdateShareResponse>
}

export function cancelShares(data: CancelShareRequest): Promise<CancelShareResponse> {
  return apiClient.delete('/share', { data }) as Promise<CancelShareResponse>
}

export function accessShare(shareId: string, data: AccessShareRequest): Promise<AccessShareResponse> {
  return publicClient.post(
    `/share/access/${encodeURIComponent(shareId)}`,
    data,
  ) as Promise<AccessShareResponse>
}

export function browseShare(
  shareId: string,
  params: BrowseShareQuery,
  shareToken: string,
): Promise<BrowseShareResponse> {
  const client = createShareClient(shareToken)
  return client.get(`/share/browse/${encodeURIComponent(shareId)}`, { params }) as Promise<BrowseShareResponse>
}

export function downloadShareFile(
  shareId: string,
  fileId: number,
  shareToken: string,
): Promise<Blob> {
  const client = createShareClient(shareToken)
  return client.get(`/share/download/${encodeURIComponent(shareId)}/${fileId}`, {
    responseType: 'blob',
    timeout: 300000,
  }) as Promise<Blob>
}

export function saveShareItems(
  shareId: string,
  data: SaveShareRequest,
  shareToken: string,
): Promise<SaveShareResponse> {
  return apiClient.post(`/share/save/${encodeURIComponent(shareId)}`, data, {
    headers: { 'X-Share-Token': shareToken },
  }) as Promise<SaveShareResponse>
}
