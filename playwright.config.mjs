import { defineConfig } from '@playwright/test';

const port = Number(process.env.E2E_PORT ?? 8174);

export default defineConfig({
  testDir: 'tests/e2e',
  use: { baseURL: `http://localhost:${port}/` },
  webServer: { command: 'node tools/serve.mjs', env: { PORT: String(port) }, port, reuseExistingServer: false },
});
