import { computed, ref } from 'vue'
import type { UploadFileInfo } from 'naive-ui'
import { uploadColorDB } from '../api/session'
import type { ColorDBInfo } from '../types'
import { isValidColorDBName, sanitizeColorDBName } from './colordbName'

type UseColorDBUploadFlowOptions = {
  onUploaded?: (result: ColorDBInfo) => void | Promise<void>
}

export function useColorDBUploadFlow(options: UseColorDBUploadFlowOptions = {}) {
  const uploadFile = ref<File | null>(null)
  const uploadName = ref('')
  const uploading = ref(false)
  const uploadError = ref<string | null>(null)

  const uploadNameError = computed(() => {
    const value = uploadName.value.trim()
    if (!value) return ''
    if (!isValidColorDBName(value)) return '名称仅支持字母、数字和下划线'
    return ''
  })

  const canUpload = computed(() => Boolean(uploadFile.value) && uploadNameError.value.length === 0)

  function handleUploadFileChange({ file }: { file: UploadFileInfo }) {
    uploadFile.value = file.file ?? null
    uploadError.value = null
    if (file.file && !uploadName.value.trim()) {
      uploadName.value = sanitizeColorDBName(file.file.name.replace(/\.json$/i, ''))
    }
    return false
  }

  async function handleUpload(): Promise<ColorDBInfo | null> {
    if (!canUpload.value || !uploadFile.value) return null

    uploading.value = true
    uploadError.value = null
    try {
      const nameToUse = uploadName.value.trim() || undefined
      const result = await uploadColorDB(uploadFile.value, nameToUse)
      if (options.onUploaded) {
        await options.onUploaded(result)
      }
      uploadFile.value = null
      uploadName.value = ''
      return result
    } catch (error: unknown) {
      uploadError.value = error instanceof Error ? error.message : String(error)
      return null
    } finally {
      uploading.value = false
    }
  }

  return {
    uploadFile,
    uploadName,
    uploading,
    uploadError,
    uploadNameError,
    canUpload,
    handleUploadFileChange,
    handleUpload,
  }
}
