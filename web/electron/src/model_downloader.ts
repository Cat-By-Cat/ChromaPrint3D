import { createHash } from 'node:crypto'
import { once } from 'node:events'
import fs from 'node:fs'
import { promises as fsp } from 'node:fs'
import path from 'node:path'
import { setTimeout as sleep } from 'node:timers/promises'

export type ModelSource = {
  name: string
  baseUrl: string
}

type ManifestModel = {
  path: string
  sha256: string
  sizeBytes: number
}

type ModelManifest = {
  repository: string
  sources: ModelSource[]
  models: ManifestModel[]
}

export type ModelFileStatus = {
  path: string
  sizeBytes: number
  expectedSha256: string
  state: 'installed' | 'missing' | 'invalid'
  exists: boolean
  message?: string
}

export type ModelDownloadStatus = {
  repository: string
  dataDir: string
  modelDir: string
  manifestPath: string
  sources: ModelSource[]
  models: ModelFileStatus[]
  totalModels: number
  installedModels: number
  missingModels: number
  invalidModels: number
  running: boolean
  currentFilePath?: string
  lastError?: string
}

export type ModelSourceConnectivity = {
  name: string
  baseUrl: string
  ok: boolean
  checkedModels: number
  reachableModels: number
  statusCode?: number
  responseTimeMs: number
  message: string
}

export type ModelConnectivityReport = {
  repository: string
  checkedAtMs: number
  totalSources: number
  availableSources: number
  checkedModels: number
  sources: ModelSourceConnectivity[]
}

export type ModelDownloadProgress = {
  type:
    | 'start'
    | 'file-start'
    | 'file-progress'
    | 'retry'
    | 'source-fallback'
    | 'completed'
    | 'cancelled'
    | 'error'
  message: string
  downloadedBytes: number
  totalBytes: number
  percent: number
  filePath?: string
  fileDownloadedBytes?: number
  fileTotalBytes?: number
  sourceName?: string
  sourceUrl?: string
  attempt?: number
  speedBytesPerSec?: number
}

type ProgressEmitter = (progress: ModelDownloadProgress) => void

type DownloaderOptions = {
  dataDir: string
  manifestRelativePath?: string
  retriesPerSource?: number
  requestTimeoutMs?: number
  retryBackoffBaseMs?: number
  onProgress?: ProgressEmitter
}

type ValidatedFile = {
  valid: boolean
  reason?: string
}

type ProbeResult = {
  ok: boolean
  statusCode?: number
  responseTimeMs: number
  message: string
}

const CONNECTIVITY_SAMPLE_MODELS = 1
const CONNECTIVITY_PROBE_TIMEOUT_MIN_MS = 800
const CONNECTIVITY_PROBE_TIMEOUT_MAX_MS = 2500

class DownloadCancelledError extends Error {
  constructor(message = 'Model download cancelled') {
    super(message)
    this.name = 'DownloadCancelledError'
  }
}

function normalizeBaseUrl(raw: string): string {
  return raw.trim().replace(/\/+$/, '')
}

function toSourceName(rawName: string | undefined, baseUrl: string): string {
  if (rawName && rawName.trim()) return rawName.trim()
  try {
    const parsed = new URL(baseUrl)
    return parsed.host
  } catch {
    return baseUrl
  }
}

function buildModelUrl(baseUrl: string, relativePath: string): string {
  const normalizedBase = normalizeBaseUrl(baseUrl)
  const normalizedPath = relativePath.replace(/^\/+/, '')
  return `${normalizedBase}/${normalizedPath}`
}

function parseContentRangeTotal(value: string | null): number | null {
  if (!value) return null
  const match = value.match(/\/(\d+)\s*$/)
  if (!match) return null
  const parsed = Number(match[1])
  return Number.isFinite(parsed) && parsed > 0 ? parsed : null
}

async function ensureParentDir(filePath: string): Promise<void> {
  await fsp.mkdir(path.dirname(filePath), { recursive: true })
}

async function safeUnlink(filePath: string): Promise<void> {
  try {
    await fsp.unlink(filePath)
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'ENOENT') throw error
  }
}

async function sha256File(filePath: string): Promise<string> {
  const hash = createHash('sha256')
  const input = fs.createReadStream(filePath)
  for await (const chunk of input) {
    hash.update(chunk as Buffer)
  }
  return hash.digest('hex')
}

async function statOrNull(filePath: string): Promise<fs.Stats | null> {
  try {
    return await fsp.stat(filePath)
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === 'ENOENT') return null
    throw error
  }
}

