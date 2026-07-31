const MAX_SOURCE_BYTES = 500_000;
const MAX_REQUEST_BYTES = 3_100_000;
const ORIGIN_TIMEOUT_MS = 25_000;

const SECURITY_HEADERS = {
  "Content-Security-Policy": "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'self'; form-action 'self'; frame-ancestors 'none'",
  "Referrer-Policy": "strict-origin-when-cross-origin",
  "X-Content-Type-Options": "nosniff",
  "X-Frame-Options": "DENY",
  "Permissions-Policy": "camera=(), microphone=(), geolocation=(), payment=(), usb=()",
  "Cross-Origin-Opener-Policy": "same-origin"
};

function apiHeaders(extra = {}) {
  return { ...SECURITY_HEADERS, "Cache-Control": "no-store", ...extra };
}

function errorResponse(status, error) {
  return Response.json({ error }, { status, headers: apiHeaders() });
}

function configured(env) {
  return typeof env.OBFUSCATOR_API_URL === "string" &&
    env.OBFUSCATOR_API_URL.startsWith("https://") &&
    typeof env.OBFUSCATOR_API_TOKEN === "string" &&
    env.OBFUSCATOR_API_TOKEN.length >= 32;
}

async function readUtf8Body(request, byteLimit) {
  if (!request.body) return "";
  const reader = request.body.getReader();
  const chunks = [];
  let total = 0;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    total += value.byteLength;
    if (total > byteLimit) {
      await reader.cancel();
      return null;
    }
    chunks.push(value);
  }
  const body = new Uint8Array(total);
  let offset = 0;
  for (const chunk of chunks) {
    body.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return new TextDecoder("utf-8", { fatal: true }).decode(body);
}

async function originFetch(env, path, init = {}) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), ORIGIN_TIMEOUT_MS);
  try {
    return await fetch(`${env.OBFUSCATOR_API_URL.replace(/\/$/, "")}${path}`, {
      ...init,
      signal: controller.signal,
      headers: {
        ...init.headers,
        Authorization: `Bearer ${env.OBFUSCATOR_API_TOKEN}`
      }
    });
  } finally {
    clearTimeout(timeout);
  }
}

function healthResponse(env) {
  if (!configured(env)) return errorResponse(503, "Service is not configured");
  return Response.json(
    { status: "ok", service: "openobfuscator", version: "1.2.0", languages: ["javascript", "luajit"] },
    { headers: apiHeaders() }
  );
}

async function obfuscateResponse(request, env) {
  if (request.method !== "POST") {
    return new Response(null, { status: 405, headers: apiHeaders({ Allow: "POST" }) });
  }
  if (!configured(env)) return errorResponse(503, "Service is not configured");
  if ((request.headers.get("Content-Type") || "").split(";", 1)[0].trim().toLowerCase() !== "application/json") {
    return errorResponse(415, "Content-Type must be application/json");
  }

  const contentLengthHeader = request.headers.get("Content-Length");
  if (contentLengthHeader !== null) {
    const declaredLength = Number(contentLengthHeader);
    if (!Number.isSafeInteger(declaredLength) || declaredLength < 0) return errorResponse(400, "Invalid Content-Length");
    if (declaredLength > MAX_REQUEST_BYTES) return errorResponse(413, "Request is too large");
  }

  let requestText;
  try {
    requestText = await readUtf8Body(request, MAX_REQUEST_BYTES);
  } catch {
    return errorResponse(400, "Could not read request body");
  }
  if (requestText === null) return errorResponse(413, "Request is too large");

  let payload;
  try {
    payload = JSON.parse(requestText);
  } catch {
    return errorResponse(400, "Invalid JSON");
  }
  if (!payload || typeof payload !== "object" || typeof payload.source !== "string" || !payload.source.trim()) {
    return errorResponse(400, "Source code is required");
  }
  if (new TextEncoder().encode(payload.source).length > MAX_SOURCE_BYTES) return errorResponse(413, "Source exceeds the 500 KB limit");
  if (!new Set(["lua", "javascript"]).has(payload.language)) return errorResponse(400, "Unsupported language");
  if (!Number.isInteger(payload.preset) || payload.preset < 0 || payload.preset > 2) return errorResponse(400, "Unsupported protection preset");
  if (payload.language === "javascript" && payload.preset !== 0) return errorResponse(400, "JavaScript supports only the source VM preset");

  let upstream;
  try {
    upstream = await originFetch(env, "/obfuscate", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "X-Client-IP": request.headers.get("CF-Connecting-IP") || "127.0.0.1"
      },
      body: JSON.stringify({ source: payload.source, language: payload.language, preset: payload.preset })
    });
  } catch {
    return errorResponse(502, "Obfuscation origin did not respond");
  }

  if (!upstream.ok) {
    let message = "The source could not be obfuscated";
    try {
      const body = await upstream.json();
      if (typeof body.error === "string") message = body.error;
    } catch {
      // Keep the public generic message when the origin response is malformed.
    }
    const status = [400, 413, 422, 429, 504].includes(upstream.status) ? upstream.status : 502;
    return errorResponse(status, message);
  }

  const headers = apiHeaders({
    "Content-Type": "text/plain; charset=utf-8",
    "X-Obfuscation-Duration-Ms": upstream.headers.get("X-Obfuscation-Duration-Ms") || "0"
  });
  return new Response(upstream.body, { status: 200, headers });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/api/health") return healthResponse(env);
    if (url.pathname === "/api/obfuscate") return obfuscateResponse(request, env);
    if (url.pathname.startsWith("/api/")) return errorResponse(404, "Not found");

    const response = await env.ASSETS.fetch(request);
    const headers = new Headers(response.headers);
    for (const [name, value] of Object.entries(SECURITY_HEADERS)) headers.set(name, value);
    headers.set("Cache-Control", url.pathname === "/" ? "no-cache" : "public, max-age=300, must-revalidate");
    return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
  }
};
