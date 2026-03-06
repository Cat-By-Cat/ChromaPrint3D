import type { MattingMethodInfo, MattingPostprocessParams, MattingTaskStatus } from '../types'
import { request } from './base'
import { deleteTask } from './tasks'

export async function fetchMattingMethods(): Promise<MattingMethodInfo[]> {
  const data = await request<{ methods: MattingMethodInfo[] }>('/api/v1/matting/methods')
  return data.methods
}

export async function submitMatting(file: File, method: string): Promise<{ task_id: string }> {
  const formData = new FormData()
  formData.append('image', file)
  formData.append('method', method)
  return request<{ task_id: string }>('/api/v1/matting/tasks', {
    method: 'POST',
    body: formData,
  })
}

export async function fetchMattingTaskStatus(taskId: string): Promise<MattingTaskStatus> {
  const raw = await request<Record<string, unknown>>(`/api/v1/tasks/${taskId}`)
  return {
    id: String(raw.id ?? taskId),
    status: String(raw.status ?? 'pending') as MattingTaskStatus['status'],
    method: String(raw.method ?? 'opencv'),
    error: (raw.error as string | null | undefined) ?? null,
    width: Number(raw.width ?? 0),
    height: Number(raw.height ?? 0),
    has_alpha: Boolean(raw.has_alpha),
    timing: (raw.timing as MattingTaskStatus['timing']) ?? null,
  }
}

function mattingArtifactPath(id: string, artifact: string): string {
  return `/api/v1/tasks/${id}/artifacts/${artifact}`
}

export function getMattingMaskPath(id: string): string {
  return mattingArtifactPath(id, 'mask')
}

export function getMattingForegroundPath(id: string): string {
  return mattingArtifactPath(id, 'foreground')
}

export function getMattingAlphaPath(id: string): string {
  return mattingArtifactPath(id, 'alpha')
}

export function getMattingProcessedForegroundPath(id: string): string {
  return mattingArtifactPath(id, 'processed-foreground')
}

export function getMattingProcessedMaskPath(id: string): string {
  return mattingArtifactPath(id, 'processed-mask')
}

export function getMattingOutlinePath(id: string): string {
  return mattingArtifactPath(id, 'outline')
}

export async function postprocessMatting(
  taskId: string,
  params: MattingPostprocessParams,
): Promise<{ artifacts: string[] }> {
  return request<{ artifacts: string[] }>(`/api/v1/matting/tasks/${taskId}/postprocess`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  })
}

export async function deleteMattingTask(id: string): Promise<void> {
  await deleteTask(id)
}