async function closeWriteStream(stream: fs.WriteStream): Promise<void> {
  await new Promise<void>((resolve, reject) => {
    stream.once('error', reject)
    stream.end(() => resolve())
  })
}

function toErrorMessage(error: unknown): string {
  if (error instanceof Error) return error.message
  return String(error)
}

export class ModelDownloader {
  private readonly dataDir: string
  private readonly modelDir: string
  private readonly manifestPath: string
  private readonly retriesPerSource: number
  private readonly requestTimeoutMs: number
  private readonly retryBackoffBaseMs: number
  private readonly onProgress?: ProgressEmitter

  private running = false
  private cancelRequested = false
  private abortController: AbortController | null = null
  private currentFilePath: string | undefined
  private lastError: string | undefined
  private progressLastAtMs = 0
  private progressLastBytes = 0
  private progressSpeedBytesPerSec = 0

  constructor(options: DownloaderOptions) {
    this.dataDir = options.dataDir
    this.modelDir = path.join(this.dataDir, 'models')
    this.manifestPath = path.join(this.dataDir, options.manifestRelativePath ?? 'models/models.json')
    this.retriesPerSource = Math.max(1, options.retriesPerSource ?? 3)
    this.requestTimeoutMs = Math.max(5000, options.requestTimeoutMs ?? 120_000)
    this.retryBackoffBaseMs = Math.max(200, options.retryBackoffBaseMs ?? 1500)
    this.onProgress = options.onProgress
  }

  isRunning(): boolean {
    return this.running
  }

  cancel(): void {
    this.cancelRequested = true
    this.abortController?.abort()
  }

  async getStatus(): Promise<ModelDownloadStatus> {
    const manifest = await this.loadManifest()
    const modelStatuses: ModelFileStatus[] = []
    let installedModels = 0
    let missingModels = 0
    let invalidModels = 0

    for (const entry of manifest.models) {
      const targetPath = path.join(this.modelDir, entry.path)
      const validated = await this.validateExistingFile(targetPath, entry)
      if (validated.valid) {
        installedModels += 1
        modelStatuses.push({
          path: entry.path,
          sizeBytes: entry.sizeBytes,
          expectedSha256: entry.sha256,
          state: 'installed',
          exists: true,
        })
        continue
      }

      const exists = Boolean(await statOrNull(targetPath))
      if (exists) {
        invalidModels += 1
        modelStatuses.push({
          path: entry.path,
          sizeBytes: entry.sizeBytes,
          expectedSha256: entry.sha256,
          state: 'invalid',
          exists: true,
          message: validated.reason ?? 'checksum mismatch',
        })
      } else {
        missingModels += 1
        modelStatuses.push({
          path: entry.path,
          sizeBytes: entry.sizeBytes,
          expectedSha256: entry.sha256,
          state: 'missing',
          exists: false,
          message: 'file not found',
        })
      }
    }

    return {
      repository: manifest.repository,
      dataDir: this.dataDir,
      modelDir: this.modelDir,
      manifestPath: this.manifestPath,
      sources: manifest.sources,
      models: modelStatuses,
      totalModels: manifest.models.length,
      installedModels,
      missingModels,
      invalidModels,
      running: this.running,
      currentFilePath: this.currentFilePath,
      lastError: this.lastError,
    }
  }

  async checkConnectivity(): Promise<ModelConnectivityReport> {
    if (this.running) {
      throw new Error('Connectivity check is unavailable while downloading')
    }
    const manifest = await this.loadManifest()
    return await this.checkConnectivityForManifest(manifest)
  }

