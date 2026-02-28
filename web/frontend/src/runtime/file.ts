import { getElectronApi } from './platform'

type PickFileOptions = {
  accept?: string
}

export async function pickSingleFile(options: PickFileOptions = {}): Promise<File | null> {
  const electronFile = getElectronApi()?.file
  if (electronFile?.pickSingleFile) {
    return electronFile.pickSingleFile(options.accept)
  }

  return new Promise((resolve) => {
    const input = document.createElement('input')
    input.type = 'file'
    if (options.accept) input.accept = options.accept
    input.onchange = () => {
      const file = input.files?.[0] ?? null
      resolve(file)
    }
    input.click()
  })
}
