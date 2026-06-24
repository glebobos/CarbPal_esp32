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
    rollupOptions: {
      output: {
        entryFileNames: 'assets/[name].js',
        chunkFileNames: 'assets/[name].js',
        assetFileNames: (assetInfo) => {
          const name = assetInfo.name || '';
          if (name.endsWith('.woff2') || name.endsWith('.woff') || name.endsWith('.ttf') || name.endsWith('.eot')) {
            return 'assets/[hash].[ext]';
          }
          return 'assets/[name].[ext]';
        },
        manualChunks: undefined
      }
    }
  }
})
