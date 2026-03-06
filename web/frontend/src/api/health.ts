import type { HealthResponse } from '../types'
import { request } from './base'

export async function fetchHealth(): Promise<HealthResponse> {
  return request<HealthResponse>('/api/v1/health')
}
