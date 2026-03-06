import { buildApiUrl } from './env'
import { mergeSessionHeader, syncSessionTokenFromResponse } from './session'

function isAbsoluteHttpUrl(value: string): boolean {
  return /^https?:\/\//i.test(value)
}

function resolveRequestUrl(pathOrUrl: string): string {
  return isAbsoluteHttpUrl(pathOrUrl) ? pathOrUrl : buildApiUrl(pathOrUrl)
}

export async function fetchWithSession(pathOrUrl: string, init?: RequestInit): Promise<Response> {
  const response = await fetch(resolveRequestUrl(pathOrUrl), {
    ...init,
    credentials: 'include',
    headers: mergeSessionHeader(init?.headers),
  })
  syncSessionTokenFromResponse(response)
  return response
}

export async function fetchBlobWithSession(pathOrUrl: string, init?: RequestInit): Promise<Blob> {
  const response = await fetchWithSession(pathOrUrl, init)
  if (!response.ok) throw new Error(`HTTP ${response.status}`)
  return response.blob()
}

export async function fetchTextWithSession(pathOrUrl: string, init?: RequestInit): Promise<string> {
  const response = await fetchWithSession(pathOrUrl, init)
  if (!response.ok) throw new Error(`HTTP ${response.status}`)
  return response.text()
}
