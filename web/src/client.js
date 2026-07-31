const MAX_SOURCE_BYTES = 500_000;
const samples = {
  javascript: `const cart = [
  { name: "Keyboard", price: 89 },
  { name: "Mouse", price: 42 }
];

function calculateTotal(items) {
  return items.reduce((sum, item) => sum + item.price, 0);
}

console.log(\`Total: $\${calculateTotal(cart)}\`);`,
  lua: `local cart = {
  { name = "Keyboard", price = 89 },
  { name = "Mouse", price = 42 }
}

local function calculateTotal(items)
  local total = 0
  for _, item in ipairs(items) do total = total + item.price end
  return total
end

print("Total: $" .. calculateTotal(cart))`
};

const rotatingLabels = {
  javascript: "JavaScript",
  lua: "LuaJIT"
};

const elements = {
  heroLanguage: document.querySelector("#hero-language"),
  source: document.querySelector("#source-code"),
  output: document.querySelector("#output-code"),
  sourceLabel: document.querySelector("#source-label"),
  outputLabel: document.querySelector("#output-label"),
  sourceLines: document.querySelector("#source-lines"),
  sourceSize: document.querySelector("#source-size"),
  outputSize: document.querySelector("#output-size"),
  outputStatus: document.querySelector("#output-status"),
  outputEmpty: document.querySelector("#output-empty"),
  message: document.querySelector("#tool-message"),
  obfuscate: document.querySelector("#obfuscate-button"),
  copy: document.querySelector("#copy-button"),
  download: document.querySelector("#download-button"),
  upload: document.querySelector("#upload-button"),
  clear: document.querySelector("#clear-button"),
  fileInput: document.querySelector("#file-input"),
  dropZone: document.querySelector("#drop-zone"),
  preset: document.querySelector("#preset-select"),
  sample: document.querySelector("#sample-button")
};

let worker;
let engineReady = false;
let activeJob = 0;
let activeRequest = null;
let outputFileName = "openobfuscator-output.js";
let heroLanguage = "javascript";

function selectedLanguage() {
  return document.querySelector('input[name="language"]:checked')?.value || "javascript";
}

function selectedPreset() {
  return { standard: 0, hardened: 1, maximum: 2 }[elements.preset.value] ?? 0;
}

function byteLength(value) {
  return new TextEncoder().encode(value).length;
}

function formatBytes(bytes) {
  if (bytes < 1000) return `${bytes} B`;
  if (bytes < 1_000_000) return `${(bytes / 1000).toFixed(bytes >= 10_000 ? 0 : 1)} KB`;
  return `${(bytes / 1_000_000).toFixed(1)} MB`;
}

function updateSourceStats() {
  const value = elements.source.value;
  const lines = value ? value.split("\n").length : 0;
  elements.sourceLines.textContent = `${lines} ${lines === 1 ? "line" : "lines"}`;
  elements.sourceSize.textContent = formatBytes(byteLength(value));
}

function setMessage(text, state = "ready") {
  elements.message.classList.toggle("is-error", state === "error");
  elements.message.classList.toggle("is-busy", state === "busy");
  elements.message.innerHTML = "<span></span>";
  elements.message.append(document.createTextNode(text));
}

function setBusy(busy) {
  elements.obfuscate.disabled = busy || !engineReady;
  elements.obfuscate.querySelector("span").textContent = busy ? "Running native engine…" : "Obfuscate code";
  if (busy) {
    elements.outputStatus.textContent = "Processing";
    setMessage("Generating a randomized source VM on this device", "busy");
  }
}

function resetOutput() {
  elements.output.value = "";
  elements.outputEmpty.hidden = false;
  elements.outputStatus.textContent = "Waiting for input";
  elements.outputSize.textContent = "—";
  elements.copy.disabled = true;
  elements.download.disabled = true;
}

function invalidateActiveJob() {
  activeJob += 1;
  activeRequest = null;
  setBusy(false);
  resetOutput();
}

