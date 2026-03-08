<script setup lang="ts">
import { computed, ref } from 'vue'
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
  NButton,
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
import ColorDBUploadSection from './components/calibration/ColorDBUploadSection.vue'
import { darkThemeOverrides, lightThemeOverrides } from './theme'
import { useAppStore } from './stores/app'
import { useAppLifecycle } from './composables/feature/useAppLifecycle'
import { isElectronRuntime } from './runtime/platform'
import {
  getSiteIcpNumber,
  getSiteIcpUrl,
  getSitePublicSecurityRecordNumber,
  getSitePublicSecurityRecordUrl,
} from './runtime/env'

const appStore = useAppStore()
const { activeTab, serverOnline, serverVersion, activeTasks, totalTasks, isDark } =
  storeToRefs(appStore)

const activeTheme = computed(() => (isDark.value ? darkTheme : null))
const activeThemeOverrides = computed(() =>
  isDark.value ? darkThemeOverrides : lightThemeOverrides,
)
const runtimeLabel = isElectronRuntime() ? 'Electron' : 'Browser'
const siteIcpNumber = getSiteIcpNumber()
const siteIcpUrl = getSiteIcpUrl()
const sitePublicSecurityRecordNumber = getSitePublicSecurityRecordNumber()
const sitePublicSecurityRecordUrl = getSitePublicSecurityRecordUrl()
const showSiteIcpRecord = !isElectronRuntime() && Boolean(siteIcpNumber)
const showSitePublicSecurityRecord =
  !isElectronRuntime() && Boolean(sitePublicSecurityRecordNumber)
const activePreprocessTab = ref('vectorize')
const activeCalibrationTab = ref('calibration')

// 兼容旧 tab 名称（热更新或旧状态回放场景）。
if (activeTab.value === 'matting' || activeTab.value === 'vectorize') {
  activePreprocessTab.value = activeTab.value
  activeTab.value = 'preprocess'
}
if (activeTab.value === 'calibration' || activeTab.value === 'calibration-8color') {
  activeCalibrationTab.value = activeTab.value
  activeTab.value = 'calibration-tools'
}

useAppLifecycle()

function handleColorDBUpdated() {
  appStore.refreshColorDBs()
}

function goToColorDBUpload() {
  activeTab.value = 'colordb-upload'
}

function goToConvert() {
  activeTab.value = 'convert'
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
            <NTabs
              v-model:value="activeTab"
              type="line"
              size="large"
              animated
              class="top-level-tabs fade-in-up"
            >
              <NTabPane name="convert" tab="叠色模型生成" display-directive="show">
                <NSpace vertical :size="20" class="tab-pane-content">
                  <div class="convert-colordb-entry">
                    <NText depth="3">需要导入已有 ColorDB JSON？</NText>
                    <NButton tertiary size="small" @click="goToColorDBUpload">
                      前往上传 ColorDB
                    </NButton>
                  </div>
                  <NGrid :cols="2" :x-gap="16" :y-gap="16" responsive="screen" item-responsive>
                    <NGridItem span="2 m:1">
                      <NSpace vertical :size="16">
                        <ImageUpload />
                        <ConvertPanel />
                      </NSpace>
                    </NGridItem>
                    <NGridItem span="2 m:1">
                      <ParamPanel />
                    </NGridItem>
                  </NGrid>
                  <ResultPanel />
                </NSpace>
              </NTabPane>

              <NTabPane name="preprocess" tab="图像预处理工具" display-directive="show">
                <div class="tab-pane-content">
                  <NTabs
                    v-model:value="activePreprocessTab"
                    type="segment"
                    animated
                    class="nested-tabs fade-in-up"
                  >
                    <NTabPane name="vectorize" tab="图像矢量化" display-directive="show">
                      <div class="nested-tab-pane-content">
                        <VectorizePanel />
                      </div>
                    </NTabPane>
                    <NTabPane name="matting" display-directive="show">
                      <template #tab>
                        <NSpace :size="4" align="center">
                          <span>图像抠图</span>
                          <NTag size="tiny" type="warning" :bordered="false">Beta</NTag>
                        </NSpace>
                      </template>
                      <div class="nested-tab-pane-content">
                        <MattingPanel />
                      </div>
                    </NTabPane>
                  </NTabs>
                </div>
              </NTabPane>

              <NTabPane name="calibration-tools" tab="校准工具" display-directive="show">
                <div class="tab-pane-content">
                  <NTabs
                    v-model:value="activeCalibrationTab"
                    type="segment"
                    animated
                    class="nested-tabs fade-in-up"
                  >
                    <NTabPane name="calibration" tab="四色及以下模式" display-directive="show">
                      <div class="nested-tab-pane-content">
                        <CalibrationPanel @colordb-updated="handleColorDBUpdated" />
                      </div>
                    </NTabPane>
                    <NTabPane name="calibration-8color" tab="八色模式" display-directive="show">
                      <div class="nested-tab-pane-content">
                        <Calibration8ColorPanel @colordb-updated="handleColorDBUpdated" />
                      </div>
                    </NTabPane>
                  </NTabs>
                </div>
              </NTabPane>

              <NTabPane name="colordb-upload" tab="上传 ColorDB" display-directive="show">
                <div class="tab-pane-content">
                  <NSpace vertical :size="20" class="calibration-layout">
                    <ColorDBUploadSection
                      title="上传已有 ColorDB"
                      tips="如果你已有可用 ColorDB JSON，可直接上传并在当前会话使用。上传成功后可返回“叠色模型生成”页面选择该数据库。"
                      @colordb-updated="handleColorDBUpdated"
                      @go-convert="goToConvert"
                    />
                  </NSpace>
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
              v-if="showSiteIcpRecord"
              :href="siteIcpUrl"
              target="_blank"
              rel="noopener noreferrer"
              class="app-shell__footer-link"
            >
              <NText depth="3" class="app-shell__meta-text">{{ siteIcpNumber }}</NText>
            </a>
            <a
              v-if="showSitePublicSecurityRecord"
              :href="sitePublicSecurityRecordUrl"
              target="_blank"
              rel="noopener noreferrer"
              class="app-shell__footer-link"
            >
              <NText depth="3" class="app-shell__meta-text">
                {{ sitePublicSecurityRecordNumber }}
              </NText>
            </a>
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
