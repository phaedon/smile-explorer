import {svelte} from '@sveltejs/vite-plugin-svelte';
import {defineConfig} from 'vite';

export default defineConfig({
  plugins: [svelte()],
  // Relative asset URLs, so the built site can be served from any path
  // (e.g. GitHub Pages under /smile-explorer/).
  base: './',
  build: {
    // The engine is loaded with top-level await.
    target: 'es2022',
  },
});
