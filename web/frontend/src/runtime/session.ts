const SESSION_HEADER = 'X-ChromaPrint3D-Session'

let sessionToken: string | null = null

export function getSessionHeaderName(): string {
  return SESSION_HEADER
}

export function setSessionToken(token: string | null): void {
  const normalized = token?.trim() ?? ''
  sessionToken = normalized.length > 0 ? normalized : null
}

export function applySessionHeader(headers: Headers): void {
  if (sessionToken && !headers.has(SESSION_HEADER)) {
    headers.set(SESSION_HEADER, sessionToken)
  }
}

export function mergeSessionHeader(headers?: HeadersInit): Headers {
  const merged = new Headers(headers)
  applySessionHeader(merged)
  return merged
}

export function syncSessionTokenFromResponse(response: Response): void {
  if (!response.headers.has(SESSION_HEADER)) return
  const token = response.headers.get(SESSION_HEADER)
  if (!token?.trim()) return
  setSessionToken(token)
}
