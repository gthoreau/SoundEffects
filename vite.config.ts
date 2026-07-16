import { defineConfig } from "vite";

// The shell is plain TypeScript against the DOM — vite is here to serve modules
// and bundle the resource JSON, nothing more. No framework plugin, by design.
export default defineConfig({
  root: ".",
  build: { outDir: "dist", target: "es2022" },
});
