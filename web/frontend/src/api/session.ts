import type { ColorDBInfo } from '../types'
import { request } from './base'

export async function fetchSessionColorDBs(): Promise<ColorDBInfo[]> {
  const data = await request<{ databases: ColorDBInfo[] }>('/api/v1/session/databases')
  return data.databases
}

export async function deleteSessionColorDB(name: string): Promise<void> {
  await request<{ deleted: boolean }>(`/api/v1/session/databases/${encodeURIComponent(name)}`, {
    method: 'DELETE',
  })
}

export function getSessionColorDBDownloadPath(name: string): string {
  return `/api/v1/session/databases/${encodeURIComponent(name)}/download`
}

export async function uploadColorDB(file: File, name?: string): Promise<ColorDBInfo> {
  const formData = new FormData()
  formData.append('file', file)
  if (name) formData.append('name', name)
  return request<ColorDBInfo>('/api/v1/session/databases/upload', {
    method: 'POST',
    body: formData,
  })
}
