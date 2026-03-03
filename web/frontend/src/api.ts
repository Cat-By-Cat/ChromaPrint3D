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

const SESSION_HEADER = 'X-ChromaPrint3D-Session'
const SESSION_QUERY_PARAM = 'session'
let sessionToken: string | null = null

function parseErrorMessage(payload: ApiErrorPayload | undefined, fallback: string): string {
  if (!payload) return fallback
  if (typeof payload === 'string') return payload
  return payload.message ?? payload.code ?? fallback
}

function mergeRequestHeaders(headers?: HeadersInit): Headers {
  const merged = new Headers(headers)
  if (sessionToken && !merged.has(SESSION_HEADER)) {
    merged.set(SESSION_HEADER, sessionToken)
  }
  return merged
}

function updateSessionTokenFromResponse(response: Response): void {
  const token = response.headers.get(SESSION_HEADER)?.trim()
  if (token) {
    sessionToken = token
  }
}

function appendSessionQuery(url: string): string {
  if (!sessionToken) return url
  const parts = url.split('#', 2)
  const base = parts[0] ?? url
  const fragment = parts[1]
  const separator = base.includes('?') ? '&' : '?'
  const withSession = `${base}${separator}${SESSION_QUERY_PARAM}=${encodeURIComponent(sessionToken)}`
  return fragment ? `${withSession}#${fragment}` : withSession
}

async function request<T>(url: string, init?: RequestInit): Promise<T> {
  const merged: RequestInit = {
    credentials: 'include',
    ...init,
    headers: mergeRequestHeaders(init?.headers),
  }
  const res = await fetch(buildApiUrl(url), merged)
  updateSessionTokenFromResponse(res)

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

// ---- Binary resource URLs (for <img src> or download links) ----

export function getPreviewUrl(id: string): string {
  return appendSessionQuery(buildApiUrl(`/api/v1/tasks/${id}/artifacts/preview`))
}

export function getSourceMaskUrl(id: string): string {
  return appendSessionQuery(buildApiUrl(`/api/v1/tasks/${id}/artifacts/source-mask`))
}

export function getResultUrl(id: string): string {
  return appendSessionQuery(buildApiUrl(`/api/v1/tasks/${id}/artifacts/result`))
}

export function getLayerPreviewUrl(id: string, artifactKey: string): string {
  return appendSessionQuery(
    buildApiUrl(`/api/v1/tasks/${id}/artifacts/${encodeURIComponent(artifactKey)}`),
  )
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

export function getBoardModelUrl(boardId: string): string {
  return buildApiUrl(`/api/v1/calibration/boards/${boardId}/model`)
}

export function getBoardMetaUrl(boardId: string): string {
  return buildApiUrl(`/api/v1/calibration/boards/${boardId}/meta`)
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

export function getSessionColorDBDownloadUrl(name: string): string {
  return appendSessionQuery(buildApiUrl(`/api/v1/session/databases/${encodeURIComponent(name)}/download`))
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

export function getMattingMaskUrl(id: string): string {
  return appendSessionQuery(buildApiUrl(`/api/v1/tasks/${id}/artifacts/mask`))
}

export function getMattingForegroundUrl(id: string): string {
  return appendSessionQuery(buildApiUrl(`/api/v1/tasks/${id}/artifacts/foreground`))
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

export function getMattingAlphaUrl(id: string): string {
  return appendSessionQuery(buildApiUrl(`/api/v1/tasks/${id}/artifacts/alpha`))
}

export function getMattingProcessedForegroundUrl(id: string): string {
  return appendSessionQuery(buildApiUrl(`/api/v1/tasks/${id}/artifacts/processed-foreground`))
}

export function getMattingProcessedMaskUrl(id: string): string {
  return appendSessionQuery(buildApiUrl(`/api/v1/tasks/${id}/artifacts/processed-mask`))
}

export function getMattingOutlineUrl(id: string): string {
  return appendSessionQuery(buildApiUrl(`/api/v1/tasks/${id}/artifacts/outline`))
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

export function getVectorizeSvgUrl(id: string): string {
  return appendSessionQuery(buildApiUrl(`/api/v1/tasks/${id}/artifacts/svg`))
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
