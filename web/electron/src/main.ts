import { app, BrowserWindow, dialog, ipcMain, shell } from 'electron'
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import http from 'node:http'
import net from 'node:net'
import path from 'node:path'

const DEFAULT_RENDERER_URL = 'http://127.0.0.1:5173'
const DEFAULT_BACKEND_PORT = 18080
const BACKEND_HEALTH_TIMEOUT_MS = 45_000
const BACKEND_HEALTH_PATH = '/api/v1/health'
const SMOKE_EXIT_ENV = 'CHROMAPRINT3D_ELECTRON_SMOKE_TIMEOUT_MS'
const IPC_GET_API_BASE = 'electron:getApiBase'
const IPC_PICK_SINGLE_FILE = 'electron:pickSingleFile'
const PACKAGED_BACKEND_DIR = 'backend'
const PACKAGED_FRONTEND_DIR = 'frontend-dist'

type RendererTarget = {
  kind: 'url' | 'file'
  value: string
}

const MIME_BY_EXTENSION: Record<string, string> = {
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.png': 'image/png',
  '.gif': 'image/gif',
  '.webp': 'image/webp',
  '.bmp': 'image/bmp',
  '.svg': 'image/svg+xml',
  '.json': 'application/json',
  '.txt': 'text/plain',
}

let mainWindow: BrowserWindow | null = null
let backendProcess: ReturnType<typeof spawn> | null = null
let backendPort = DEFAULT_BACKEND_PORT
let isQuitting = false

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

function repoRoot(): string {
  return path.resolve(__dirname, '..', '..', '..')
}

function runtimeRoot(): string {
  if (app.isPackaged) return process.resourcesPath
  return repoRoot()
}

function parsePort(raw: string | undefined, fallback: number): number {
  if (!raw) return fallback
  const parsed = Number(raw)
  if (!Number.isInteger(parsed) || parsed <= 0 || parsed > 65535) return fallback
  return parsed
}

function parsePositiveInt(raw: string | undefined): number | null {
  if (!raw) return null
  const parsed = Number(raw)
  if (!Number.isInteger(parsed) || parsed <= 0) return null
  return parsed
}

function resolveRendererTarget(): RendererTarget {
  const fromEnv = process.env.CHROMAPRINT3D_RENDERER_URL?.trim()
  if (fromEnv) {
    return { kind: 'url', value: fromEnv }
  }
  if (!app.isPackaged) {
    return { kind: 'url', value: DEFAULT_RENDERER_URL }
  }
  return {
    kind: 'file',
    value: path.join(process.resourcesPath, PACKAGED_FRONTEND_DIR, 'index.html'),
  }
}

function backendBinaryPath(root: string): string {
  const fromEnv = process.env.CHROMAPRINT3D_BACKEND_PATH?.trim()
  if (fromEnv) return path.resolve(fromEnv)
  const binaryName = process.platform === 'win32' ? 'chromaprint3d_server.exe' : 'chromaprint3d_server'
  if (app.isPackaged) {
    if (process.platform !== 'win32') {
      const wrapperPath = path.join(root, PACKAGED_BACKEND_DIR, 'run_chromaprint3d_server.sh')
      if (fs.existsSync(wrapperPath)) {
        return wrapperPath
      }
    }
    return path.join(root, PACKAGED_BACKEND_DIR, binaryName)
  }
  return path.join(root, 'build', 'bin', binaryName)
}

function backendDataDir(root: string): string {
  return process.env.CHROMAPRINT3D_DATA_DIR?.trim() || path.join(root, 'data')
}

function backendModelPackPath(root: string): string {
  return (
    process.env.CHROMAPRINT3D_MODEL_PACK_PATH?.trim() ||
    path.join(root, 'data', 'model_pack', 'model_package.json')
  )
}

function buildMissingBackendMessage(binaryPath: string): string {
  if (app.isPackaged) {
    return [
      `Backend binary not found: ${binaryPath}`,
      '',
      `Expected packaged binary at "${PACKAGED_BACKEND_DIR}" under resources.`,
      'You can override path via CHROMAPRINT3D_BACKEND_PATH.',
    ].join('\n')
  }
  return [
    `Backend binary not found: ${binaryPath}`,
    '',
    'Build it first from repository root:',
    '  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release',
    '  cmake --build build --target chromaprint3d_server',
  ].join('\n')
}

