import { ref, onUnmounted } from 'vue'
import { useAppStore } from '../store/index.js'

export function useWebSocket() {
  const store  = useAppStore()
  const wsRef  = ref(null)
  const reconnectTimer = ref(null)
  const reconnectDelay = 3000

  function connect() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws'
    const url   = `${proto}://${location.host}/ws`

    const ws = new WebSocket(url)
    wsRef.value = ws

    ws.onopen = () => {
      store.connected = true
      ws.send(JSON.stringify({ cmd: 'get_status' }))
    }

    ws.onclose = () => {
      store.connected = false
      scheduleReconnect()
    }

    ws.onerror = () => {
      store.connected = false
    }

    ws.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data)
        if (data.type === 'status') {
          store.setStatus(data)
        } else if (data.type === 'log') {
          store.addLog(data.message)
        } else if (data.type === 'rpc_result') {
          // handled by caller if needed
        } else {
          // It's a signal event
          store.addSignal(data)
          store.addLog(event.data)
        }
      } catch {
        store.addLog(event.data)
      }
    }
  }

  function scheduleReconnect() {
    if (reconnectTimer.value) return
    reconnectTimer.value = setTimeout(() => {
      reconnectTimer.value = null
      connect()
    }, reconnectDelay)
  }

  function send(obj) {
    if (wsRef.value && wsRef.value.readyState === WebSocket.OPEN) {
      wsRef.value.send(JSON.stringify(obj))
    }
  }

  function disconnect() {
    if (reconnectTimer.value) {
      clearTimeout(reconnectTimer.value)
      reconnectTimer.value = null
    }
    if (wsRef.value) {
      wsRef.value.onclose = null
      wsRef.value.close()
      wsRef.value = null
    }
  }

  onUnmounted(disconnect)

  return { connect, disconnect, send, wsRef }
}
