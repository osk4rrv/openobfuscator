import { access, readFile } from "node:fs/promises";

const required = [
  "dist/index.html",
  "dist/styles.css",
  "dist/app.js",
  "dist/mark.svg"
];
await Promise.all(required.map((file) => access(file)));
const html = await readFile("dist/index.html", "utf8");
for (const marker of ["OpenObfuscator", 'name="language"', 'id="source-code"', 'id="obfuscate-button"']) {
  if (!html.includes(marker)) throw new Error(`Missing required markup: ${marker}`);
}
console.log("Dual-language web build checks passed.");
