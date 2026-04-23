<template>
  <div>
    <a-card title="rtl_433 Settings">
      <a-form
        :model="form"
        :label-col="{ span: 6 }"
        :wrapper-col="{ span: 16 }"
        @finish="saveConfig"
      >
        <!-- Device -->
        <a-form-item label="Device" name="device">
          <a-input-group compact>
            <a-input
              v-model:value="form.device"
              placeholder="0, driver=plutosdr, etc."
              style="width:calc(100% - 96px)"
            />
            <a-button @click="loadDevices" :loading="loadingDevices" style="width:96px">
              Scan
            </a-button>
          </a-input-group>
          <div v-if="devices.length" style="margin-top:8px">
            <a-tag
              v-for="d in devices"
              :key="d.index"
              style="cursor:pointer;margin-bottom:4px"
              @click="form.device = d.index"
            >{{ d.index }}: {{ d.name }}</a-tag>
          </div>
        </a-form-item>

        <!-- Frequency -->
        <a-form-item label="Frequency (Hz)" name="frequency">
          <a-input-number
            v-model:value="form.frequency"
            :min="1000000"
            :max="6000000000"
            :step="100000"
            style="width:100%"
          />
          <div style="color:#999;font-size:12px">
            Common: 433.92 MHz = 433920000, 315 MHz = 315000000, 868 MHz = 868000000
          </div>
        </a-form-item>

        <!-- Sample rate -->
        <a-form-item label="Sample Rate (Hz)" name="sample_rate">
          <a-select v-model:value="form.sample_rate" style="width:100%">
            <a-select-option :value="250000">250,000</a-select-option>
            <a-select-option :value="1000000">1,000,000</a-select-option>
            <a-select-option :value="2048000">2,048,000</a-select-option>
            <a-select-option :value="2400000">2,400,000</a-select-option>
          </a-select>
        </a-form-item>

        <!-- Gain -->
        <a-form-item label="Gain" name="gain">
          <a-radio-group v-model:value="gainMode">
            <a-radio-button value="auto">Auto</a-radio-button>
            <a-radio-button value="manual">Manual</a-radio-button>
          </a-radio-group>
          <a-input-number
            v-if="gainMode === 'manual'"
            v-model:value="manualGainValue"
            placeholder="e.g. 40"
            style="width:120px;margin-left:8px"
            @change="v => form.gain = String(v)"
          />
        </a-form-item>

        <!-- Squelch -->
        <a-form-item label="Squelch Level" name="squelch">
          <a-slider
            v-model:value="form.squelch"
            :min="0"
            :max="50"
            :step="1"
            :marks="{ 0: 'Off', 10: '10', 20: '20', 30: '30', 40: '40', 50: '50' }"
            style="margin-bottom:16px"
          />
        </a-form-item>

        <!-- Hop interval -->
        <a-form-item label="Hop Interval (s)" name="hop_interval">
          <a-input-number
            v-model:value="form.hop_interval"
            :min="0"
            :max="3600"
            style="width:120px"
          />
          <span style="color:#999;margin-left:8px">0 = disabled</span>
        </a-form-item>

        <!-- rtl_433 binary path -->
        <a-form-item label="rtl_433 Path" name="rtl433_path">
          <a-input v-model:value="form.rtl433_path" placeholder="rtl_433" />
        </a-form-item>

        <!-- Extra args -->
        <a-form-item label="Extra Arguments" name="extra_args">
          <a-input v-model:value="form.extra_args" placeholder="-q -v ..." />
        </a-form-item>

        <!-- Protocols -->
        <a-form-item label="Protocols">
          <a-button size="small" @click="loadProtocols" :loading="loadingProtocols">
            Load Protocol List
          </a-button>
          <div style="margin-top:8px;color:#999;font-size:12px">
            Selected (empty = all): {{ form.protocols.join(', ') || 'All' }}
          </div>
          <div v-if="protocols.length" style="max-height:300px;overflow-y:auto;margin-top:8px;border:1px solid #d9d9d9;border-radius:4px;padding:8px">
            <a-checkbox-group v-model:value="form.protocols">
              <div v-for="p in protocols" :key="p.id" style="margin-bottom:4px">
                <a-checkbox :value="p.id">
                  [{{ p.id }}] {{ p.name }}
                </a-checkbox>
              </div>
            </a-checkbox-group>
          </div>
        </a-form-item>

        <a-form-item :wrapper-col="{ offset: 6, span: 16 }">
          <a-space>
            <a-button type="primary" html-type="submit">Save & Apply</a-button>
            <a-button @click="resetForm">Reset</a-button>
          </a-space>
        </a-form-item>
      </a-form>
    </a-card>
  </div>
</template>

<script setup>
import { ref, reactive, watch, onMounted } from 'vue'
import { message } from 'ant-design-vue'
import { useAppStore } from '../store/index.js'

const store = useAppStore()

const form = reactive({ ...store.config })
const gainMode = ref(form.gain === 'auto' ? 'auto' : 'manual')
const manualGainValue = ref(form.gain === 'auto' ? 40 : Number(form.gain) || 40)
const devices  = ref([])
const protocols = ref([])
const loadingDevices   = ref(false)
const loadingProtocols = ref(false)

watch(gainMode, v => {
  if (v === 'auto') form.gain = 'auto'
  else form.gain = String(manualGainValue.value)
})

onMounted(() => {
  Object.assign(form, store.config)
  gainMode.value = form.gain === 'auto' ? 'auto' : 'manual'
})

async function loadDevices() {
  loadingDevices.value = true
  try {
    const res  = await fetch('/api/devices')
    devices.value = await res.json()
  } catch {
    message.error('Failed to load devices')
  } finally {
    loadingDevices.value = false
  }
}

async function loadProtocols() {
  loadingProtocols.value = true
  try {
    const res  = await fetch('/api/protocols')
    protocols.value = await res.json()
  } catch {
    message.error('Failed to load protocols')
  } finally {
    loadingProtocols.value = false
  }
}

async function saveConfig() {
  store.setConfig({ ...form })
  try {
    await fetch('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(form),
    })
    message.success('Config saved')
  } catch {
    message.error('Failed to save config')
  }
}

function resetForm() {
  Object.assign(form, store.config)
}
</script>
