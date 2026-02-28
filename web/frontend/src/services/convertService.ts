import { submitConvertRaster, submitConvertVector } from '../api'
import { buildRasterParams, buildVectorParams } from '../domain/params/convertParamBuilders'
import type { ConvertAnyParams, InputType } from '../types'

export async function submitConvertTask(
  file: File,
  params: ConvertAnyParams,
  inputType: InputType,
): Promise<{ task_id: string }> {
  if (inputType === 'vector') {
    return submitConvertVector(file, buildVectorParams(params))
  }
  return submitConvertRaster(file, buildRasterParams(params))
}