  async downloadAll(): Promise<ModelDownloadStatus> {
    if (this.running) {
      throw new Error('Model download is already running')
    }

    this.running = true
    this.cancelRequested = false
    this.currentFilePath = undefined
    this.lastError = undefined

    try {
      const manifest = await this.loadManifest()
      const connectivity = await this.checkConnectivityForManifest(manifest)
      if (connectivity.availableSources <= 0) {
        throw new Error('下载源连通性检查失败：没有可用下载源，请先检查网络或切换网络环境后重试。')
      }
      const totalBytes = manifest.models.reduce((sum, entry) => sum + Math.max(0, entry.sizeBytes), 0)
      let verifiedBytes = 0

      for (const entry of manifest.models) {
        this.throwIfCancelled()
        const targetPath = path.join(this.modelDir, entry.path)
        const existing = await this.validateExistingFile(targetPath, entry)
        if (existing.valid) {
          verifiedBytes += entry.sizeBytes
        }
      }

      this.resetSpeedTracker(verifiedBytes)
      this.emitProgress({
        type: 'start',
        message: `连通性检查通过（可用源 ${connectivity.availableSources}/${connectivity.totalSources}），开始下载模型（共 ${manifest.models.length} 个文件）`,
        downloadedBytes: verifiedBytes,
        totalBytes,
        percent: this.computePercent(verifiedBytes, totalBytes),
        speedBytesPerSec: 0,
      })

      for (const entry of manifest.models) {
        this.throwIfCancelled()
        const targetPath = path.join(this.modelDir, entry.path)
        const existing = await this.validateExistingFile(targetPath, entry)
        if (existing.valid) continue

        this.currentFilePath = entry.path
        await this.downloadOneModel(entry, manifest.sources, verifiedBytes, totalBytes)
        verifiedBytes += entry.sizeBytes
      }

      this.currentFilePath = undefined
      this.emitProgress({
        type: 'completed',
        message: '模型下载完成，重启应用后可加载深度学习抠图模型。',
        downloadedBytes: totalBytes,
        totalBytes,
        percent: this.computePercent(totalBytes, totalBytes),
        speedBytesPerSec: 0,
      })
      return await this.getStatus()
    } catch (error) {
      if (error instanceof DownloadCancelledError) {
        const status = await this.getStatus()
        this.emitProgress({
          type: 'cancelled',
          message: '模型下载已取消',
          downloadedBytes: 0,
          totalBytes: 0,
          percent: 0,
        })
        return status
      }
      const message = toErrorMessage(error)
      this.lastError = message
      this.emitProgress({
        type: 'error',
        message,
        downloadedBytes: 0,
        totalBytes: 0,
        percent: 0,
      })
      throw error
    } finally {
      this.abortController = null
      this.currentFilePath = undefined
      this.running = false
      this.cancelRequested = false
    }
  }

  private throwIfCancelled(): void {
    if (this.cancelRequested) throw new DownloadCancelledError()
  }

  private computePercent(downloadedBytes: number, totalBytes: number): number {
    if (totalBytes <= 0) return 0
    return Math.min(100, Math.max(0, Number(((downloadedBytes / totalBytes) * 100).toFixed(1))))
  }

  private resetSpeedTracker(initialDownloadedBytes: number): void {
    this.progressLastAtMs = Date.now()
    this.progressLastBytes = Math.max(0, initialDownloadedBytes)
    this.progressSpeedBytesPerSec = 0
  }

  private updateSpeedTracker(currentDownloadedBytes: number): number {
    const now = Date.now()
    const elapsedMs = now - this.progressLastAtMs
    if (elapsedMs < 250) return this.progressSpeedBytesPerSec

    const bytesDelta = currentDownloadedBytes - this.progressLastBytes
    this.progressLastAtMs = now
    this.progressLastBytes = currentDownloadedBytes
    if (bytesDelta <= 0) return this.progressSpeedBytesPerSec

    const instant = (bytesDelta * 1000) / elapsedMs
    if (!Number.isFinite(instant) || instant <= 0) return this.progressSpeedBytesPerSec
    if (this.progressSpeedBytesPerSec <= 0) {
      this.progressSpeedBytesPerSec = instant
      return this.progressSpeedBytesPerSec
    }
    // Exponential moving average keeps speed stable while reacting quickly enough.
    this.progressSpeedBytesPerSec = this.progressSpeedBytesPerSec * 0.75 + instant * 0.25
    return this.progressSpeedBytesPerSec
  }

  private emitProgress(progress: ModelDownloadProgress): void {
    this.onProgress?.(progress)
  }

  private async waitRetryDelay(attempt: number): Promise<void> {
    const backoff = this.retryBackoffBaseMs * Math.pow(2, Math.max(0, attempt - 1))
    const jitter = 0.8 + Math.random() * 0.4
    await sleep(Math.round(backoff * jitter))
  }

  private async validateExistingFile(filePath: string, entry: ManifestModel): Promise<ValidatedFile> {
    const stats = await statOrNull(filePath)
    if (!stats || !stats.isFile()) return { valid: false, reason: 'file not found' }
    if (entry.sizeBytes > 0 && stats.size !== entry.sizeBytes) {
      return { valid: false, reason: `size mismatch (${stats.size} != ${entry.sizeBytes})` }
    }
    const actualSha = await sha256File(filePath)
    if (actualSha !== entry.sha256) {
      return { valid: false, reason: 'sha256 mismatch' }
    }
    return { valid: true }
  }

