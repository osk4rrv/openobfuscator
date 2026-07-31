import { cp, mkdir, rm } from "node:fs/promises";
import { build } from "esbuild";

await rm("dist", { recursive: true, force: true });
await mkdir("dist", { recursive: true });
await cp("public", "dist", { recursive: true });

await build({
  entryPoints: { app: "src/client.js" },
  outdir: "dist",
  bundle: true,
  minify: true,
  format: "iife",
  target: ["es2022"],
  legalComments: "none"
});
