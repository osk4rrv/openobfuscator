#!/usr/bin/env python3
"""Minimal authenticated HTTP adapter for the OpenObfuscator CLI."""

from __future__ import annotations

import codecs
import hmac
import ipaddress
import json
import logging
import os
import resource
import secrets
import shutil
import signal
import sqlite3
import subprocess
import tempfile
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from typing import NoReturn

PROCESS_TIMEOUT_SECONDS = 90
INPUT_READ_TIMEOUT_SECONDS = 30
OUTPUT_WRITE_TIMEOUT_SECONDS = 30
IO_CHUNK_SIZE = 64 * 1024
MAX_CHUNK_LINE_BYTES = 8192
RATE_LIMIT_WINDOW_SECONDS = 60 * 60
RATE_LIMIT_TOKENS = 3

BINARY = Path(os.environ.get("OBFUSCATOR_BINARY", "/opt/openobfuscator/bin/openobfuscator"))
SANDBOX_BINARY = Path(os.environ.get("OBFUSCATOR_SANDBOX", "/usr/bin/bwrap"))
API_TOKEN = os.environ.get("OBFUSCATOR_API_TOKEN", "")
BIND_HOST = os.environ.get("OBFUSCATOR_BIND_HOST", "127.0.0.1")
BIND_PORT = int(os.environ.get("OBFUSCATOR_BIND_PORT", "8788"))
RATE_DB = Path(os.environ.get("OBFUSCATOR_RATE_DB", "/var/lib/openobfuscator/allowance.sqlite3"))

logging.basicConfig(level=os.environ.get("LOG_LEVEL", "INFO"), format="%(asctime)s %(levelname)s %(message)s")
LOGGER = logging.getLogger("openobfuscator-api")
RATE_LIMIT_LOCK = threading.Lock()
ALLOWANCE_CLEANUP_STOP = threading.Event()


class RequestBodyError(Exception):
    def __init__(self, status: HTTPStatus, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.message = message


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


def client_ip(header_value: str | None, fallback: str) -> str:
    if header_value:
        try:
            return str(ipaddress.ip_address(header_value.strip()))
        except ValueError:
            pass
    return str(ipaddress.ip_address(fallback))


def client_cookie(header_value: str | None) -> str | None:
    if not header_value:
        return None
    value = header_value.strip().lower()
    if len(value) != 32 or any(character not in "0123456789abcdef" for character in value):
        return None
    return value


def rate_connection() -> sqlite3.Connection:
    connection = sqlite3.connect(RATE_DB, timeout=5)
    connection.execute("PRAGMA secure_delete=ON")
    return connection


def initialize_rate_store() -> None:
    RATE_DB.parent.mkdir(parents=True, exist_ok=True)
    with rate_connection() as connection:
        connection.execute(
            "CREATE TABLE IF NOT EXISTS allowance (identity_type TEXT NOT NULL, identity TEXT NOT NULL, used_at REAL NOT NULL)"
        )
        connection.execute(
            "CREATE INDEX IF NOT EXISTS allowance_identity ON allowance(identity_type, identity, used_at)"
        )
        connection.execute("CREATE INDEX IF NOT EXISTS allowance_expiry ON allowance(used_at)")


def purge_expired(connection: sqlite3.Connection, now: float) -> None:
    connection.execute("DELETE FROM allowance WHERE used_at <= ?", (now - RATE_LIMIT_WINDOW_SECONDS,))


def allowance_state(
    connection: sqlite3.Connection,
    now: float,
    ip: str,
    cookie: str,
) -> tuple[bool, int, int]:
    histories: list[tuple[int, float | None]] = []
    for identity_type, identity in (("ip", ip), ("cookie", cookie)):
        count, oldest = connection.execute(
            "SELECT COUNT(*), MIN(used_at) FROM allowance WHERE identity_type = ? AND identity = ?",
            (identity_type, identity),
        ).fetchone()
        histories.append((int(count), float(oldest) if oldest is not None else None))

    exhausted = [history for history in histories if history[0] >= RATE_LIMIT_TOKENS]
    remaining = min(max(0, RATE_LIMIT_TOKENS - history[0]) for history in histories)
    if exhausted:
        reset_base = max(history[1] for history in exhausted if history[1] is not None)
        return False, 0, max(1, int(reset_base + RATE_LIMIT_WINDOW_SECONDS - now) + 1)

    constraining_count = max(history[0] for history in histories)
    constraining = [history for history in histories if history[0] == constraining_count]
    oldest_values = [history[1] for history in constraining if history[1] is not None]
    reset_after = RATE_LIMIT_WINDOW_SECONDS if not oldest_values else max(
        1, int(max(oldest_values) + RATE_LIMIT_WINDOW_SECONDS - now) + 1
    )
    return True, remaining, reset_after


def check_allowance(ip: str, cookie: str) -> tuple[bool, int, int]:
    now = time.time()
    with RATE_LIMIT_LOCK, rate_connection() as connection:
        purge_expired(connection, now)
        return allowance_state(connection, now, ip, cookie)


def record_success(ip: str, cookie: str) -> tuple[bool, int, int]:
    now = time.time()
    with RATE_LIMIT_LOCK, rate_connection() as connection:
        purge_expired(connection, now)
        allowed, remaining, reset_after = allowance_state(connection, now, ip, cookie)
        if not allowed:
            return allowed, remaining, reset_after
        connection.executemany(
            "INSERT INTO allowance(identity_type, identity, used_at) VALUES (?, ?, ?)",
            (("ip", ip, now), ("cookie", cookie, now)),
        )
        _, remaining, reset_after = allowance_state(connection, now, ip, cookie)
        return True, remaining, reset_after


def allowance_cleanup_loop() -> None:
    while not ALLOWANCE_CLEANUP_STOP.is_set():
        delay = 60.0
        try:
            now = time.time()
            with RATE_LIMIT_LOCK, rate_connection() as connection:
                purge_expired(connection, now)
                oldest = connection.execute("SELECT MIN(used_at) FROM allowance").fetchone()[0]
            if oldest is not None:
                delay = max(0.05, min(60.0, float(oldest) + RATE_LIMIT_WINDOW_SECONDS - time.time()))
        except sqlite3.Error:
            LOGGER.exception("Could not clean expired allowance records; retrying")
        ALLOWANCE_CLEANUP_STOP.wait(delay)


def set_child_limits() -> None:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_CPU, (PROCESS_TIMEOUT_SECONDS, PROCESS_TIMEOUT_SECONDS))


