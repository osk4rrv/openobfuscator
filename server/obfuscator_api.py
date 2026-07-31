#!/usr/bin/env python3
"""Minimal authenticated HTTP adapter for the OpenObfuscator CLI."""

from __future__ import annotations

import hmac
import ipaddress
import json
import logging
import os
import resource
import secrets
import signal
import subprocess
import tempfile
import time
from collections import deque
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from typing import NoReturn

MAX_SOURCE_BYTES = 500_000
MAX_REQUEST_BYTES = 3_100_000
MAX_OUTPUT_BYTES = 16_000_000
PROCESS_TIMEOUT_SECONDS = 20
RATE_LIMIT_WINDOW_SECONDS = 60
RATE_LIMIT_PER_CLIENT = 10
RATE_LIMIT_GLOBAL = 120

BINARY = Path(os.environ.get("OBFUSCATOR_BINARY", "/opt/openobfuscator/bin/openobfuscator"))
SANDBOX_BINARY = Path(os.environ.get("OBFUSCATOR_SANDBOX", "/usr/bin/bwrap"))
API_TOKEN = os.environ.get("OBFUSCATOR_API_TOKEN", "")
BIND_HOST = os.environ.get("OBFUSCATOR_BIND_HOST", "127.0.0.1")
BIND_PORT = int(os.environ.get("OBFUSCATOR_BIND_PORT", "8788"))

logging.basicConfig(level=os.environ.get("LOG_LEVEL", "INFO"), format="%(asctime)s %(levelname)s %(message)s")
LOGGER = logging.getLogger("openobfuscator-api")
CLIENT_REQUESTS: dict[str, deque[float]] = {}
GLOBAL_REQUESTS: deque[float] = deque()


def fail_startup(message: str) -> NoReturn:
    raise SystemExit(message)


def json_bytes(payload: dict[str, object]) -> bytes:
    return json.dumps(payload, separators=(",", ":")).encode("utf-8")


def build_command(source_path: Path, language: str, preset: int) -> list[str]:
    command = [
        str(BINARY),
        "--language",
        language,
        "--seed",
        str(secrets.randbits(32)),
    ]
    if preset == 0:
        command.extend(["--no-style", "--no-antidebug"])
    elif preset == 1:
        command.append("--no-antidebug")
    command.append(str(source_path))
    return [
        str(SANDBOX_BINARY),
        "--unshare-net",
        "--die-with-parent",
        "--new-session",
        "--ro-bind",
        "/",
        "/",
        "--dev",
        "/dev",
        "--proc",
        "/proc",
        "--",
        *command,
    ]


def client_key(header_value: str | None, fallback: str) -> str:
    if header_value:
        try:
            return str(ipaddress.ip_address(header_value.strip()))
        except ValueError:
            pass
    return fallback


def rate_limit_exceeded(key: str) -> bool:
    now = time.monotonic()
    cutoff = now - RATE_LIMIT_WINDOW_SECONDS
    while GLOBAL_REQUESTS and GLOBAL_REQUESTS[0] <= cutoff:
        GLOBAL_REQUESTS.popleft()

    requests = CLIENT_REQUESTS.setdefault(key, deque())
    while requests and requests[0] <= cutoff:
        requests.popleft()

    if len(CLIENT_REQUESTS) > 10_000:
        for stale_key in [candidate for candidate, times in CLIENT_REQUESTS.items() if candidate != key and (not times or times[-1] <= cutoff)]:
            CLIENT_REQUESTS.pop(stale_key, None)

    if len(GLOBAL_REQUESTS) >= RATE_LIMIT_GLOBAL or len(requests) >= RATE_LIMIT_PER_CLIENT:
        return True
    GLOBAL_REQUESTS.append(now)
    requests.append(now)
    return False


def set_child_limits() -> None:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_FSIZE, (MAX_OUTPUT_BYTES, MAX_OUTPUT_BYTES))
    resource.setrlimit(resource.RLIMIT_CPU, (PROCESS_TIMEOUT_SECONDS, PROCESS_TIMEOUT_SECONDS))


