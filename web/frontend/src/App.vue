<script setup lang="ts">
import { computed } from 'vue'
import { storeToRefs } from 'pinia'
import {
  NConfigProvider,
  NLayout,
  NLayoutHeader,
  NLayoutContent,
  NLayoutFooter,
  NSpace,
  NGrid,
  NGridItem,
  NText,
  NTag,
  NTabs,
  NTabPane,
  NMessageProvider,
  NSwitch,
  darkTheme,
} from 'naive-ui'
import ImageUpload from './components/ImageUpload.vue'
import ParamPanel from './components/ParamPanel.vue'
import ConvertPanel from './components/ConvertPanel.vue'
import ResultPanel from './components/ResultPanel.vue'
import CalibrationPanel from './components/CalibrationPanel.vue'
import Calibration8ColorPanel from './components/Calibration8ColorPanel.vue'
import MattingPanel from './components/MattingPanel.vue'
import VectorizePanel from './components/VectorizePanel.vue'
import { darkThemeOverrides, lightThemeOverrides } from './theme'
import { useAppStore } from './stores/app'
import { useAppLifecycle } from './composables/feature/useAppLifecycle'

const appStore = useAppStore()
const { activeTab, serverOnline, serverVersion, activeTasks, totalTasks, isDark } =
  storeToRefs(appStore)

const activeTheme = computed(() => (isDark.value ? darkTheme : null))
const activeThemeOverrides = computed(() =>
  isDark.value ? darkThemeOverrides : lightThemeOverrides,
)

const layoutStyle = computed(() => ({
  minHeight: '100vh',
  background: 'var(--n-body-color)',
}))

const headerStyle = computed(() => ({
  padding: '12px 24px',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'space-between',
  background: 'var(--n-card-color)',
}))

const contentStyle = {
  padding: '24px',
  maxWidth: '1200px',
  margin: '0 auto',
}

const footerStyle = computed(() => ({
  padding: '16px 24px',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  background: 'var(--n-card-color)',
}))

useAppLifecycle()

function handleColorDBUpdated() {
  appStore.refreshColorDBs()
}
</script>

<template>
  <NConfigProvider :theme="activeTheme" :theme-overrides="activeThemeOverrides">
    <NMessageProvider>
      <NLayout :style="layoutStyle">
        <!-- Header -->
        <NLayoutHeader bordered :style="headerStyle">
          <NSpace align="center" :size="12">
            <NText strong style="font-size: 20px; letter-spacing: 0.5px"> ChromaPrint3D </NText>
            <NText v-if="serverVersion" depth="3" style="font-size: 12px">
              v{{ serverVersion }}
            </NText>
          </NSpace>
          <NSpace align="center" :size="12">
            <NText v-if="serverOnline && totalTasks > 0" depth="3" style="font-size: 12px">
              {{ activeTasks > 0 ? `${activeTasks} 个任务进行中` : `${totalTasks} 个历史任务` }}
            </NText>
            <NSpace align="center" :size="6">
              <NText depth="3" style="font-size: 12px">深色</NText>
              <NSwitch v-model:value="isDark" size="small" />
            </NSpace>
            <NTag :type="serverOnline ? 'success' : 'error'" size="small" round>
              {{ serverOnline ? '服务器在线' : '服务器离线' }}
            </NTag>
          </NSpace>
        </NLayoutHeader>

        <!-- Main content -->
        <NLayoutContent :style="contentStyle">
          <NTabs v-model:value="activeTab" type="line" size="large" animated>
            <NTabPane name="convert" tab="图像转换" display-directive="show">
              <NSpace vertical :size="16" style="padding-top: 16px">
                <NGrid :cols="2" :x-gap="16" responsive="screen" item-responsive>
                  <NGridItem span="2 m:1">
                    <ImageUpload />
                  </NGridItem>
                  <NGridItem span="2 m:1">
                    <ParamPanel />
                  </NGridItem>
                </NGrid>
                <ConvertPanel />
                <ResultPanel />
              </NSpace>
            </NTabPane>

            <NTabPane name="matting" tab="图像抠图" display-directive="show">
              <div style="padding-top: 16px">
                <MattingPanel />
              </div>
            </NTabPane>

            <NTabPane name="vectorize" tab="图像矢量化" display-directive="show">
              <div style="padding-top: 16px">
                <VectorizePanel />
              </div>
            </NTabPane>

            <NTabPane name="calibration" tab="校准工具（四色以下）" display-directive="show">
              <div style="padding-top: 16px">
                <CalibrationPanel @colordb-updated="handleColorDBUpdated" />
              </div>
            </NTabPane>

            <NTabPane name="calibration-8color" display-directive="show">
              <template #tab>
                <NSpace :size="4" align="center">
                  <span>八色校准</span>
                  <NTag size="tiny" type="warning" :bordered="false">Beta</NTag>
                </NSpace>
              </template>
              <div style="padding-top: 16px">
                <Calibration8ColorPanel @colordb-updated="handleColorDBUpdated" />
              </div>
            </NTabPane>
          </NTabs>
        </NLayoutContent>

        <!-- Footer -->
        <NLayoutFooter bordered :style="footerStyle">
          <NSpace align="center" :size="16" justify="center" style="flex-wrap: wrap">
            <NText depth="3" style="font-size: 12px">
              ChromaPrint3D{{ serverVersion ? ` v${serverVersion}` : '' }}
            </NText>
            <NText depth="3" style="font-size: 12px"> Multi-color 3D Print Image Processor </NText>
            <a
              href="https://github.com/neroued/ChromaPrint3D"
              target="_blank"
              rel="noopener noreferrer"
              class="footer-link"
            >
              <NText depth="3" style="font-size: 12px">GitHub</NText>
            </a>
            <a href="mailto:neroued@gmail.com" class="footer-link">
              <NText depth="3" style="font-size: 12px">Neroued@gmail.com</NText>
            </a>
          </NSpace>
        </NLayoutFooter>
      </NLayout>
    </NMessageProvider>
  </NConfigProvider>
</template>

<style>
/* Global reset */
* {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

body {
  font-family:
    -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
  -webkit-font-smoothing: antialiased;
  -moz-osx-font-smoothing: grayscale;
}

.footer-link {
  text-decoration: none;
  transition: opacity 0.2s;
}

.footer-link:hover {
  opacity: 0.7;
}
</style>
