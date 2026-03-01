import { createApp } from 'vue'
import { createPinia } from 'pinia'
import App from './App.vue'
import './styles/base.css'
import './styles/app-shell.css'
import './styles/calibration.css'

const app = createApp(App)
app.use(createPinia())
app.mount('#app')
