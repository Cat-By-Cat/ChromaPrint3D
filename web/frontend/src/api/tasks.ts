import type { TaskStatus } from '../types'
import { request } from './base'

export async function fetchTaskStatus(id: string): Promise<TaskStatus> {
  return request<TaskStatus>(`/api/v1/tasks/${id}`)
}

export async function fetchTasks(): Promise<TaskStatus[]> {
  const data = await request<{ tasks: TaskStatus[] }>('/api/v1/tasks')
  return data.tasks
}

export async function deleteTask(id: string): Promise<void> {
  await request<{ deleted: boolean }>(`/api/v1/tasks/${id}`, { method: 'DELETE' })
}

function taskArtifactPath(id: string, artifact: string): string {
  return `/api/v1/tasks/${id}/artifacts/${artifact}`
}

export function getPreviewPath(id: string): string {
  return taskArtifactPath(id, 'preview')
}

export function getSourceMaskPath(id: string): string {
  return taskArtifactPath(id, 'source-mask')
}

export function getResultPath(id: string): string {
  return taskArtifactPath(id, 'result')
}

export function getLayerPreviewPath(id: string, artifactKey: string): string {
  return taskArtifactPath(id, encodeURIComponent(artifactKey))
}