function createWorker() {
  try {
    const instance = new Worker("obfuscation-worker.js");
    instance.addEventListener("message", (event) => {
      if (event.data.type === "ready") {
        engineReady = true;
        setBusy(false);
        setMessage("Native OpenObfuscator V1.2 engine ready");
        return;
      }
      if (event.data.type === "startup-error") {
        setMessage(`Native engine failed to load: ${event.data.error}`, "error");
        return;
      }
      handleWorkerResult(event.data);
    });
    instance.addEventListener("error", () => {
      setBusy(false);
      setMessage("The native WebAssembly engine could not start.", "error");
    });
    return instance;
  } catch {
    setMessage("The native WebAssembly engine requires an HTTP origin.", "error");
    return null;
  }
}

function handleWorkerResult({ id, code, error, duration }) {
  if (id !== activeJob || !activeRequest) return;
  const request = activeRequest;
  activeRequest = null;
  setBusy(false);
  if (error) {
    elements.outputStatus.textContent = "Could not process";
    setMessage(error, "error");
    return;
  }
  elements.output.value = code;
  elements.outputEmpty.hidden = true;
  const outputBytes = byteLength(code);
  const change = Math.round(((outputBytes - request.sourceBytes) / Math.max(1, request.sourceBytes)) * 100);
  elements.outputStatus.textContent = `${Math.max(1, Math.round(duration))} ms · +${Math.max(0, change)}% size`;
  elements.outputSize.textContent = formatBytes(outputBytes);
  outputFileName = request.outputFileName;
  elements.copy.disabled = false;
  elements.download.disabled = false;
  setMessage("Protected by the native V1.2 engine — nothing was uploaded");
}

function setLanguage(language, replaceSource = true) {
  const isLua = language === "lua";
  document.querySelector(`input[name="language"][value="${language}"]`).checked = true;
  document.querySelector("#standard-preset-label").textContent = isLua ? "Standard" : "Source VM";
  Array.from(elements.preset.options).forEach((option) => {
    option.disabled = !isLua && option.value !== "standard";
  });
  if (!isLua) elements.preset.value = "standard";
  elements.sourceLabel.textContent = isLua ? "Source LuaJIT" : "Source JavaScript";
  elements.outputLabel.textContent = isLua ? "Protected LuaJIT" : "Protected JavaScript";
  elements.source.setAttribute("aria-label", isLua ? "Source LuaJIT" : "Source JavaScript");
  elements.fileInput.accept = isLua ? ".lua,text/x-lua" : ".js,.cjs,text/javascript,application/javascript";
  outputFileName = isLua ? "openobfuscator-output.lua" : "openobfuscator-output.js";
  if (replaceSource) elements.source.value = samples[language];
  invalidateActiveJob();
  updateSourceStats();
  setMessage(`${isLua ? "LuaJIT" : "JavaScript"} source VM selected`);
}

function obfuscate() {
  const source = elements.source.value;
  const size = byteLength(source);
  if (!source.trim()) {
    setMessage("Add source code before obfuscating.", "error");
    elements.source.focus();
    return;
  }
  if (size > MAX_SOURCE_BYTES) {
    setMessage(`The browser build accepts up to ${formatBytes(MAX_SOURCE_BYTES)}.`, "error");
    return;
  }
  activeJob += 1;
  const language = selectedLanguage();
  activeRequest = {
    sourceBytes: size,
    outputFileName: language === "lua" ? "openobfuscator-output.lua" : "openobfuscator-output.js"
  };
  setBusy(true);
  worker.postMessage({ id: activeJob, source, language, preset: selectedPreset() });
}

