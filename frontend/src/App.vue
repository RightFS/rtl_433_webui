<template>
  <a-config-provider :theme="{ token: { colorPrimary: '#1890ff' } }">
    <a-layout class="app-layout">
      <!-- Desktop sidebar -->
      <a-layout-sider
        v-if="!isMobile"
        v-model:collapsed="collapsed"
        collapsible
        :width="220"
        class="sider"
      >
        <div class="logo">
          <span v-if="!collapsed">📡 rtl_433</span>
          <span v-else>📡</span>
        </div>
        <a-menu
          theme="dark"
          mode="inline"
          :selected-keys="[currentRoute]"
          @click="navigate"
        >
          <a-menu-item key="dashboard">
            <template #icon><DashboardOutlined /></template>
            Dashboard
          </a-menu-item>
          <a-menu-item key="live">
            <template #icon><RadarChartOutlined /></template>
            Live Data
          </a-menu-item>
          <a-menu-item key="settings">
            <template #icon><SettingOutlined /></template>
            Settings
          </a-menu-item>
          <a-menu-item key="log">
            <template #icon><FileTextOutlined /></template>
            Signal Log
          </a-menu-item>
        </a-menu>
        <div class="status-indicator">
          <a-badge :status="store.running ? 'processing' : 'default'" />
          <span v-if="!collapsed" style="color:#aaa;font-size:12px">
            {{ store.running ? 'Running' : 'Stopped' }}
          </span>
        </div>
      </a-layout-sider>

      <a-layout>
        <a-layout-header class="header">
          <div class="header-left">
            <MenuOutlined v-if="isMobile" @click="mobileDrawer = true" style="font-size:20px;cursor:pointer" />
            <span class="header-title">rtl_433 WebUI</span>
          </div>
          <div class="header-right">
            <a-badge :status="store.connected ? 'success' : 'error'" :text="store.connected ? 'Connected' : 'Disconnected'" />
          </div>
        </a-layout-header>

        <a-layout-content class="content">
          <router-view />
        </a-layout-content>
      </a-layout>

      <!-- Mobile drawer -->
      <a-drawer
        v-if="isMobile"
        v-model:open="mobileDrawer"
        placement="left"
        :closable="true"
        :width="220"
        title="rtl_433 WebUI"
      >
        <a-menu
          mode="inline"
          :selected-keys="[currentRoute]"
          @click="(e) => { navigate(e); mobileDrawer = false }"
        >
          <a-menu-item key="dashboard"><DashboardOutlined /> Dashboard</a-menu-item>
          <a-menu-item key="live"><RadarChartOutlined /> Live Data</a-menu-item>
          <a-menu-item key="settings"><SettingOutlined /> Settings</a-menu-item>
          <a-menu-item key="log"><FileTextOutlined /> Signal Log</a-menu-item>
        </a-menu>
      </a-drawer>
    </a-layout>
  </a-config-provider>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import {
  DashboardOutlined, RadarChartOutlined,
  SettingOutlined, FileTextOutlined, MenuOutlined,
} from '@ant-design/icons-vue'
import { useAppStore } from './store/index.js'
import { useWebSocket } from './composables/useWebSocket.js'

const store       = useAppStore()
const router      = useRouter()
const route       = useRoute()
const collapsed   = ref(false)
const mobileDrawer = ref(false)
const isMobile    = ref(window.innerWidth < 768)

const currentRoute = computed(() => route.path.replace('/', '') || 'dashboard')

function navigate({ key }) {
  router.push('/' + key)
}

const { connect } = useWebSocket()
onMounted(() => {
  connect()
  window.addEventListener('resize', () => {
    isMobile.value = window.innerWidth < 768
  })
})
</script>

<style>
html, body, #app { height: 100%; margin: 0; padding: 0; }
.app-layout { min-height: 100vh; }
.sider .logo {
  height: 64px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #fff;
  font-size: 18px;
  font-weight: bold;
  background: rgba(255,255,255,0.05);
}
.sider .status-indicator {
  position: absolute;
  bottom: 48px;
  left: 0; right: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 8px;
}
.header {
  background: #fff;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 24px;
  box-shadow: 0 1px 4px rgba(0,21,41,.08);
}
.header-title { font-size: 18px; font-weight: 600; margin-left: 12px; }
.content { padding: 24px; background: #f0f2f5; overflow: auto; }
@media (max-width: 768px) {
  .content { padding: 12px; }
  .header { padding: 0 12px; }
}
</style>
