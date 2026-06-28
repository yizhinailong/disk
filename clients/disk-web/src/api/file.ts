import { apiClient } from './client'
import type {
  InitUploadRequest,
  InitUploadResponse,
  UploadChunkResponse,
  CompleteUploadRequest,
  CompleteUploadResponse,
  DownloadInfoResponse,
  FileListQuery,
  FileListResponse,
  FileDetailResponse,
  RenameRequest,
  RenameResponse,
  MoveRequest,
  MoveResponse,
  CopyRequest,
  CopyResponse,
  DeleteRequest,
  DeleteResponse,
  SearchQuery,
  SearchResponse,
} from '@/types'

export function initUpload(data: InitUploadRequest): Promise<InitUploadResponse> {
  return apiClient.post('/file/upload/init', data) as Promise<InitUploadResponse>
}

export function uploadChunk(
  uploadId: string,
  chunkIndex: number,
  chunkHash: string,
  chunkData: Blob,
): Promise<UploadChunkResponse> {
  return apiClient.post('/file/upload/chunk', chunkData, {
    params: {
      upload_id: uploadId,
      chunk_index: chunkIndex,
      chunk_hash: chunkHash,
    },
    headers: { 'Content-Type': 'application/octet-stream' },
    timeout: 120000,
  }) as Promise<UploadChunkResponse>
}

export function completeUpload(data: CompleteUploadRequest): Promise<CompleteUploadResponse> {
  return apiClient.post('/file/upload/complete', data) as Promise<CompleteUploadResponse>
}

export function cancelUpload(uploadId: string): Promise<void> {
  return apiClient.delete(`/file/upload/${encodeURIComponent(uploadId)}`) as Promise<void>
}

export function listFiles(params: FileListQuery): Promise<FileListResponse> {
  return apiClient.get('/file/list', { params }) as Promise<FileListResponse>
}

export function getFileDetail(fileId: number): Promise<FileDetailResponse> {
  return apiClient.get(`/file/${fileId}`) as Promise<FileDetailResponse>
}

export function getDownloadInfo(fileId: number): Promise<DownloadInfoResponse> {
  return apiClient.get(`/file/download/${fileId}/info`) as Promise<DownloadInfoResponse>
}

export function downloadFile(
  fileId: number,
  onProgress?: (progress: number) => void,
): Promise<Blob> {
  return apiClient.get(`/file/download/${fileId}`, {
    responseType: 'blob',
    timeout: 300000,
    onDownloadProgress: onProgress
      ? (event) => {
          if (event.total) {
            onProgress(Math.round((event.loaded / event.total) * 100))
          }
        }
      : undefined,
  }) as Promise<Blob>
}

export function renameFile(fileId: number, data: RenameRequest): Promise<RenameResponse> {
  return apiClient.put(`/file/${fileId}/rename`, data) as Promise<RenameResponse>
}

export function moveFiles(data: MoveRequest): Promise<MoveResponse> {
  return apiClient.put('/file/move', data) as Promise<MoveResponse>
}

export function copyFiles(data: CopyRequest): Promise<CopyResponse> {
  return apiClient.post('/file/copy', data) as Promise<CopyResponse>
}

export function deleteFiles(data: DeleteRequest): Promise<DeleteResponse> {
  return apiClient.delete('/file', { data }) as Promise<DeleteResponse>
}

export function searchFiles(params: SearchQuery): Promise<SearchResponse> {
  return apiClient.get('/file/search', { params }) as Promise<SearchResponse>
}
