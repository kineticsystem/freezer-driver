import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

// The development server, with hot reload, on port 5175. The container shares
// the host's network, where another development server may take Vite's
// default, 5173. The page reaches the node through rosbridge, on port 9092, in
// development as when the launch file serves it.
export default defineConfig({
  plugins: [react()],
  server: { host: '0.0.0.0', port: 5175, strictPort: true },
  build: { outDir: 'dist', emptyOutDir: true },
});
