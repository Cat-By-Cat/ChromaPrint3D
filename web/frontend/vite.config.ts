import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

const frontendTarget = (process.env.CHROMAPRINT3D_FRONTEND_TARGET ?? '').trim().toLowerCase()
const isElectronTarget = frontendTarget === 'electron'

export default defineConfig({
  base: isElectronTarget ? './' : '/',
  plugins: [vue()],
  server: {
    port: 5173,
    proxy: {
      '/api': 'http://localhost:8080',
    },
  },
  build: {
    rollupOptions: {
      output: {
        manualChunks: {
          'naive-ui': ['naive-ui'],
        },
      },
    },
  },
})