class ObfuscatorHandler(BaseHTTPRequestHandler):
    server_version = "OpenObfuscatorAPI/1.0"
    sys_version = ""

    def send_bytes(self, status: HTTPStatus, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, status: HTTPStatus, payload: dict[str, object]) -> None:
        self.send_bytes(status, json_bytes(payload), "application/json; charset=utf-8")

    def is_authorized(self) -> bool:
        supplied = self.headers.get("Authorization", "")
        expected = f"Bearer {API_TOKEN}"
        return bool(API_TOKEN) and hmac.compare_digest(supplied, expected)

    def require_authorization(self) -> bool:
        if self.is_authorized():
            return True
        self.send_json(HTTPStatus.UNAUTHORIZED, {"error": "Unauthorized"})
        return False

    def do_GET(self) -> None:  # noqa: N802
        if self.path != "/health":
            self.send_json(HTTPStatus.NOT_FOUND, {"error": "Not found"})
            return
        if not self.require_authorization():
            return
        self.send_json(HTTPStatus.OK, {"status": "ok", "service": "openobfuscator-origin", "version": "1.2.0"})

    def do_POST(self) -> None:  # noqa: N802
        if self.path != "/obfuscate":
            self.send_json(HTTPStatus.NOT_FOUND, {"error": "Not found"})
            return
        if not self.require_authorization():
            return
        key = client_key(self.headers.get("X-Client-IP"), self.client_address[0])
        if rate_limit_exceeded(key):
            self.send_json(HTTPStatus.TOO_MANY_REQUESTS, {"error": "Rate limit exceeded; try again shortly"})
            return
        if self.headers.get_content_type() != "application/json":
            self.send_json(HTTPStatus.UNSUPPORTED_MEDIA_TYPE, {"error": "Content-Type must be application/json"})
            return

        raw_length = self.headers.get("Content-Length")
        try:
            content_length = int(raw_length or "")
        except ValueError:
            self.send_json(HTTPStatus.LENGTH_REQUIRED, {"error": "A valid Content-Length is required"})
            return
        if content_length < 2 or content_length > MAX_REQUEST_BYTES:
            self.send_json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, {"error": "Request is too large"})
            return

        body = self.rfile.read(content_length)
        if len(body) != content_length:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "Incomplete request body"})
            return
        try:
            payload = json.loads(body)
        except (UnicodeDecodeError, json.JSONDecodeError):
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "Invalid JSON"})
            return

        source = payload.get("source") if isinstance(payload, dict) else None
        language = payload.get("language") if isinstance(payload, dict) else None
        preset = payload.get("preset") if isinstance(payload, dict) else None
        if not isinstance(source, str) or not source.strip():
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "Source code is required"})
            return
        source_bytes = source.encode("utf-8")
        if len(source_bytes) > MAX_SOURCE_BYTES:
            self.send_json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, {"error": "Source exceeds the 500 KB limit"})
            return
        if language not in {"lua", "javascript"}:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "Unsupported language"})
            return
        if type(preset) is not int or preset not in {0, 1, 2}:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "Unsupported protection preset"})
            return
        if language == "javascript" and preset != 0:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "JavaScript supports only the source VM preset"})
            return

        started = time.monotonic()
        suffix = ".lua" if language == "lua" else ".js"
        process: subprocess.Popen[bytes] | None = None
        try:
            with tempfile.TemporaryDirectory(prefix="openobfuscator-") as temporary_directory:
                temporary_path = Path(temporary_directory)
                source_path = temporary_path / f"input{suffix}"
                output_path = temporary_path / "output.txt"
                error_path = temporary_path / "error.txt"
                source_path.write_bytes(source_bytes)
                with output_path.open("xb") as output_file, error_path.open("xb") as error_file:
                    process = subprocess.Popen(
                        build_command(source_path, language, preset),
                        stdin=subprocess.DEVNULL,
                        stdout=output_file,
                        stderr=error_file,
                        start_new_session=True,
                        env={"LANG": "C.UTF-8", "PATH": "/usr/bin:/bin"},
                        preexec_fn=set_child_limits,
                    )
                    return_code = process.wait(timeout=PROCESS_TIMEOUT_SECONDS)
                output = output_path.read_bytes()
                with error_path.open("rb") as error_file:
                    error_output = error_file.read(1000)
        except subprocess.TimeoutExpired:
            if process is not None:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
            LOGGER.warning("Obfuscation timed out after %ss", PROCESS_TIMEOUT_SECONDS)
            self.send_json(HTTPStatus.GATEWAY_TIMEOUT, {"error": "Obfuscation timed out"})
            return
        except OSError:
            LOGGER.exception("Could not start the obfuscator binary")
            self.send_json(HTTPStatus.SERVICE_UNAVAILABLE, {"error": "Obfuscation service is unavailable"})
            return

        if return_code != 0:
            if len(output) >= MAX_OUTPUT_BYTES:
                self.send_json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": "Generated output exceeds the service limit"})
                return
            LOGGER.warning("Obfuscator exited with code %s: %s", return_code, error_output.decode("utf-8", "replace"))
            self.send_json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": "The source could not be obfuscated"})
            return
        if not output or len(output) >= MAX_OUTPUT_BYTES:
            self.send_json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": "Generated output exceeds the service limit"})
            return

        duration_ms = max(1, round((time.monotonic() - started) * 1000))
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(output)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Obfuscation-Duration-Ms", str(duration_ms))
        self.end_headers()
        self.wfile.write(output)

    def log_message(self, message: str, *args: object) -> None:
        LOGGER.info("%s %s", self.client_address[0], message % args)


if __name__ == "__main__":
    if len(API_TOKEN) < 32:
        fail_startup("OBFUSCATOR_API_TOKEN must contain at least 32 characters")
    if not BINARY.is_file() or not os.access(BINARY, os.X_OK):
        fail_startup(f"Obfuscator binary is not executable: {BINARY}")
    if not SANDBOX_BINARY.is_file() or not os.access(SANDBOX_BINARY, os.X_OK):
        fail_startup(f"Sandbox binary is not executable: {SANDBOX_BINARY}")
    server = HTTPServer((BIND_HOST, BIND_PORT), ObfuscatorHandler)
    LOGGER.info("OpenObfuscator origin listening on %s:%s", BIND_HOST, BIND_PORT)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
