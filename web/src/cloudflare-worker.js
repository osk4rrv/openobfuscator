const ORIGIN_TIMEOUT_MS = 180_000;
const HEALTH_TIMEOUT_MS = 5_000;
const CLIENT_COOKIE = "oo_client";
const CLIENT_COOKIE_SECONDS = 60 * 60;

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

function errorResponse(status, error, extraHeaders = {}) {
  return Response.json({ error }, { status, headers: apiHeaders(extraHeaders) });
}

function configured(env) {
  return typeof env.OBFUSCATOR_API_URL === "string" &&
    env.OBFUSCATOR_API_URL.startsWith("https://") &&
    typeof env.OBFUSCATOR_API_TOKEN === "string" &&
    env.OBFUSCATOR_API_TOKEN.length >= 32;
}

function readCookie(request, name) {
  const cookieHeader = request.headers.get("Cookie") || "";
  for (const part of cookieHeader.split(";")) {
    const separator = part.indexOf("=");
    if (separator === -1 || part.slice(0, separator).trim() !== name) continue;
    const value = part.slice(separator + 1).trim().toLowerCase();
    if (/^[a-f0-9]{32}$/.test(value)) return value;
  }
  return null;
}

function clientIdentity(request) {
  const existing = readCookie(request, CLIENT_COOKIE);
  const id = existing || crypto.randomUUID().replaceAll("-", "");
  return {
    id,
    setCookie: `${CLIENT_COOKIE}=${id}; Max-Age=${CLIENT_COOKIE_SECONDS}; Path=/; Secure; HttpOnly; SameSite=Strict`
  };
}

function forwardedHeaders(upstream, setCookie) {
  const headers = {};
  for (const name of ["X-RateLimit-Limit", "X-RateLimit-Remaining", "X-RateLimit-Reset", "Retry-After"]) {
    const value = upstream.headers.get(name);
    if (value !== null) headers[name] = value;
  }
  if (setCookie) headers["Set-Cookie"] = setCookie;
  return headers;
}

async function originFetch(env, path, init = {}, timeoutMs = ORIGIN_TIMEOUT_MS) {
  const deadline = new AbortController();
  const timeoutError = new Error("Origin response timed out");
  const timeout = setTimeout(() => deadline.abort(timeoutError), timeoutMs);
  const signal = init.signal ? AbortSignal.any([deadline.signal, init.signal]) : deadline.signal;

  let upstream;
  try {
    upstream = await fetch(`${env.OBFUSCATOR_API_URL.replace(/\/$/, "")}${path}`, {
      ...init,
      signal,
      headers: {
        ...init.headers,
        Authorization: `Bearer ${env.OBFUSCATOR_API_TOKEN}`
      }
    });
  } catch (error) {
    clearTimeout(timeout);
    throw error;
  }

  if (!upstream.body) {
    clearTimeout(timeout);
    return upstream;
  }

  const reader = upstream.body.getReader();
  let settled = false;
  let bodyController;
  const body = new ReadableStream({
    start(controller) {
      bodyController = controller;
    },
    async pull(controller) {
      try {
        const { done, value } = await reader.read();
        if (settled) return;
        if (done) {
          settled = true;
          clearTimeout(timeout);
          controller.close();
          return;
        }
        controller.enqueue(value);
      } catch (error) {
        if (settled) return;
        settled = true;
        clearTimeout(timeout);
        controller.error(error);
      }
    },
    async cancel(reason) {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      deadline.abort(reason);
      await reader.cancel(reason);
    }
  });

  signal.addEventListener("abort", () => {
    if (settled) return;
    settled = true;
    clearTimeout(timeout);
    reader.cancel(signal.reason).catch(() => {});
    bodyController.error(signal.reason || timeoutError);
  }, { once: true });

  return new Response(body, {
    status: upstream.status,
    statusText: upstream.statusText,
    headers: upstream.headers
  });
}

async function healthResponse(request, env) {
  if (!configured(env)) return errorResponse(503, "Service is not configured");
  try {
    const upstream = await originFetch(env, "/health", {
      headers: { Accept: "application/json" },
      signal: request.signal
    }, HEALTH_TIMEOUT_MS);
    if (!upstream.ok) throw new Error("Origin health check failed");
    const health = await upstream.json();
    if (health?.status !== "ok" || health?.service !== "openobfuscator-origin" || health?.version !== "1.3.0") {
      throw new Error("Origin identity or version mismatch");
    }
  } catch {
    return errorResponse(503, "Obfuscation origin is unavailable");
  }
  return Response.json(
    { status: "ok", service: "openobfuscator", version: "1.3.0", languages: ["javascript", "luajit"] },
    { headers: apiHeaders() }
  );
}

async function obfuscateResponse(request, env) {
  if (request.method !== "POST") {
    return new Response(null, { status: 405, headers: apiHeaders({ Allow: "POST" }) });
  }
  if (!configured(env)) return errorResponse(503, "Service is not configured");
  if ((request.headers.get("Content-Type") || "").split(";", 1)[0].trim().toLowerCase() !== "text/plain") {
    return errorResponse(415, "Content-Type must be text/plain");
  }

  const language = (request.headers.get("X-OpenObfuscator-Language") || "").trim().toLowerCase();
  const preset = (request.headers.get("X-OpenObfuscator-Preset") || "").trim();
  if (!new Set(["lua", "javascript"]).has(language)) return errorResponse(400, "Unsupported language");
  if (!new Set(["0", "1", "2"]).has(preset)) return errorResponse(400, "Unsupported protection preset");
  if (language === "javascript" && preset !== "0") return errorResponse(400, "JavaScript supports only the encoded-loader preset");

  const identity = clientIdentity(request);

  let upstream;
  try {
    upstream = await originFetch(env, "/obfuscate", {
      method: "POST",
      headers: {
        "Content-Type": "text/plain; charset=utf-8",
        "X-OpenObfuscator-Language": language,
        "X-OpenObfuscator-Preset": preset,
        "X-Client-IP": request.headers.get("CF-Connecting-IP") || "127.0.0.1",
        "X-Client-ID": identity.id
      },
      body: request.body,
      signal: request.signal
    });
  } catch {
    return errorResponse(
      502,
      "Obfuscation origin did not respond",
      identity.setCookie ? { "Set-Cookie": identity.setCookie } : {}
    );
  }

  if (!upstream.ok) {
    let message = "The source could not be obfuscated";
    try {
      const body = await upstream.json();
      if (typeof body.error === "string") message = body.error;
    } catch {
      // Keep the public generic message when the origin response is malformed.
    }
    const status = [400, 408, 411, 413, 415, 422, 429, 501, 504].includes(upstream.status) ? upstream.status : 502;
    return errorResponse(status, message, forwardedHeaders(upstream, identity.setCookie));
  }

  const headers = apiHeaders({
    "Content-Type": "text/plain; charset=utf-8",
    "X-Obfuscation-Duration-Ms": upstream.headers.get("X-Obfuscation-Duration-Ms") || "0",
    ...forwardedHeaders(upstream, identity.setCookie)
  });
  return new Response(upstream.body, { status: 200, headers });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/api/health") return healthResponse(request, env);
    if (url.pathname === "/api/obfuscate") return obfuscateResponse(request, env);
    if (url.pathname.startsWith("/api/")) return errorResponse(404, "Not found");

    const response = await env.ASSETS.fetch(request);
    const headers = new Headers(response.headers);
    for (const [name, value] of Object.entries(SECURITY_HEADERS)) headers.set(name, value);
    headers.set("Cache-Control", url.pathname === "/" ? "no-cache" : "public, max-age=300, must-revalidate");
    return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
  }
};
