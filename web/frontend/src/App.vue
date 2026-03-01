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
import { getApiBase } from './runtime/env'
import { isElectronRuntime } from './runtime/platform'

const appStore = useAppStore()
const { activeTab, serverOnline, serverVersion, activeTasks, totalTasks, isDark } =
  storeToRefs(appStore)

const activeTheme = computed(() => (isDark.value ? darkTheme : null))
const activeThemeOverrides = computed(() =>
  isDark.value ? darkThemeOverrides : lightThemeOverrides,
)
const runtimeLabel = isElectronRuntime() ? 'Electron' : 'Browser'
const apiBase = getApiBase()
const apiBaseDisplay = apiBase || '(同源 /api)'

useAppLifecycle()

function handleColorDBUpdated() {
  appStore.refreshColorDBs()
}
</script>

<template>
  <NConfigProvider :theme="activeTheme" :theme-overrides="activeThemeOverrides">
    <NMessageProvider>
      <NLayout class="app-shell">
        <NLayoutHeader bordered class="app-shell__header">
          <div class="app-shell__header-inner">
            <NSpace align="center" :size="12">
              <NText strong class="app-shell__brand-title">ChromaPrint3D</NText>
              <NText v-if="serverVersion" depth="3" class="app-shell__version">
                v{{ serverVersion }}
              </NText>
            </NSpace>
            <NSpace align="center" :size="10" class="app-shell__header-status">
              <NText v-if="serverOnline && totalTasks > 0" depth="3" class="app-shell__meta-text">
                {{ activeTasks > 0 ? `${activeTasks} 个任务进行中` : `${totalTasks} 个历史任务` }}
              </NText>
              <NTag size="small" :bordered="false" type="info">
                {{ runtimeLabel }}
              </NTag>
              <NTag size="small" :bordered="false">
                API: {{ apiBaseDisplay }}
              </NTag>
              <NSpace align="center" :size="6">
                <NText depth="3" class="app-shell__meta-text">深色</NText>
                <NSwitch v-model:value="isDark" size="small" />
              </NSpace>
              <NTag :type="serverOnline ? 'success' : 'error'" size="small" round>
                {{ serverOnline ? '服务器在线' : '服务器离线' }}
              </NTag>
            </NSpace>
          </div>
        </NLayoutHeader>

        <NLayoutContent class="app-shell__content">
          <div class="app-shell__content-inner">
            <NTabs v-model:value="activeTab" type="line" size="large" animated>
              <NTabPane name="convert" tab="图像转换" display-directive="show">
                <NSpace vertical :size="20" class="tab-pane-content">
                  <NGrid :cols="2" :x-gap="16" :y-gap="16" responsive="screen" item-responsive>
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
                <div class="tab-pane-content">
                  <MattingPanel />
                </div>
              </NTabPane>

              <NTabPane name="vectorize" tab="图像矢量化" display-directive="show">
                <div class="tab-pane-content">
                  <VectorizePanel />
                </div>
              </NTabPane>

              <NTabPane name="calibration" tab="校准工具（四色以下）" display-directive="show">
                <div class="tab-pane-content">
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
                <div class="tab-pane-content">
                  <Calibration8ColorPanel @colordb-updated="handleColorDBUpdated" />
                </div>
              </NTabPane>
            </NTabs>
          </div>
        </NLayoutContent>

        <NLayoutFooter bordered class="app-shell__footer">
          <NSpace align="center" :size="16" justify="center" class="app-shell__footer-content">
            <NText depth="3" class="app-shell__meta-text">
              ChromaPrint3D{{ serverVersion ? ` v${serverVersion}` : '' }}
            </NText>
            <NText depth="3" class="app-shell__meta-text">
              Multi-color 3D Print Image Processor
            </NText>
            <a
              href="https://github.com/neroued/ChromaPrint3D"
              target="_blank"
              rel="noopener noreferrer"
              class="app-shell__footer-link"
            >
              <NText depth="3" class="app-shell__meta-text">GitHub</NText>
            </a>
            <a href="mailto:neroued@gmail.com" class="app-shell__footer-link">
              <NText depth="3" class="app-shell__meta-text">Neroued@gmail.com</NText>
            </a>
          </NSpace>
        </NLayoutFooter>
      </NLayout>
    </NMessageProvider>
  </NConfigProvider>
</template>