  private async checkConnectivityForManifest(
    manifest: ModelManifest,
  ): Promise<ModelConnectivityReport> {
    const sampledModels = manifest.models.slice(0, Math.max(1, CONNECTIVITY_SAMPLE_MODELS))
    const checkedModels = sampledModels.length
    const checkedAtMs = Date.now()
    const sourceReports: ModelSourceConnectivity[] = []
    let availableSources = 0

    for (const source of manifest.sources) {
      this.throwIfCancelled()
      let reachableModels = 0
      let accumulatedMs = 0
      let lastStatusCode: number | undefined
      let lastMessage = 'probe not started'

      for (const model of sampledModels) {
        this.throwIfCancelled()
        const probe = await this.probeSourceModel(source, model.path)
        accumulatedMs += probe.responseTimeMs
        if (probe.statusCode !== undefined) lastStatusCode = probe.statusCode
        lastMessage = probe.message
        if (probe.ok) reachableModels += 1
      }

      const ok = reachableModels > 0
      if (ok) availableSources += 1
      const avgResponseTime = checkedModels > 0 ? Math.round(accumulatedMs / checkedModels) : 0
      sourceReports.push({
        name: source.name,
        baseUrl: source.baseUrl,
        ok,
        checkedModels,
        reachableModels,
        statusCode: lastStatusCode,
        responseTimeMs: avgResponseTime,
        message: ok ? `reachable ${reachableModels}/${checkedModels}` : lastMessage,
      })
    }

    return {
      repository: manifest.repository,
      checkedAtMs,
      totalSources: manifest.sources.length,
      availableSources,
      checkedModels,
      sources: sourceReports,
    }
  }

  private async probeSourceModel(source: ModelSource, relativePath: string): Promise<ProbeResult> {
    const url = buildModelUrl(source.baseUrl, relativePath)
    const timeoutMs = Math.max(
      CONNECTIVITY_PROBE_TIMEOUT_MIN_MS,
      Math.min(CONNECTIVITY_PROBE_TIMEOUT_MAX_MS, Math.floor(this.requestTimeoutMs / 100)),
    )
    const startedAt = Date.now()
    const controller = new AbortController()
    this.abortController = controller
    const timer = setTimeout(() => controller.abort(), timeoutMs)
    try {
      const response = await fetch(url, {
        method: 'GET',
        headers: { Range: 'bytes=0-0' },
        signal: controller.signal,
        cache: 'no-store',
      })
      const responseTimeMs = Date.now() - startedAt
      const statusCode = response.status
      const ok = statusCode === 200 || statusCode === 206
      await response.body?.cancel()
      return {
        ok,
        statusCode,
        responseTimeMs,
        message: ok ? 'ok' : `HTTP ${statusCode}`,
      }
    } catch (error) {
      if ((error as Error).name === 'AbortError' && this.cancelRequested) {
        throw new DownloadCancelledError()
      }
      const responseTimeMs = Date.now() - startedAt
      if ((error as Error).name === 'AbortError') {
        return {
          ok: false,
          responseTimeMs,
          message: `timeout (${timeoutMs}ms)`,
        }
      }
      return {
        ok: false,
        responseTimeMs,
        message: toErrorMessage(error),
      }
    } finally {
      clearTimeout(timer)
      if (this.abortController === controller) {
        this.abortController = null
      }
    }
  }

