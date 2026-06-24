import { defineConfig } from 'vite'
import webfontDownload from 'vite-plugin-webfont-dl'

export default defineConfig({
  plugins: [webfontDownload()],
  base: '/',
  build: {
    outDir: 'dist',
    assetsDir: 'assets',
    target: 'esnext',
    minify: true,
    emptyOutDir: true,
    rollupOptions: {
      output: {
        entryFileNames: 'assets/[name]-[hash].js',
        chunkFileNames: 'assets/[name]-[hash].js',
        assetFileNames: (assetInfo) => {
          const name = assetInfo.name || '';
          if (name.endsWith('.css')) {
            return 'assets/[name]-[hash].[ext]';
          }
          return 'assets/[hash].[ext]';
        },
        manualChunks: undefined
      }
    }
  }
})
