let enginePromise;

function loadEngine() {
  if (!enginePromise) {
    importScripts("engine/openobfuscator.js");
    enginePromise = self.createOpenObfuscator({
      locateFile(path) {
        return new URL(`engine/${path}`, self.location.origin + "/").href;
      }
    });
  }
  return enginePromise;
}

loadEngine()
  .then(() => self.postMessage({ type: "ready" }))
  .catch((error) => self.postMessage({ type: "startup-error", error: error instanceof Error ? error.message : String(error) }));

self.addEventListener("message", async (event) => {
  const { id, source, language, preset } = event.data;
  const startedAt = performance.now();
  try {
    const engine = await loadEngine();
    const sourceBytes = new TextEncoder().encode(source).length;
    const code = engine.ccall(
      "oo_obfuscate",
      "string",
      ["string", "number", "number", "number", "number"],
      [source, sourceBytes, language === "lua" ? 0 : 1, crypto.getRandomValues(new Uint32Array(1))[0], preset]
    );
    if (!code) {
      const detail = engine.ccall("oo_last_error", "string", [], []);
      throw new Error(detail || "Native engine returned no output");
    }
    self.postMessage({ id, code, duration: performance.now() - startedAt, engine: "OpenObfuscator C++ V1.2 / WASM" });
  } catch (error) {
    self.postMessage({ id, error: error instanceof Error ? error.message : String(error) });
  }
});
