#!/usr/bin/env node

import { spawnSync } from 'node:child_process'
import { existsSync } from 'node:fs'
import path from 'node:path'
import process from 'node:process'
import { fileURLToPath } from 'node:url'

function resolvePlatform() {
  const raw = (process.env.CHROMAPRINT3D_ELECTRON_PLATFORM ?? '').trim().toLowerCase()
  if (raw) return raw
  if (process.platform === 'win32') return 'win'
  if (process.platform === 'darwin') return 'mac'
  return 'linux'
}

function resolvePlatformArgs(platform) {
  if (platform === 'linux') return ['--linux']
  if (platform === 'mac') return ['--mac']
  if (platform === 'win' || platform === 'windows') return ['--win']
  throw new Error(
    `Unsupported CHROMAPRINT3D_ELECTRON_PLATFORM="${platform}", expected linux/mac/win`,
  )
}

function resolveArchArgs() {
  const raw = (process.env.CHROMAPRINT3D_ELECTRON_ARCH ?? 'x64').trim().toLowerCase()
  if (raw === 'x64') return ['--x64']
  if (raw === 'arm64') return ['--arm64']
  if (raw === 'universal') return ['--universal']
  throw new Error(`Unsupported CHROMAPRINT3D_ELECTRON_ARCH="${raw}", expected x64/arm64/universal`)
}

const passthroughArgs = process.argv.slice(2).filter((arg) => arg !== '--dir')
const shouldPackDir = process.argv.includes('--dir')
const platformArgs = resolvePlatformArgs(resolvePlatform())
const archArgs = resolveArchArgs()
const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const electronBuilderCli = path.resolve(__dirname, '..', 'node_modules', 'electron-builder', 'cli.js')

if (!existsSync(electronBuilderCli)) {
  throw new Error(`electron-builder cli not found: ${electronBuilderCli}`)
}

const command = process.execPath
const args = [
  electronBuilderCli,
  '--config',
  'electron-builder.yml',
  '--publish',
  'never',
  ...platformArgs,
  ...archArgs,
  ...passthroughArgs,
]

if (shouldPackDir) {
  args.push('--dir')
}

const result = spawnSync(command, args, {
  stdio: 'inherit',
  env: process.env,
})

if (result.error) {
  throw result.error
}

process.exit(result.status ?? 1)