function healthCheckStatus(baseUrl: string): Promise<number | null> {
  return new Promise((resolve) => {
    const request = http.get(`${baseUrl}${BACKEND_HEALTH_PATH}`, (response) => {
      response.resume()
      resolve(response.statusCode ?? null)
    })
    request.on('error', () => resolve(null))
    request.setTimeout(1500, () => {
      request.destroy()
      resolve(null)
    })
  })
}

async function waitBackendReady(baseUrl: string, timeoutMs: number): Promise<void> {
  const startedAt = Date.now()
  while (Date.now() - startedAt < timeoutMs) {
    const status = await healthCheckStatus(baseUrl)
    if (status === 200) return
    await sleep(500)
  }
  throw new Error(`Backend health check timeout after ${timeoutMs}ms: ${baseUrl}${BACKEND_HEALTH_PATH}`)
}

function isPortAvailable(port: number): Promise<boolean> {
  return new Promise((resolve) => {
    const server = net.createServer()
    server.once('error', () => {
      resolve(false)
    })
    server.once('listening', () => {
      server.close(() => resolve(true))
    })
    server.listen(port, '127.0.0.1')
  })
}

async function findAvailablePort(startPort: number): Promise<number> {
  const maxProbe = 20
  for (let offset = 0; offset < maxProbe; offset += 1) {
    const port = startPort + offset
    if (await isPortAvailable(port)) return port
  }
  throw new Error(`No available port found in range ${startPort}-${startPort + maxProbe - 1}`)
}

function buildDialogFilters(accept?: string): Electron.FileFilter[] {
  if (!accept) return []
  const extensions = accept
    .split(',')
    .map((token) => token.trim())
    .filter((token) => token.startsWith('.'))
    .map((ext) => ext.slice(1).toLowerCase())
    .filter(Boolean)
  if (extensions.length === 0) return []
  return [{ name: 'Allowed files', extensions }]
}

function guessMimeType(filePath: string): string {
  return MIME_BY_EXTENSION[path.extname(filePath).toLowerCase()] ?? 'application/octet-stream'
}

function isAllowedExternalUrl(url: string): boolean {
  try {
    const parsed = new URL(url)
    return parsed.protocol === 'https:' || parsed.protocol === 'http:' || parsed.protocol === 'mailto:'
  } catch {
    return false
  }
}

function isAllowedNavigation(target: RendererTarget, url: string): boolean {
  if (target.kind === 'file') {
    return url.startsWith('file://')
  }
  try {
    return new URL(url).origin === new URL(target.value).origin
  } catch {
    return false
  }
}

function applyWindowSecurity(window: BrowserWindow, target: RendererTarget): void {
  window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }))
  window.webContents.on('will-navigate', (event, targetUrl) => {
    if (!isAllowedNavigation(target, targetUrl)) {
      event.preventDefault()
    }
  })
}

function registerIpcHandlers(): void {
  ipcMain.on(IPC_GET_API_BASE, (event) => {
    event.returnValue = `http://127.0.0.1:${backendPort}`
  })

  ipcMain.handle('electron:openExternal', async (_event, url: string) => {
    if (!isAllowedExternalUrl(url)) {
      throw new Error('Unsupported external URL protocol')
    }
    await shell.openExternal(url)
  })

  ipcMain.handle(IPC_PICK_SINGLE_FILE, async (_event, accept?: string) => {
    const options = {
      properties: ['openFile'] as Array<'openFile'>,
      filters: buildDialogFilters(accept),
    }
    const result = mainWindow
      ? await dialog.showOpenDialog(mainWindow, options)
      : await dialog.showOpenDialog(options)
    if (result.canceled || result.filePaths.length === 0) return null
    const selectedPath = result.filePaths[0]
    const content = await fs.promises.readFile(selectedPath)
    return {
      name: path.basename(selectedPath),
      mimeType: guessMimeType(selectedPath),
      bytesBase64: content.toString('base64'),
    }
  })
}

