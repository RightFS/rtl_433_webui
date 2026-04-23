import { createRouter, createWebHashHistory } from 'vue-router'
import Dashboard  from '../views/Dashboard.vue'
import LiveData   from '../views/LiveData.vue'
import Settings   from '../views/Settings.vue'
import SignalLog  from '../views/SignalLog.vue'

const routes = [
  { path: '/',          redirect: '/dashboard' },
  { path: '/dashboard', component: Dashboard,  name: 'Dashboard' },
  { path: '/live',      component: LiveData,   name: 'LiveData'  },
  { path: '/settings',  component: Settings,   name: 'Settings'  },
  { path: '/log',       component: SignalLog,  name: 'SignalLog' },
]

export default createRouter({
  history: createWebHashHistory(),
  routes,
})
