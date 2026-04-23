<template>
  <div>
    <a-card title="Signal Log">
      <template #extra>
        <a-space>
          <a-button size="small" @click="exportLog">Export</a-button>
          <a-button size="small" danger @click="store.logMessages = []">Clear</a-button>
          <a-switch v-model:checked="autoScroll" checked-children="Auto" un-checked-children="Scroll" />
        </a-space>
      </template>

      <div
        ref="logContainer"
        class="log-container"
        @scroll="onScroll"
      >
        <div
          v-for="(entry, idx) in store.logMessages"
          :key="idx"
          class="log-line"
          :class="logClass(entry.msg)"
        >
          <span class="log-ts">{{ entry.ts }}</span>
          <span class="log-msg">{{ entry.msg }}</span>
        </div>
        <div v-if="store.logMessages.length === 0" class="empty-log">
          No log messages yet. Start rtl_433 to see output.
        </div>
      </div>
    </a-card>
  </div>
</template>

<script setup>
import { ref, watch, nextTick } from 'vue'
import { useAppStore } from '../store/index.js'

const store       = useAppStore()
const logContainer = ref(null)
const autoScroll  = ref(true)

watch(() => store.logMessages.length, async () => {
  if (autoScroll.value) {
    await nextTick()
    if (logContainer.value) {
      logContainer.value.scrollTop = 0
    }
  }
})

function onScroll() {
  // Disable autoscroll if user scrolls up
  if (logContainer.value) {
    autoScroll.value = logContainer.value.scrollTop < 20
  }
}

function logClass(msg) {
  if (!msg) return ''
  try {
    const j = JSON.parse(msg)
    if (j.type === 'log') return 'log-info'
    return 'log-signal'
  } catch {
    return 'log-info'
  }
}

function exportLog() {
  const content = store.logMessages
    .map(e => `${e.ts}  ${e.msg}`)
    .join('\n')
  const blob = new Blob([content], { type: 'text/plain' })
  const url  = URL.createObjectURL(blob)
  const a    = document.createElement('a')
  a.href     = url
  a.download = `rtl433_log_${new Date().toISOString().replace(/[:.]/g, '-')}.txt`
  a.click()
  URL.revokeObjectURL(url)
}
</script>

<style scoped>
.log-container {
  height: calc(100vh - 220px);
  min-height: 300px;
  overflow-y: auto;
  background: #1e1e1e;
  border-radius: 4px;
  padding: 8px;
  font-family: 'Courier New', monospace;
  font-size: 12px;
}
.log-line {
  display: flex;
  gap: 12px;
  padding: 2px 4px;
  border-radius: 2px;
  word-break: break-all;
}
.log-line:hover { background: rgba(255,255,255,0.05); }
.log-ts   { color: #858585; white-space: nowrap; flex-shrink: 0; }
.log-msg  { color: #d4d4d4; }
.log-signal .log-msg { color: #4ec9b0; }
.log-info   .log-msg { color: #9cdcfe; }
.empty-log  { color: #555; text-align: center; padding: 40px; }
</style>