async function startBackend(): Promise<void> {
  const root = runtimeRoot()
  const binaryPath = backendBinaryPath(root)
  const dataDir = backendDataDir(root)
  const modelPackPath = backendModelPackPath(root)

  if (!fs.existsSync(binaryPath)) {
    throw new Error(buildMissingBackendMessage(binaryPath))
  }
  if (!fs.existsSync(dataDir)) {
    throw new Error(`Backend data directory not found: ${dataDir}`)
  }
  if (!fs.existsSync(modelPackPath)) {
    throw new Error(`Backend model pack file not found: ${modelPackPath}`)
  }

  const preferredPort = parsePort(process.env.CHROMAPRINT3D_BACKEND_PORT, DEFAULT_BACKEND_PORT)
  backendPort = await findAvailablePort(preferredPort)

  const args = [
    '--host',
    '127.0.0.1',
    '--port',
    String(backendPort),
    '--data',
    dataDir,
    '--model-pack',
    modelPackPath,
  ]
  const child = spawn(binaryPath, args, {
    cwd: root,
    windowsHide: true,
    stdio: ['ignore', 'pipe', 'pipe'],
  })
  backendProcess = child

  child.stdout.on('data', (chunk) => {
    const text = String(chunk).trimEnd()
    if (text) console.log(`[backend] ${text}`)
  })
  child.stderr.on('data', (chunk) => {
    const text = String(chunk).trimEnd()
    if (text) console.error(`[backend] ${text}`)
  })
  child.once('exit', (code, signal) => {
    backendProcess = null
    if (!isQuitting) {
      dialog.showErrorBox(
        'ChromaPrint3D Backend Exited',
        `Backend process exited unexpectedly (code: ${code ?? 'null'}, signal: ${signal ?? 'null'}).`,
      )
      app.quit()
    }
  })
  child.once('error', (error) => {
    console.error('[backend] failed to spawn', error)
  })

  const baseUrl = `http://127.0.0.1:${backendPort}`
  await waitBackendReady(baseUrl, BACKEND_HEALTH_TIMEOUT_MS)
  console.log(`[backend] ready at ${baseUrl}`)
}

async function stopBackend(): Promise<void> {
  const child = backendProcess
  if (!child || child.killed) return

  await new Promise<void>((resolve) => {
    const timer = setTimeout(() => {
      if (!child.killed) child.kill('SIGKILL')
      resolve()
    }, 5000)

    child.once('exit', () => {
      clearTimeout(timer)
      resolve()
    })

    child.kill('SIGTERM')
  })
}

async function createMainWindow(): Promise<void> {
  const preloadPath = path.join(__dirname, 'preload.js')
  const apiBase = `http://127.0.0.1:${backendPort}`
  const target = resolveRendererTarget()
  const window = new BrowserWindow({
    width: 1360,
    height: 900,
    minWidth: 1100,
    minHeight: 760,
    webPreferences: {
      preload: preloadPath,
      contextIsolation: true,
      sandbox: true,
      nodeIntegration: false,
      additionalArguments: [`--chromaprint3d-api-base=${apiBase}`],
    },
  })
  mainWindow = window
  window.on('closed', () => {
    mainWindow = null
  })
  applyWindowSecurity(window, target)

  if (target.kind === 'url') {
    await window.loadURL(target.value)
    return
  }
  if (!fs.existsSync(target.value)) {
    throw new Error(`Renderer entry not found: ${target.value}`)
  }
  await window.loadFile(target.value)
}

async function bootstrap(): Promise<void> {
  registerIpcHandlers()
  await startBackend()
  await createMainWindow()

  const smokeExitMs = parsePositiveInt(process.env[SMOKE_EXIT_ENV])
  if (smokeExitMs) {
    setTimeout(() => app.quit(), smokeExitMs).unref()
  }
}

function formatError(error: unknown): string {
  if (error instanceof Error) return error.message
  return String(error)
}

app.on('before-quit', () => {
  isQuitting = true
})

app.on('window-all-closed', () => {
  app.quit()
})

app.on('will-quit', () => {
  void stopBackend()
})

for (const signal of ['SIGINT', 'SIGTERM'] as const) {
  process.on(signal, () => {
    app.quit()
  })
}

const hasSingleInstanceLock = app.requestSingleInstanceLock()
if (!hasSingleInstanceLock) {
  app.quit()
  process.exit(0)
}

app.on('second-instance', () => {
  if (!mainWindow) return
  if (mainWindow.isMinimized()) mainWindow.restore()
  mainWindow.focus()
})

app
  .whenReady()
  .then(() => bootstrap())
  .catch((error) => {
    const message = formatError(error)
    console.error('[electron] startup failed:', message)
    dialog.showErrorBox('ChromaPrint3D Electron Startup Failed', message)
    app.quit()
  })