function loadFile(file) {
  if (!file) return;
  const language = file.name.toLowerCase().endsWith(".lua") ? "lua" : file.name.match(/\.(?:js|cjs)$/i) ? "javascript" : null;
  if (!language) {
    setMessage("Choose a .js, .cjs, or .lua file.", "error");
    return;
  }
  if (file.size > MAX_SOURCE_BYTES) {
    setMessage(`That file exceeds the ${formatBytes(MAX_SOURCE_BYTES)} browser limit.`, "error");
    return;
  }
  const reader = new FileReader();
  reader.addEventListener("load", () => {
    setLanguage(language, false);
    elements.source.value = String(reader.result || "");
    outputFileName = file.name.replace(/\.(js|cjs|lua)$/i, ".protected.$1");
    updateSourceStats();
    setMessage(`${file.name} loaded locally`);
  });
  reader.addEventListener("error", () => setMessage("The file could not be read.", "error"));
  reader.readAsText(file);
}

async function copyOutput() {
  if (!elements.output.value) return;
  try { await navigator.clipboard.writeText(elements.output.value); }
  catch { elements.output.select(); document.execCommand("copy"); }
  const label = elements.copy.querySelector("span");
  label.textContent = "Copied";
  setTimeout(() => { label.textContent = "Copy"; }, 1200);
}

function downloadOutput() {
  if (!elements.output.value) return;
  const blob = new Blob([elements.output.value], { type: "text/plain;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const link = Object.assign(document.createElement("a"), { href: url, download: outputFileName });
  document.body.append(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
}

function rotateHero() {
  heroLanguage = heroLanguage === "javascript" ? "lua" : "javascript";
  elements.heroLanguage.classList.add("is-changing");
  setTimeout(() => {
    elements.heroLanguage.textContent = rotatingLabels[heroLanguage];
    elements.heroLanguage.classList.remove("is-changing");
  }, 180);
}

document.querySelectorAll('input[name="language"]').forEach((input) => input.addEventListener("change", () => setLanguage(input.value)));
elements.preset.addEventListener("change", () => {
  invalidateActiveJob();
  setMessage(`${elements.preset.options[elements.preset.selectedIndex].text} profile selected`);
});
elements.source.addEventListener("input", () => {
  updateSourceStats();
  if (activeRequest || elements.output.value) invalidateActiveJob();
});
elements.source.addEventListener("keydown", (event) => {
  if (event.key === "Tab") {
    event.preventDefault();
    elements.source.setRangeText("  ", elements.source.selectionStart, elements.source.selectionEnd, "end");
    updateSourceStats();
    if (activeRequest || elements.output.value) invalidateActiveJob();
  }
  if ((event.ctrlKey || event.metaKey) && event.key === "Enter") { event.preventDefault(); obfuscate(); }
});
elements.obfuscate.addEventListener("click", obfuscate);
elements.copy.addEventListener("click", copyOutput);
elements.download.addEventListener("click", downloadOutput);
elements.upload.addEventListener("click", () => elements.fileInput.click());
elements.sample.addEventListener("click", () => {
  elements.source.value = samples[selectedLanguage()];
  invalidateActiveJob();
  updateSourceStats();
  setMessage(`${selectedLanguage() === "lua" ? "LuaJIT" : "JavaScript"} example loaded`);
  elements.source.focus();
});
elements.fileInput.addEventListener("change", () => loadFile(elements.fileInput.files[0]));
elements.clear.addEventListener("click", () => { elements.source.value = ""; invalidateActiveJob(); updateSourceStats(); elements.source.focus(); });
["dragenter", "dragover"].forEach((type) => elements.dropZone.addEventListener(type, (event) => { event.preventDefault(); elements.dropZone.classList.add("is-dragging"); }));
["dragleave", "drop"].forEach((type) => elements.dropZone.addEventListener(type, (event) => { event.preventDefault(); elements.dropZone.classList.remove("is-dragging"); }));
elements.dropZone.addEventListener("drop", (event) => loadFile(event.dataTransfer.files[0]));

document.querySelector("#current-year").textContent = new Date().getFullYear();
updateSourceStats();
resetOutput();
worker = createWorker();
setInterval(rotateHero, 3200);
