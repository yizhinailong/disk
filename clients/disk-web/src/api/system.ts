import { publicClient, apiClient } from './client'
import type { HealthResponse, SystemInfoResponse, LogsQuery, LogsResponse } from '@/types'

export function healthCheck(): Promise<HealthResponse> {
  return publicClient.get('/health') as Promise<HealthResponse>
}

export function getSystemInfo(): Promise<SystemInfoResponse> {
  return apiClient.get('/system/info') as Promise<SystemInfoResponse>
}

export function getLogs(params: LogsQuery): Promise<LogsResponse> {
  return apiClient.get('/logs', { params }) as Promise<LogsResponse>
}
