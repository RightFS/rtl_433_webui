import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const useAppStore = defineStore('app', () => {
  const connected    = ref(false)
  const running      = ref(false)
  const signalCount  = ref(0)
  const uptime       = ref(0)
  const signals      = ref([])
  const logMessages  = ref([])
  const config       = ref({
    device:       '0',
    frequency:    433920000,
    sample_rate:  250000,
    gain:         'auto',
    protocols:    [],
    squelch:      0,
    hop_interval: 0,
    rtl433_path:  'rtl_433',
    extra_args:   '',
  })

  const maxSignals  = 1000
  const maxLogLines = 2000

  function addSignal(data) {
    const entry = { ...data, _id: Date.now() + Math.random(), _ts: new Date().toISOString() }
    signals.value.unshift(entry)
    if (signals.value.length > maxSignals) signals.value.length = maxSignals
    signalCount.value++
  }

  function addLog(msg) {
    logMessages.value.unshift({ msg, ts: new Date().toISOString() })
    if (logMessages.value.length > maxLogLines) logMessages.value.length = maxLogLines
  }

  function setStatus(s) {
    running.value     = s.running  ?? running.value
    signalCount.value = s.signal_count ?? signalCount.value
    uptime.value      = s.uptime   ?? uptime.value
  }

  function setConfig(c) {
    config.value = { ...config.value, ...c }
  }

  function clearSignals() {
    signals.value   = []
    signalCount.value = 0
  }

  const recentProtocols = computed(() => {
    const counts = {}
    signals.value.slice(0, 200).forEach(s => {
      const proto = s.protocol || s.model || 'Unknown'
      counts[proto] = (counts[proto] || 0) + 1
    })
    return Object.entries(counts)
      .sort((a, b) => b[1] - a[1])
      .slice(0, 10)
      .map(([name, count]) => ({ name, count }))
  })

  return {
    connected, running, signalCount, uptime, signals, logMessages, config,
    addSignal, addLog, setStatus, setConfig, clearSignals, recentProtocols,
  }
})