class ObfuscatorHandler(BaseHTTPRequestHandler):
    server_version = "OpenObfuscatorAPI/1.3.0"
    sys_version = ""

    def send_bytes(
        self,
        status: HTTPStatus,
        body: bytes,
        content_type: str,
        extra_headers: dict[str, str] | None = None,
    ) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        for name, value in (extra_headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def send_json(
        self,
        status: HTTPStatus,
        payload: dict[str, object],
        extra_headers: dict[str, str] | None = None,
    ) -> None:
        self.send_bytes(status, json_bytes(payload), "application/json; charset=utf-8", extra_headers)

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
        self.send_json(HTTPStatus.OK, {"status": "ok", "service": "openobfuscator-origin", "version": "1.3.0"})

    def read_exact_chunks(self, size: int):
        remaining = size
        while remaining:
            chunk = self.rfile.read(min(IO_CHUNK_SIZE, remaining))
            if not chunk:
                raise RequestBodyError(HTTPStatus.BAD_REQUEST, "Incomplete request body")
            remaining -= len(chunk)
            yield chunk

    def request_body_chunks(self):
        transfer_encoding = self.headers.get("Transfer-Encoding")
        raw_length = self.headers.get("Content-Length")
        if transfer_encoding and raw_length:
            raise RequestBodyError(HTTPStatus.BAD_REQUEST, "Conflicting request body framing")

        if transfer_encoding:
            encodings = [value.strip().lower() for value in transfer_encoding.split(",")]
            if encodings != ["chunked"]:
                raise RequestBodyError(HTTPStatus.NOT_IMPLEMENTED, "Unsupported Transfer-Encoding")
            while True:
                line = self.rfile.readline(MAX_CHUNK_LINE_BYTES + 1)
                if len(line) > MAX_CHUNK_LINE_BYTES or not line.endswith(b"\r\n"):
                    raise RequestBodyError(HTTPStatus.BAD_REQUEST, "Invalid chunked request body")
                size_text = line[:-2].split(b";", 1)[0]
                if not size_text or any(byte not in b"0123456789abcdefABCDEF" for byte in size_text):
                    raise RequestBodyError(HTTPStatus.BAD_REQUEST, "Invalid chunked request body")
                chunk_size = int(size_text, 16)
                if chunk_size == 0:
                    while True:
                        trailer = self.rfile.readline(MAX_CHUNK_LINE_BYTES + 1)
                        if len(trailer) > MAX_CHUNK_LINE_BYTES or not trailer.endswith(b"\r\n"):
                            raise RequestBodyError(HTTPStatus.BAD_REQUEST, "Invalid chunked request body")
                        if trailer == b"\r\n":
                            return
                yield from self.read_exact_chunks(chunk_size)
                if self.rfile.read(2) != b"\r\n":
                    raise RequestBodyError(HTTPStatus.BAD_REQUEST, "Invalid chunked request body")

        if raw_length is None:
            raise RequestBodyError(HTTPStatus.LENGTH_REQUIRED, "Content-Length or chunked framing is required")
        try:
            content_length = int(raw_length)
        except ValueError as error:
            raise RequestBodyError(HTTPStatus.BAD_REQUEST, "Invalid Content-Length") from error
        if content_length < 0:
            raise RequestBodyError(HTTPStatus.BAD_REQUEST, "Invalid Content-Length")
        yield from self.read_exact_chunks(content_length)

    def stream_source(self, source_path: Path) -> bool:
        decoder = codecs.getincrementaldecoder("utf-8")("strict")
        has_source = False
        with source_path.open("xb") as source_file:
            for chunk in self.request_body_chunks():
                source_file.write(chunk)
                text = decoder.decode(chunk)
                if not has_source and any(not character.isspace() for character in text):
                    has_source = True
            text = decoder.decode(b"", final=True)
            if not has_source and any(not character.isspace() for character in text):
                has_source = True
        return has_source

    def do_POST(self) -> None:  # noqa: N802
        if self.path != "/obfuscate":
            self.send_json(HTTPStatus.NOT_FOUND, {"error": "Not found"})
            return
        if not self.require_authorization():
            return
        if self.headers.get_content_type() != "text/plain":
            self.send_json(HTTPStatus.UNSUPPORTED_MEDIA_TYPE, {"error": "Content-Type must be text/plain"})
            return

        ip = client_ip(self.headers.get("X-Client-IP"), self.client_address[0])
        cookie = client_cookie(self.headers.get("X-Client-ID"))
        if cookie is None:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "A valid client cookie identifier is required"})
            return

        language = self.headers.get("X-OpenObfuscator-Language", "").strip().lower()
        raw_preset = self.headers.get("X-OpenObfuscator-Preset", "").strip()
        if language not in {"lua", "javascript"}:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "Unsupported language"})
            return
        if raw_preset not in {"0", "1", "2"}:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "Unsupported protection preset"})
            return
        preset = int(raw_preset)
        if language == "javascript" and preset != 0:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "JavaScript supports only the encoded-loader preset"})
            return

        try:
            allowed, remaining, reset_after = check_allowance(ip, cookie)
        except sqlite3.Error:
            LOGGER.exception("Could not read the allowance store")
            self.send_json(HTTPStatus.SERVICE_UNAVAILABLE, {"error": "Obfuscation service is unavailable"})
            return
        allowance_headers = {
            "X-RateLimit-Limit": str(RATE_LIMIT_TOKENS),
            "X-RateLimit-Remaining": str(remaining),
            "X-RateLimit-Reset": str(reset_after),
        }
        if not allowed:
            allowance_headers["Retry-After"] = str(reset_after)
            self.send_json(
                HTTPStatus.TOO_MANY_REQUESTS,
                {"error": "Hourly limit reached. You can obfuscate three times per hour."},
                allowance_headers,
            )
            return

        suffix = ".lua" if language == "lua" else ".js"
        process: subprocess.Popen[bytes] | None = None
        response_started = False
        try:
            with tempfile.TemporaryDirectory(prefix="openobfuscator-") as temporary_directory:
                temporary_path = Path(temporary_directory)
                source_path = temporary_path / f"input{suffix}"
                output_path = temporary_path / "output.txt"
                error_path = temporary_path / "error.txt"

                self.connection.settimeout(INPUT_READ_TIMEOUT_SECONDS)
                try:
                    has_source = self.stream_source(source_path)
                except RequestBodyError as error:
                    self.send_json(error.status, {"error": error.message}, allowance_headers)
                    return
                except UnicodeDecodeError:
                    self.send_json(HTTPStatus.BAD_REQUEST, {"error": "Source code must be valid UTF-8"}, allowance_headers)
                    return
                except TimeoutError:
                    self.send_json(HTTPStatus.REQUEST_TIMEOUT, {"error": "Request body timed out"}, allowance_headers)
                    return
                finally:
                    self.connection.settimeout(None)
                if not has_source:
                    self.send_json(HTTPStatus.BAD_REQUEST, {"error": "Source code is required"}, allowance_headers)
                    return

                started = time.monotonic()
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
                    try:
                        return_code = process.wait(timeout=PROCESS_TIMEOUT_SECONDS)
                    except subprocess.TimeoutExpired:
                        try:
                            os.killpg(process.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        process.wait()
                        raise
                with error_path.open("rb") as error_file:
                    error_output = error_file.read(1000)

                if return_code != 0:
                    LOGGER.warning("Obfuscator exited with code %s: %s", return_code, error_output.decode("utf-8", "replace"))
                    self.send_json(
                        HTTPStatus.UNPROCESSABLE_ENTITY,
                        {"error": "The source could not be obfuscated"},
                        allowance_headers,
                    )
                    return

                output_size = output_path.stat().st_size
                if output_size == 0:
                    self.send_json(
                        HTTPStatus.UNPROCESSABLE_ENTITY,
                        {"error": "The obfuscator produced no output"},
                        allowance_headers,
                    )
                    return

                try:
                    allowed, remaining, reset_after = record_success(ip, cookie)
                except sqlite3.Error:
                    LOGGER.exception("Could not record a successful obfuscation")
                    self.send_json(
                        HTTPStatus.SERVICE_UNAVAILABLE,
                        {"error": "Obfuscation service is unavailable"},
                        allowance_headers,
                    )
                    return
                allowance_headers.update({
                    "X-RateLimit-Remaining": str(remaining),
                    "X-RateLimit-Reset": str(reset_after),
                })
                if not allowed:
                    allowance_headers["Retry-After"] = str(reset_after)
                    self.send_json(
                        HTTPStatus.TOO_MANY_REQUESTS,
                        {"error": "Hourly limit reached. You can obfuscate three times per hour."},
                        allowance_headers,
                    )
                    return
                LOGGER.info("Successful obfuscation recorded; remaining=%s", remaining)

                duration_ms = max(1, round((time.monotonic() - started) * 1000))
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/plain; charset=utf-8")
                self.send_header("Content-Length", str(output_size))
                self.send_header("Cache-Control", "no-store")
                self.send_header("X-Content-Type-Options", "nosniff")
                self.send_header("X-Obfuscation-Duration-Ms", str(duration_ms))
                for name, value in allowance_headers.items():
                    self.send_header(name, value)
                self.end_headers()
                response_started = True
                self.connection.settimeout(OUTPUT_WRITE_TIMEOUT_SECONDS)
                with output_path.open("rb") as output_file:
                    shutil.copyfileobj(output_file, self.wfile, length=IO_CHUNK_SIZE)
        except subprocess.TimeoutExpired:
            LOGGER.warning("Obfuscation timed out after %ss", PROCESS_TIMEOUT_SECONDS)
            self.send_json(HTTPStatus.GATEWAY_TIMEOUT, {"error": "Obfuscation timed out"}, allowance_headers)
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            LOGGER.info("Client disconnected or timed out while receiving obfuscated output")
        except OSError:
            LOGGER.exception("Could not run the obfuscator binary or transfer its output")
            if not response_started:
                self.send_json(
                    HTTPStatus.SERVICE_UNAVAILABLE,
                    {"error": "Obfuscation service is unavailable"},
                    allowance_headers,
                )

    def log_message(self, message: str, *args: object) -> None:
        LOGGER.info("%s %s", self.client_address[0], message % args)


if __name__ == "__main__":
    if len(API_TOKEN) < 32:
        fail_startup("OBFUSCATOR_API_TOKEN must contain at least 32 characters")
    if not BINARY.is_file() or not os.access(BINARY, os.X_OK):
        fail_startup(f"Obfuscator binary is not executable: {BINARY}")
    if not SANDBOX_BINARY.is_file() or not os.access(SANDBOX_BINARY, os.X_OK):
        fail_startup(f"Sandbox binary is not executable: {SANDBOX_BINARY}")
    initialize_rate_store()
    server = HTTPServer((BIND_HOST, BIND_PORT), ObfuscatorHandler)
    cleanup_thread = threading.Thread(target=allowance_cleanup_loop, name="allowance-cleanup", daemon=True)
    cleanup_thread.start()
    LOGGER.info("OpenObfuscator origin listening on %s:%s", BIND_HOST, BIND_PORT)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        ALLOWANCE_CLEANUP_STOP.set()
        cleanup_thread.join(timeout=2)
        server.server_close()
