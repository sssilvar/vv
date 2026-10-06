import { defineConfig } from "vite";
import { resolve } from "node:path";
import { readFileSync } from "node:fs";

export default defineConfig({
  base: "./",
  plugins: [
    {
      name: "vv-runtime",
      apply: "build",
      enforce: "pre",
      resolveId(id) {
        if (id === "./wasm/vv.js") return { id: "./vv-runtime.js", external: true };
      },
      generateBundle() {
        this.emitFile({
          type: "asset",
          fileName: "vv-runtime.js",
          source: readFileSync("src/wasm/vv.js"),
        });
      },
    },
  ],
  build: {
    emptyOutDir: true,
    assetsInlineLimit: 0,
    lib: {
      entry: { vv: resolve("src/index.tsx"), engine: resolve("src/engine.tsx") },
      formats: ["es"],
      fileName: (_format, entry) => `${entry}.js`,
    },
    rollupOptions: { external: ["react", "react/jsx-runtime"] },
  },
});
