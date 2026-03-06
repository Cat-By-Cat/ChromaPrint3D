import type {
  HealthResponse,
  ColorDBInfo,
  DefaultConfig,
  ConvertRasterParams,
  ConvertVectorParams,
  TaskStatus,
  GenerateBoardRequest,
  GenerateBoardResponse,
  Generate8ColorBoardRequest,
  MattingMethodInfo,
  MattingTaskStatus,
  MattingPostprocessParams,
  VectorizeParams,
  VectorizeTaskStatus,
} from './types'
import { buildApiUrl } from './runtime/env'
import { fetchWithSession } from './runtime/protectedRequest'

type ApiErrorPayload =
  | {
      code?: string
      message?: string
    }
  | string

type ApiEnvelope<T> = {
  ok: boolean
  data?: T
  error?: ApiErrorPayload
}

function parseErrorMessage(payload: ApiErrorPayload | undefined, fallback: string): string {
  if (!payload) return fallback
  if (typeof payload === 'string') return payload
  return payload.message ?? payload.code ?? fallback
}

async function request<T>(url: string, init?: RequestInit): Promise<T> {
  const res = await fetchWithSession(url, init)

  let envelope: ApiEnvelope<T> | null = null
  try {
    envelope = (await res.json()) as ApiEnvelope<T>
  } catch {
    // ignore parse errors, fallback to HTTP status
  }

  if (!res.ok) {
    throw new Error(parseErrorMessage(envelope?.error, `HTTP ${res.status}`))
  }
  if (!envelope?.ok) {
    throw new Error(parseErrorMessage(envelope?.error, 'Request failed'))
  }
  return envelope.data as T
}

// ---- Health ----

export async function fetchHealth(): Promise<HealthResponse> {
  return request<HealthResponse>('/api/v1/health')
}

// ---- ColorDBs ----

export async function fetchColorDBs(): Promise<ColorDBInfo[]> {
  const data = await request<{ databases: ColorDBInfo[] }>('/api/v1/databases')
  return data.databases
}

// ---- Default config ----

export async function fetchDefaults(): Promise<DefaultConfig> {
  return request<DefaultConfig>('/api/v1/convert/defaults')
}

// ---- Submit raster conversion ----

export async function submitConvertRaster(
  file: File,
  params: ConvertRasterParams,
): Promise<{ task_id: string }> {
  const formData = new FormData()
  formData.append('image', file)
  formData.append('params', JSON.stringify(params))
  return request<{ task_id: string }>('/api/v1/convert/raster', {
    method: 'POST',
    body: formData,
  })
}

// ---- Submit vector (SVG) conversion ----

export async function submitConvertVector(
  file: File,
  params: ConvertVectorParams,
): Promise<{ task_id: string }> {
  const formData = new FormData()
  formData.append('svg', file)
  formData.append('params', JSON.stringify(params))
  return request<{ task_id: string }>('/api/v1/convert/vector', {
    method: 'POST',
    body: formData,
  })
}

// ---- Task status ----

export async function fetchTaskStatus(id: string): Promise<TaskStatus> {
  return request<TaskStatus>(`/api/v1/tasks/${id}`)
}

// ---- Task list ----

export async function fetchTasks(): Promise<TaskStatus[]> {
  const data = await request<{ tasks: TaskStatus[] }>('/api/v1/tasks')
  return data.tasks
}

// ---- Delete task ----

export async function deleteTask(id: string): Promise<void> {
  await request<{ deleted: boolean }>(`/api/v1/tasks/${id}`, { method: 'DELETE' })
}

// ---- Protected binary resource URLs ----
// Use with session-aware helpers (fetchWithSession/downloadFromUrl), avoid raw DOM direct linking.

function taskArtifactPath(id: string, artifact: string): string {
  return `/api/v1/tasks/${id}/artifacts/${artifact}`
}

export function getPreviewPath(id: string): string {
  return taskArtifactPath(id, 'preview')
}

export function getPreviewUrl(id: string): string {
  return buildApiUrl(getPreviewPath(id))
}

export function getSourceMaskPath(id: string): string {
  return taskArtifactPath(id, 'source-mask')
}

export function getSourceMaskUrl(id: string): string {
  return buildApiUrl(getSourceMaskPath(id))
}

export function getResultPath(id: string): string {
  return taskArtifactPath(id, 'result')
}

export function getResultUrl(id: string): string {
  return buildApiUrl(getResultPath(id))
}

export function getLayerPreviewPath(id: string, artifactKey: string): string {
  return taskArtifactPath(id, encodeURIComponent(artifactKey))
}

export function getLayerPreviewUrl(id: string, artifactKey: string): string {
  return buildApiUrl(getLayerPreviewPath(id, artifactKey))
}

// ---- Calibration ----