  private async downloadOneModel(
    entry: ManifestModel,
    sources: ModelSource[],
    verifiedBytesBeforeFile: number,
    totalBytes: number,
  ): Promise<void> {
    const targetPath = path.join(this.modelDir, entry.path)
    const partPath = `${targetPath}.part`

    await ensureParentDir(targetPath)

    let lastError: unknown = null
    for (let sourceIndex = 0; sourceIndex < sources.length; sourceIndex += 1) {
      const source = sources[sourceIndex]
      for (let attempt = 1; attempt <= this.retriesPerSource; attempt += 1) {
        this.throwIfCancelled()
        try {
          await this.downloadFromSource(
            entry,
            source,
            attempt,
            targetPath,
            partPath,
            verifiedBytesBeforeFile,
            totalBytes,
            (fileDone, fileTotal) => {
              const downloadedBytes = verifiedBytesBeforeFile + fileDone
              const speedBytesPerSec = this.updateSpeedTracker(downloadedBytes)
              this.emitProgress({
                type: 'file-progress',
                message: `下载中：${entry.path}`,
                filePath: entry.path,
                sourceName: source.name,
                sourceUrl: source.baseUrl,
                fileDownloadedBytes: fileDone,
                fileTotalBytes: fileTotal,
                downloadedBytes,
                totalBytes,
                percent: this.computePercent(downloadedBytes, totalBytes),
                speedBytesPerSec,
              })
            },
          )
          return
        } catch (error) {
          if (error instanceof DownloadCancelledError) throw error
          lastError = error
          const message = toErrorMessage(error)
          if (attempt < this.retriesPerSource) {
            this.emitProgress({
              type: 'retry',
              message: `下载失败，准备重试（${attempt}/${this.retriesPerSource}）：${message}`,
              filePath: entry.path,
              sourceName: source.name,
              sourceUrl: source.baseUrl,
              attempt,
              downloadedBytes: verifiedBytesBeforeFile,
              totalBytes,
              percent: this.computePercent(verifiedBytesBeforeFile, totalBytes),
            })
            await this.waitRetryDelay(attempt)
            continue
          }
        }
      }
      if (sourceIndex < sources.length - 1) {
        this.emitProgress({
          type: 'source-fallback',
          message: `主源失败，切换到下一个下载源：${sources[sourceIndex + 1]?.name ?? 'unknown'}`,
          filePath: entry.path,
          sourceName: source.name,
          sourceUrl: source.baseUrl,
          downloadedBytes: verifiedBytesBeforeFile,
          totalBytes,
          percent: this.computePercent(verifiedBytesBeforeFile, totalBytes),
        })
      }
    }

    throw new Error(`Failed to download "${entry.path}": ${toErrorMessage(lastError)}`)
  }

  private async downloadFromSource(
    entry: ManifestModel,
    source: ModelSource,
    attempt: number,
    targetPath: string,
    partPath: string,
    verifiedBytesBeforeFile: number,
    totalBytes: number,
    onChunkProgress: (fileDone: number, fileTotal: number) => void,
  ): Promise<void> {
    const url = buildModelUrl(source.baseUrl, entry.path)
    const partStats = await statOrNull(partPath)
    let partSize = partStats?.size ?? 0
    if (entry.sizeBytes > 0 && partSize > entry.sizeBytes) {
      await safeUnlink(partPath)
      partSize = 0
    }

    this.emitProgress({
      type: 'file-start',
      message: `下载 ${entry.path}（${source.name}，尝试 ${attempt}/${this.retriesPerSource}）`,
      filePath: entry.path,
      sourceName: source.name,
      sourceUrl: source.baseUrl,
      attempt,
      downloadedBytes: verifiedBytesBeforeFile + partSize,
      totalBytes,
      percent: this.computePercent(verifiedBytesBeforeFile + partSize, totalBytes),
      fileDownloadedBytes: partSize,
      fileTotalBytes: entry.sizeBytes,
    })

    this.abortController = new AbortController()
    const headers: HeadersInit = {}
    if (partSize > 0) headers.Range = `bytes=${partSize}-`
    const timeoutHandle = setTimeout(() => this.abortController?.abort(), this.requestTimeoutMs)

    let response: Response
    try {
      response = await fetch(url, {
        method: 'GET',
        headers,
        signal: this.abortController.signal,
        cache: 'no-store',
      })
    } catch (error) {
      if ((error as Error).name === 'AbortError' && this.cancelRequested) {
        throw new DownloadCancelledError()
      }
      throw new Error(`Request failed: ${toErrorMessage(error)}`)
    } finally {
      clearTimeout(timeoutHandle)
    }

    if (response.status === 416 && entry.sizeBytes > 0 && partSize === entry.sizeBytes) {
      const checksum = await sha256File(partPath)
      if (checksum !== entry.sha256) {
        await safeUnlink(partPath)
        throw new Error(`Checksum mismatch after resume completion for ${entry.path}`)
      }
      await safeUnlink(targetPath)
      await fsp.rename(partPath, targetPath)
      return
    }

    if (response.status !== 200 && response.status !== 206) {
      throw new Error(`HTTP ${response.status}`)
    }

    let fileDownloadedBytes = partSize
    let fileTotalBytes = entry.sizeBytes
    let writeFlags: 'a' | 'w' = 'w'
    if (response.status === 206) {
      writeFlags = 'a'
      const totalFromHeader = parseContentRangeTotal(response.headers.get('content-range'))
      if (totalFromHeader && fileTotalBytes <= 0) fileTotalBytes = totalFromHeader
    } else {
      writeFlags = 'w'
      fileDownloadedBytes = 0
      partSize = 0
      await safeUnlink(partPath)
      const contentLength = Number(response.headers.get('content-length') ?? '0')
      if (contentLength > 0 && fileTotalBytes <= 0) fileTotalBytes = contentLength
    }

    const writer = fs.createWriteStream(partPath, { flags: writeFlags })
    try {
      const reader = response.body?.getReader()
      if (!reader) throw new Error('Empty response body')

      onChunkProgress(fileDownloadedBytes, fileTotalBytes)
      while (true) {
        this.throwIfCancelled()
        const { done, value } = await reader.read()
        if (done) break
        if (!value || value.length === 0) continue
        const chunk = Buffer.from(value)
        if (!writer.write(chunk)) {
          await once(writer, 'drain')
        }
        fileDownloadedBytes += chunk.length
        onChunkProgress(fileDownloadedBytes, fileTotalBytes)
      }
      await closeWriteStream(writer)
    } catch (error) {
      writer.destroy()
      if (error instanceof DownloadCancelledError) throw error
      if ((error as Error).name === 'AbortError' && this.cancelRequested) {
        throw new DownloadCancelledError()
      }
      throw error
    } finally {
      this.abortController = null
    }

    if (fileTotalBytes > 0 && fileDownloadedBytes !== fileTotalBytes) {
      throw new Error(
        `Incomplete download for ${entry.path}: ${fileDownloadedBytes} / ${fileTotalBytes}`,
      )
    }

    const checksum = await sha256File(partPath)
    if (checksum !== entry.sha256) {
      await safeUnlink(partPath)
      throw new Error(`Checksum mismatch for ${entry.path}`)
    }

    await safeUnlink(targetPath)
    await fsp.rename(partPath, targetPath)
  }

