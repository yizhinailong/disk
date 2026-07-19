import { defineConfig, devices } from '@playwright/test'

const baseURL = process.env.DISK_E2E_BASE_URL ?? 'http://localhost:5173'
const webServerCommand = process.env.DISK_E2E_WEB_SERVER_COMMAND ?? 'bun run dev'

export default defineConfig({
  testDir: './e2e',
  outputDir: process.env.DISK_E2E_OUTPUT_DIR ?? 'test-results',
  fullyParallel: false,
  retries: 0,
  timeout: 30_000,
  expect: {
    timeout: 10_000,
  },
  use: {
    baseURL,
    trace: 'on-first-retry',
    screenshot: 'only-on-failure',
    actionTimeout: 10_000,
    navigationTimeout: 15_000,
  },
  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'] },
    },
  ],
  webServer: {
    command: webServerCommand,
    url: baseURL,
    reuseExistingServer: process.env.DISK_E2E_REUSE_WEB_SERVER !== '0',
    timeout: 30_000,
  },
})
