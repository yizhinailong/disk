import { createApp } from 'vue'
import { createPinia } from 'pinia'
import App from './App.vue'
import router from './router'
import { useAuthStore } from './stores/auth'
import 'element-plus/dist/index.css'

const pinia = createPinia()
const app = createApp(App)
app.use(pinia)
app.use(router)

useAuthStore().loadTokensFromStorage()

app.mount('#app')
