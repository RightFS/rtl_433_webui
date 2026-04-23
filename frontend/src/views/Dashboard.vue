<template>
  <div class="dashboard">
    <a-row :gutter="[16, 16]">
      <!-- Status card -->
      <a-col :xs="24" :sm="12" :lg="6">
        <a-card>
          <a-statistic
            title="Status"
            :value="store.running ? 'Running' : 'Stopped'"
            :value-style="{ color: store.running ? '#3f8600' : '#cf1322' }"
          >
            <template #prefix>
              <CheckCircleOutlined v-if="store.running" />
              <StopOutlined v-else />
            </template>
          </a-statistic>
        </a-card>
      </a-col>

      <!-- Signal count -->
      <a-col :xs="24" :sm="12" :lg="6">
        <a-card>
          <a-statistic
            title="Signals Received"
            :value="store.signalCount"
            :precision="0"
          >
            <template #prefix><RadarChartOutlined /></template>
          </a-statistic>
        </a-card>
      </a-col>

      <!-- Uptime -->
      <a-col :xs="24" :sm="12" :lg="6">
        <a-card>
          <a-statistic
            title="Uptime"
            :value="formatUptime(store.uptime)"
          >
            <template #prefix><ClockCircleOutlined /></template>
          </a-statistic>
        </a-card>
      </a-col>

      <!-- Frequency -->
      <a-col :xs="24" :sm="12" :lg="6">
        <a-card>
          <a-statistic
            title="Frequency"
            :value="(store.config.frequency / 1e6).toFixed(3)"
            suffix="MHz"
          >
            <template #prefix><WifiOutlined /></template>
          </a-statistic>
        </a-card>
      </a-col>
    </a-row>

    <!-- Controls -->
    <a-row :gutter="[16, 16]" style="margin-top:16px">
      <a-col :span="24">
        <a-card title="Control">
          <a-space wrap>
            <a-button
              type="primary"
              :disabled="store.running"
              @click="startRtl433"
              :icon="h(PlayCircleOutlined)"
            >Start</a-button>
            <a-button
              danger
              :disabled="!store.running"
              @click="stopRtl433"
              :icon="h(PauseCircleOutlined)"
            >Stop</a-button>
            <a-button @click="clearData" :icon="h(ClearOutlined)">Clear Data</a-button>
            <a-tag :color="store.connected ? 'green' : 'red'">
              {{ store.connected ? 'WebSocket Connected' : 'Disconnected' }}
            </a-tag>
          </a-space>
        </a-card>
      </a-col>
    </a-row>

    <!-- Recent protocols chart -->
    <a-row :gutter="[16, 16]" style="margin-top:16px">
      <a-col :xs="24" :lg="12">
        <a-card title="Active Protocols">
          <div v-if="store.recentProtocols.length === 0" class="empty-hint">
            No data yet. Start rtl_433 to begin receiving signals.
          </div>
          <div v-else>
            <div
              v-for="p in store.recentProtocols"
              :key="p.name"
              class="proto-row"
            >
              <span class="proto-name">{{ p.name }}</span>
              <a-progress
                :percent="Math.round((p.count / totalCount) * 100)"
                :format="() => p.count"
                size="small"
                style="flex:1;margin:0 8px"
              />
            </div>
          </div>
        </a-card>
      </a-col>

      <!-- Recent signals -->
      <a-col :xs="24" :lg="12">
        <a-card title="Recent Signals" :extra="store.signals.length + ' total'">
          <a-list
            size="small"
            :data-source="store.signals.slice(0, 8)"
            :locale="{ emptyText: 'No signals yet' }"
          >
            <template #renderItem="{ item }">
              <a-list-item>
                <a-list-item-meta
                  :title="item.model || item.protocol || 'Unknown'"
                  :description="item._ts"
                />
                <template #extra>
                  <a-tag v-if="item.rssi" color="blue">{{ item.rssi }} dB</a-tag>
                </template>
              </a-list-item>
            </template>
          </a-list>
        </a-card>
      </a-col>
    </a-row>
  </div>
</template>

<script setup>
import { h, computed } from 'vue'
import {
  CheckCircleOutlined, StopOutlined, RadarChartOutlined,
  ClockCircleOutlined, WifiOutlined, PlayCircleOutlined,
  PauseCircleOutlined, ClearOutlined,
} from '@ant-design/icons-vue'
import { useAppStore } from '../store/index.js'
import { useWebSocket } from '../composables/useWebSocket.js'

const store = useAppStore()
const { send } = useWebSocket()

const totalCount = computed(() =>
  store.recentProtocols.reduce((s, p) => s + p.count, 0) || 1
)

function formatUptime(secs) {
  if (!secs) return '0s'
  const h = Math.floor(secs / 3600)
  const m = Math.floor((secs % 3600) / 60)
  const s = Math.floor(secs % 60)
  if (h > 0) return `${h}h ${m}m ${s}s`
  if (m > 0) return `${m}m ${s}s`
  return `${s}s`
}

async function startRtl433() {
  try {
    const res = await fetch('/api/start', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(store.config),
    })
    const data = await res.json()
    if (data.success) store.running = true
  } catch {}
}

async function stopRtl433() {
  try {
    await fetch('/api/stop', { method: 'POST' })
    store.running = false
  } catch {}
}

function clearData() {
  store.clearSignals()
}
</script>

<style scoped>
.dashboard {}
.proto-row {
  display: flex;
  align-items: center;
  margin-bottom: 8px;
}
.proto-name {
  width: 160px;
  font-size: 13px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.empty-hint {
  color: #999;
  text-align: center;
  padding: 24px 0;
}
</style>
