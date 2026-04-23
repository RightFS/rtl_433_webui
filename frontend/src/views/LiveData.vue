<template>
  <div>
    <a-card title="Live Signal Data" style="margin-bottom:16px">
      <template #extra>
        <a-space>
          <a-input-search
            v-model:value="filterText"
            placeholder="Filter..."
            style="width:200px"
            allow-clear
          />
          <a-button @click="store.clearSignals" size="small">Clear</a-button>
          <a-badge :count="store.signals.length" :overflow-count="9999" />
        </a-space>
      </template>

      <a-table
        :columns="columns"
        :data-source="filteredSignals"
        :row-key="r => r._id"
        :scroll="{ x: 800, y: 500 }"
        size="small"
        :pagination="{ pageSize: 50, showSizeChanger: true, showTotal: t => `${t} signals` }"
        @row-click="showDetail"
      />
    </a-card>

    <!-- Detail drawer -->
    <a-drawer
      v-model:open="detailOpen"
      title="Signal Detail"
      :width="400"
      placement="right"
    >
      <a-descriptions v-if="selectedSignal" :column="1" bordered size="small">
        <a-descriptions-item
          v-for="(val, key) in flatSignal"
          :key="key"
          :label="key"
        >{{ val }}</a-descriptions-item>
      </a-descriptions>
    </a-drawer>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useAppStore } from '../store/index.js'

const store      = useAppStore()
const filterText = ref('')
const detailOpen = ref(false)
const selectedSignal = ref(null)

const columns = [
  { title: 'Time',     dataIndex: '_ts',      key: 'ts',   width: 180, sorter: (a,b) => a._ts.localeCompare(b._ts) },
  { title: 'Model',    dataIndex: 'model',    key: 'model', width: 180, sorter: (a,b) => (a.model||'').localeCompare(b.model||'') },
  { title: 'Protocol', dataIndex: 'protocol', key: 'proto', width: 100, sorter: (a,b) => (a.protocol||0)-(b.protocol||0) },
  { title: 'ID',       dataIndex: 'id',       key: 'id',   width: 80  },
  { title: 'Channel',  dataIndex: 'channel',  key: 'ch',   width: 80  },
  { title: 'Temp (°C)',dataIndex: 'temperature_C', key: 'temp', width: 100,
    customRender: ({value}) => value != null ? value.toFixed(1) : '-'
  },
  { title: 'Humidity', dataIndex: 'humidity', key: 'hum',  width: 90,
    customRender: ({value}) => value != null ? value + '%' : '-'
  },
  { title: 'RSSI',     dataIndex: 'rssi',     key: 'rssi', width: 90,
    customRender: ({value}) => value != null ? value.toFixed(1) + ' dB' : '-'
  },
  { title: 'Battery', dataIndex: 'battery_ok', key: 'bat', width: 80,
    customRender: ({value}) => value == null ? '-' : (value ? '✅' : '❌')
  },
]

const filteredSignals = computed(() => {
  if (!filterText.value) return store.signals
  const f = filterText.value.toLowerCase()
  return store.signals.filter(s => JSON.stringify(s).toLowerCase().includes(f))
})

const flatSignal = computed(() => {
  if (!selectedSignal.value) return {}
  const out = {}
  for (const [k, v] of Object.entries(selectedSignal.value)) {
    if (!k.startsWith('_')) out[k] = typeof v === 'object' ? JSON.stringify(v) : String(v)
  }
  return out
})

function showDetail(record) {
  selectedSignal.value = record
  detailOpen.value     = true
}
</script>
