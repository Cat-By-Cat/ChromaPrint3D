export const COLOR_DB_NAME_PATTERN = /^[a-zA-Z0-9_]+$/

export function sanitizeColorDBName(raw: string): string {
  return raw.trim().replace(/[^a-zA-Z0-9_]/g, '_')
}

export function isValidColorDBName(raw: string): boolean {
  const value = raw.trim()
  return value.length > 0 && COLOR_DB_NAME_PATTERN.test(value)
}
