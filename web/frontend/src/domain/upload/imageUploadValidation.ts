import type { ImageDimensions, InputType } from '../../types'

export const BACKEND_MAX_UPLOAD_MB = 50
export const BACKEND_MAX_UPLOAD_BYTES = BACKEND_MAX_UPLOAD_MB * 1024 * 1024
export const BACKEND_MAX_IMAGE_PIXELS = 4096 * 4096

const RASTER_EXTENSIONS = new Set(['jpg', 'jpeg', 'png', 'bmp', 'tif', 'tiff'])
const RASTER_MIME_TYPES = new Set([
  'image/jpeg',
  'image/png',
  'image/bmp',
  'image/x-ms-bmp',
  'image/tiff',
])
const SVG_EXTENSION = 'svg'
const SVG_MIME_TYPE = 'image/svg+xml'

export const RASTER_IMAGE_ACCEPT =
  '.jpg,.jpeg,.png,.bmp,.tif,.tiff,image/jpeg,image/png,image/bmp,image/x-ms-bmp,image/tiff'
export const CONVERT_IMAGE_ACCEPT = `${RASTER_IMAGE_ACCEPT},.svg,image/svg+xml`
export const RASTER_IMAGE_FORMATS_TEXT = 'JPG / PNG / BMP / TIFF'
export const CONVERT_IMAGE_FORMATS_TEXT = `${RASTER_IMAGE_FORMATS_TEXT} / SVG`

export type UploadValidationScene = 'convert' | 'raster-tool'

export interface UploadValidationOptions {
  maxUploadBytes?: number
  maxPixels?: number
}

export type UploadValidationResult =
  | {
      ok: true
      inputType: InputType
      dimensions: ImageDimensions | null
    }
  | {
      ok: false
      message: string
    }

function getFileExtension(fileName: string): string {
  const dot = fileName.lastIndexOf('.')
  if (dot < 0 || dot === fileName.length - 1) return ''
  return fileName.slice(dot + 1).toLowerCase()
}

function isSvgFile(file: File, ext: string): boolean {
  return ext === SVG_EXTENSION || file.type.toLowerCase() === SVG_MIME_TYPE
}

function isRasterFile(file: File, ext: string): boolean {
  const mime = file.type.toLowerCase()
  return RASTER_EXTENSIONS.has(ext) || RASTER_MIME_TYPES.has(mime)
}

export function readRasterImageDimensions(file: File): Promise<ImageDimensions | null> {
  return new Promise((resolve) => {
    const url = URL.createObjectURL(file)
    const img = new Image()
    img.onload = () => {
      resolve({ width: img.naturalWidth, height: img.naturalHeight })
      URL.revokeObjectURL(url)
    }
    img.onerror = () => {
      resolve(null)
      URL.revokeObjectURL(url)
    }
    img.src = url
  })
}

function formatLimitPixels(maxPixels: number): string {
  return maxPixels.toLocaleString('zh-CN')
}

export async function validateImageUploadFile(
  file: File,
  scene: UploadValidationScene,
  options: UploadValidationOptions = {},
): Promise<UploadValidationResult> {
  const maxUploadBytes = options.maxUploadBytes ?? BACKEND_MAX_UPLOAD_BYTES
  const maxPixels = options.maxPixels ?? BACKEND_MAX_IMAGE_PIXELS
  const maxUploadMB = (maxUploadBytes / 1024 / 1024).toFixed(0)
  const ext = getFileExtension(file.name)

  if (file.size <= 0) {
    return { ok: false, message: '文件为空，请重新选择。' }
  }
  if (file.size > maxUploadBytes) {
    return {
      ok: false,
      message: `文件大小超过后端上传限制（${maxUploadMB}MB）。`,
    }
  }

  const inputType: InputType = isSvgFile(file, ext) ? 'vector' : 'raster'

  if (scene === 'raster-tool' && inputType === 'vector') {
    return {
      ok: false,
      message: `当前页面仅支持位图格式（${RASTER_IMAGE_FORMATS_TEXT}）。`,
    }
  }

  if (inputType === 'vector') {
    if (ext !== SVG_EXTENSION && file.type.toLowerCase() !== SVG_MIME_TYPE && file.type !== '') {
      return {
        ok: false,
        message: 'SVG 文件类型不受支持，请重新导出标准 SVG 后重试。',
      }
    }
    return { ok: true, inputType, dimensions: null }
  }

  if (!isRasterFile(file, ext)) {
    return {
      ok: false,
      message: `不支持该图片格式，请使用 ${RASTER_IMAGE_FORMATS_TEXT}。`,
    }
  }

  const dimensions = await readRasterImageDimensions(file)
  if (!dimensions || dimensions.width <= 0 || dimensions.height <= 0) {
    return {
      ok: false,
      message: '图片无法解码，请确认文件未损坏且为受支持格式。',
    }
  }

  const pixels = dimensions.width * dimensions.height
  if (pixels > maxPixels) {
    return {
      ok: false,
      message: `图片像素超过后端上限（${formatLimitPixels(maxPixels)} 像素）。`,
    }
  }

  return { ok: true, inputType, dimensions }
}