  private async loadManifest(): Promise<ModelManifest> {
    const rawText = await fsp.readFile(this.manifestPath, 'utf8')
    const raw = JSON.parse(rawText) as {
      repository?: unknown
      base_url?: unknown
      sources?: unknown
      models?: unknown
    }

    const repository = typeof raw.repository === 'string' ? raw.repository : 'unknown'
    const modelsRaw = Array.isArray(raw.models) ? raw.models : []
    const models: ManifestModel[] = modelsRaw
      .map((item) => {
        const row = item as { path?: unknown; sha256?: unknown; size_bytes?: unknown }
        if (
          typeof row.path !== 'string' ||
          typeof row.sha256 !== 'string' ||
          !row.path.trim() ||
          !row.sha256.trim()
        ) {
          return null
        }
        const sizeBytes = Number(row.size_bytes ?? 0)
        return {
          path: row.path.trim(),
          sha256: row.sha256.trim().toLowerCase(),
          sizeBytes: Number.isFinite(sizeBytes) && sizeBytes > 0 ? Math.floor(sizeBytes) : 0,
        }
      })
      .filter((item): item is ManifestModel => Boolean(item))

    if (models.length === 0) {
      throw new Error(`No model entries found in manifest: ${this.manifestPath}`)
    }

    const rawSources = Array.isArray(raw.sources) ? raw.sources : []
    const parsedSources: ModelSource[] = rawSources
      .map((item) => {
        const row = item as { name?: unknown; base_url?: unknown; baseUrl?: unknown }
        const baseUrlRaw =
          typeof row.base_url === 'string'
            ? row.base_url
            : typeof row.baseUrl === 'string'
              ? row.baseUrl
              : ''
        const baseUrl = normalizeBaseUrl(baseUrlRaw)
        if (!baseUrl) return null
        const name = toSourceName(typeof row.name === 'string' ? row.name : undefined, baseUrl)
        return { name, baseUrl }
      })
      .filter((item): item is ModelSource => Boolean(item))

    if (parsedSources.length === 0 && typeof raw.base_url === 'string' && raw.base_url.trim()) {
      const baseUrl = normalizeBaseUrl(raw.base_url)
      parsedSources.push({ name: toSourceName(undefined, baseUrl), baseUrl })
    }
    if (parsedSources.length === 0) {
      throw new Error(`No available download source found in manifest: ${this.manifestPath}`)
    }

    return {
      repository,
      sources: parsedSources,
      models,
    }
  }
}
