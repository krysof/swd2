#!/usr/bin/env python3
"""Run the two-document IDBFS probe in the installed branded Safari app."""

from __future__ import annotations

import argparse
import datetime
import hashlib
import http.server
import json
import mimetypes
import queue
import secrets
import subprocess
import threading
import urllib.parse
from pathlib import Path


ASSETS = ("index.html", "index.js", "index.wasm", "index.data")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(*args: str) -> str:
    return subprocess.run(
        args, check=True, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT).stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("site", type=Path, nargs="?",
                        default=root / "build-wasm" / "site")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()
    try:
        site = args.site.resolve()
        if not 1 <= args.cycles <= 100:
            raise ValueError("cycles must be from 1 through 100")
        if not 10 <= args.timeout <= 300:
            raise ValueError("timeout must be from 10 through 300 seconds")
        for name in ASSETS:
            if not (site / name).is_file():
                raise ValueError(f"WASM site asset is absent: {name}")

        safari = Path("/Applications/Safari.app")
        info = safari / "Contents" / "Info.plist"
        if not info.is_file():
            raise ValueError("branded Safari.app is absent")
        bundle_id = command("defaults", "read", str(info), "CFBundleIdentifier")
        version = command("defaults", "read", str(info),
                          "CFBundleShortVersionString")
        build = command("defaults", "read", str(info), "CFBundleVersion")
        if bundle_id != "com.apple.Safari" or not version or not build:
            raise ValueError("Safari bundle identity/version differs")

        reports: queue.Queue[dict[str, object]] = queue.Queue()

        class Handler(http.server.BaseHTTPRequestHandler):
            def end_headers(self) -> None:
                self.send_header("Cache-Control", "no-store")
                self.send_header("Cross-Origin-Opener-Policy", "same-origin")
                self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
                super().end_headers()

            def do_GET(self) -> None:  # noqa: N802
                path = urllib.parse.urlsplit(self.path).path
                relative = "index.html" if path == "/" else path.lstrip("/")
                filename = (site / relative).resolve()
                if site not in filename.parents or not filename.is_file():
                    self.send_error(404)
                    return
                mime = {
                    ".html": "text/html; charset=utf-8",
                    ".js": "text/javascript; charset=utf-8",
                    ".wasm": "application/wasm",
                    ".data": "application/octet-stream",
                }.get(filename.suffix, mimetypes.guess_type(filename)[0] or
                      "application/octet-stream")
                body = filename.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", mime)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_POST(self) -> None:  # noqa: N802
                if urllib.parse.urlsplit(self.path).path != \
                        "/__swd2_idbfs_result":
                    self.send_error(404)
                    return
                length = int(self.headers.get("Content-Length", "0"))
                if length < 2 or length > 4096:
                    self.send_error(400)
                    return
                try:
                    payload = json.loads(self.rfile.read(length))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    self.send_error(400)
                    return
                if not isinstance(payload, dict):
                    self.send_error(400)
                    return
                reports.put(payload)
                self.send_response(204)
                self.end_headers()

            def log_message(self, _format: str, *_arguments: object) -> None:
                pass

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        results = []
        try:
            for cycle in range(args.cycles):
                nonce = secrets.token_hex(16)
                token = f"safari-brand-roundtrip-{cycle}-{secrets.token_hex(8)}"
                query = urllib.parse.urlencode({
                    "idbfs-self-test": token,
                    "idbfs-report": nonce,
                })
                url = f"http://127.0.0.1:{server.server_port}/?{query}"
                subprocess.run(["open", "-a", str(safari), url], check=True)
                deadline = datetime.datetime.now().timestamp() + args.timeout
                matched = None
                while datetime.datetime.now().timestamp() < deadline:
                    try:
                        candidate = reports.get(timeout=0.25)
                    except queue.Empty:
                        continue
                    if candidate.get("nonce") == nonce:
                        matched = candidate
                        break
                if matched is None:
                    raise ValueError(f"Safari cycle {cycle} did not report")
                user_agent = matched.get("userAgent")
                if matched.get("passed") is not True or \
                        matched.get("message") != \
                        "IDBFS 写入、页面重启与字节恢复测试通过。" or \
                        not isinstance(user_agent, str) or \
                        "Safari/" not in user_agent or "Version/" not in user_agent or \
                        "Chrome/" in user_agent or "Chromium/" in user_agent:
                    raise ValueError(
                        f"Safari cycle {cycle} result differs: {matched!r}")
                results.append({
                    "cycle": cycle,
                    "status": "verified",
                    "two_document_reload": True,
                    "exact_probe_bytes_restored": True,
                    "cleanup_sync": True,
                    "user_agent": user_agent,
                    "navigator_platform": matched.get("platform", ""),
                })
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)

        report = {
            "schema_version": 1,
            "kind": "wasm_branded_safari_idbfs_restart",
            "status": "verified",
            "limitation": (
                "Installed macOS Safari is not physical iOS/iPadOS and this "
                "self-reporting same-origin probe does not test touch input."),
            "captured_at": datetime.datetime.now().astimezone().isoformat(
                timespec="seconds"),
            "source_commit": command("git", "rev-parse", "HEAD"),
            "browser": {
                "bundle_id": bundle_id,
                "version": version,
                "build": build,
            },
            "cycles": results,
            "assets": {name: sha256(site / name) for name in ASSETS},
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        print(
            f"WASM branded Safari IDBFS: OK ({args.cycles} two-document "
            f"restart cycle{'s' if args.cycles != 1 else ''}, Safari {version})")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"WASM branded Safari IDBFS: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