export async function generateBoard(payload: GenerateBoardRequest): Promise<GenerateBoardResponse> {
  return request<GenerateBoardResponse>('/api/v1/calibration/boards', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })
}

export async function generate8ColorBoard(
  payload: Generate8ColorBoardRequest,
): Promise<GenerateBoardResponse> {
  return request<GenerateBoardResponse>('/api/v1/calibration/boards/8color', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })
}

export function getBoardModelPath(boardId: string): string {
  return `/api/v1/calibration/boards/${boardId}/model`
}

export function getBoardModelUrl(boardId: string): string {
  return buildApiUrl(getBoardModelPath(boardId))
}

export function getBoardMetaPath(boardId: string): string {
  return `/api/v1/calibration/boards/${boardId}/meta`
}

export function getBoardMetaUrl(boardId: string): string {
  return buildApiUrl(getBoardMetaPath(boardId))
}

export async function buildColorDB(image: File, meta: File, name: string): Promise<ColorDBInfo> {
  const formData = new FormData()
  formData.append('image', image)
  formData.append('meta', meta)
  formData.append('name', name)
  return request<ColorDBInfo>('/api/v1/calibration/colordb', {
    method: 'POST',
    body: formData,
  })
}

// ---- Session ColorDBs ----

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

export function getSessionColorDBDownloadUrl(name: string): string {
  return buildApiUrl(getSessionColorDBDownloadPath(name))
}

// ---- Matting ----

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

export function getMattingMaskPath(id: string): string {
  return taskArtifactPath(id, 'mask')
}

export function getMattingMaskUrl(id: string): string {
  return buildApiUrl(getMattingMaskPath(id))
}

export function getMattingForegroundPath(id: string): string {
  return taskArtifactPath(id, 'foreground')
}

export function getMattingForegroundUrl(id: string): string {
  return buildApiUrl(getMattingForegroundPath(id))
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

export function getMattingAlphaPath(id: string): string {
  return taskArtifactPath(id, 'alpha')
}

export function getMattingAlphaUrl(id: string): string {
  return buildApiUrl(getMattingAlphaPath(id))
}

export function getMattingProcessedForegroundPath(id: string): string {
  return taskArtifactPath(id, 'processed-foreground')
}

export function getMattingProcessedForegroundUrl(id: string): string {
  return buildApiUrl(getMattingProcessedForegroundPath(id))
}

export function getMattingProcessedMaskPath(id: string): string {
  return taskArtifactPath(id, 'processed-mask')
}

export function getMattingProcessedMaskUrl(id: string): string {
  return buildApiUrl(getMattingProcessedMaskPath(id))
}

export function getMattingOutlinePath(id: string): string {
  return taskArtifactPath(id, 'outline')
}

export function getMattingOutlineUrl(id: string): string {
  return buildApiUrl(getMattingOutlinePath(id))
}

export async function deleteMattingTask(id: string): Promise<void> {
  await deleteTask(id)
}

// ---- Vectorize ----

export async function submitVectorize(
  file: File,
  params: VectorizeParams,
): Promise<{ task_id: string }> {
  const formData = new FormData()
  formData.append('image', file)
  formData.append('params', JSON.stringify(params))
  return request<{ task_id: string }>('/api/v1/vectorize/tasks', {
    method: 'POST',
    body: formData,
  })
}

export async function fetchVectorizeDefaults(): Promise<VectorizeParams> {
  return request<VectorizeParams>('/api/v1/vectorize/defaults')
}

export async function fetchVectorizeTaskStatus(taskId: string): Promise<VectorizeTaskStatus> {
  const raw = await request<Record<string, unknown>>(`/api/v1/tasks/${taskId}`)
  return {
    id: String(raw.id ?? taskId),
    status: String(raw.status ?? 'pending') as VectorizeTaskStatus['status'],
    error: (raw.error as string | null | undefined) ?? null,
    width: Number(raw.width ?? 0),
    height: Number(raw.height ?? 0),
    num_shapes: Number(raw.num_shapes ?? 0),
    svg_size: Number(raw.svg_size ?? 0),
    timing: (raw.timing as VectorizeTaskStatus['timing']) ?? null,
  }
}

export function getVectorizeSvgPath(id: string): string {
  return taskArtifactPath(id, 'svg')
}

export function getVectorizeSvgUrl(id: string): string {
  return buildApiUrl(getVectorizeSvgPath(id))
}

export async function deleteVectorizeTask(id: string): Promise<void> {
  await deleteTask(id)
}

// ---- Session ColorDBs ----

export async function uploadColorDB(file: File, name?: string): Promise<ColorDBInfo> {
  const formData = new FormData()
  formData.append('file', file)
  if (name) formData.append('name', name)
  return request<ColorDBInfo>('/api/v1/session/databases/upload', {
    method: 'POST',
    body: formData,
  })
}
