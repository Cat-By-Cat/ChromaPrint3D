export function roundTo(value: number, fractionDigits: number): number {
  if (!Number.isFinite(value)) return value
  const digits = Math.max(0, Math.trunc(fractionDigits))
  const factor = 10 ** digits
  const rounded = Math.round((value + Number.EPSILON) * factor) / factor
  return Object.is(rounded, -0) ? 0 : rounded
}

export function formatFloat(value: number, fractionDigits: number): string {
  if (!Number.isFinite(value)) return '0'
  return roundTo(value, fractionDigits).toString()
}
