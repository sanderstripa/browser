"""Verify CEF cookies and site storage survive a graceful Soulu restart."""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes
import http.server
import json
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request

import websocket


COOKIE_NAME = "soulu_session"
COOKIE_VALUE = "persistent-session-value"
STORAGE_KEY = "soulu-session-test"
STORAGE_VALUE = "persistent-storage-value"


class SiteHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"<!doctype html><meta charset=utf-8><title>Soulu session test</title>"
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format, *_args):
        pass


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def targets(port: int) -> list[dict]:
    with urllib.request.urlopen(f"http://127.0.0.1:{port}/json", timeout=1) as response:
        return json.load(response)


def content_target(port: int, timeout: float = 30) -> dict:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            for target in targets(port):
                url = target.get("url", "")
                if target.get("type") == "page" and "/ui/index.html" not in url:
                    return target
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.2)
    raise AssertionError("CEF content target did not appear")


class DevTools:
    def __init__(self, target: dict, port: int):
        self.socket = websocket.create_connection(
            target["webSocketDebuggerUrl"], timeout=5, origin=f"http://127.0.0.1:{port}"
        )
        self.next_id = 0

    def close(self):
        self.socket.close()

    def call(self, method: str, params: dict | None = None) -> dict:
        self.next_id += 1
        request_id = self.next_id
        self.socket.send(json.dumps({"id": request_id, "method": method, "params": params or {}}))
        while True:
            response = json.loads(self.socket.recv())
            if response.get("id") == request_id:
                if "error" in response:
                    raise AssertionError(f"{method} failed: {response['error']}")
                return response.get("result", {})

    def evaluate(self, expression: str):
        response = self.call(
            "Runtime.evaluate",
            {"expression": expression, "awaitPromise": True, "returnByValue": True},
        )
        if "exceptionDetails" in response:
            raise AssertionError(response["exceptionDetails"])
        result = response["result"]
        return result.get("value")


def navigate(devtools: DevTools, url: str):
    devtools.call("Page.enable")
    devtools.call("Page.navigate", {"url": url})
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if devtools.evaluate("document.readyState") == "complete":
            return
        time.sleep(0.1)
    raise AssertionError(f"Page did not load: {url}")


def write_site_state(devtools: DevTools) -> dict:
    return devtools.evaluate(
        f"""
        (async () => {{
          document.cookie = {json.dumps(f'{COOKIE_NAME}={COOKIE_VALUE}; Path=/; SameSite=Lax')};
          localStorage.setItem({json.dumps(STORAGE_KEY)}, {json.dumps(STORAGE_VALUE)});
          await new Promise((resolve, reject) => {{
            const request = indexedDB.open('soulu-session-test', 1);
            request.onupgradeneeded = () => request.result.createObjectStore('state');
            request.onerror = () => reject(request.error);
            request.onsuccess = () => {{
              const db = request.result;
              const transaction = db.transaction('state', 'readwrite');
              transaction.objectStore('state').put({json.dumps(STORAGE_VALUE)}, {json.dumps(STORAGE_KEY)});
              transaction.oncomplete = () => {{ db.close(); resolve(); }};
              transaction.onerror = () => reject(transaction.error);
            }};
          }});
          return {{cookie: document.cookie, localStorage: localStorage.getItem({json.dumps(STORAGE_KEY)})}};
        }})()
        """
    )


def read_site_state(devtools: DevTools) -> dict:
    return devtools.evaluate(
        f"""
        (async () => {{
          const indexedDBValue = await new Promise((resolve, reject) => {{
            const request = indexedDB.open('soulu-session-test', 1);
            request.onerror = () => reject(request.error);
            request.onsuccess = () => {{
              const db = request.result;
              const transaction = db.transaction('state', 'readonly');
              const value = transaction.objectStore('state').get({json.dumps(STORAGE_KEY)});
              value.onsuccess = () => {{ db.close(); resolve(value.result ?? null); }};
              value.onerror = () => reject(value.error);
            }};
          }});
          return {{
            cookie: document.cookie,
            localStorage: localStorage.getItem({json.dumps(STORAGE_KEY)}),
            indexedDB: indexedDBValue
          }};
        }})()
        """
    )


def close_gracefully(process: subprocess.Popen, timeout: float = 30):
    user32 = ctypes.windll.user32
    windows = []
    callback_type = ctypes.WINFUNCTYPE(
        ctypes.wintypes.BOOL, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM
    )
    user32.EnumWindows.argtypes = [callback_type, ctypes.wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [
        ctypes.wintypes.HWND,
        ctypes.POINTER(ctypes.wintypes.DWORD),
    ]
    user32.IsWindowVisible.argtypes = [ctypes.wintypes.HWND]
    user32.PostMessageW.argtypes = [
        ctypes.wintypes.HWND,
        ctypes.wintypes.UINT,
        ctypes.wintypes.WPARAM,
        ctypes.wintypes.LPARAM,
    ]

    @callback_type
    def callback(hwnd, _lparam):
        process_id = ctypes.wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(process_id))
        if process_id.value == process.pid and user32.IsWindowVisible(hwnd):
            windows.append(hwnd)
        return True

    user32.EnumWindows(callback, 0)
    if not windows:
        raise AssertionError("Soulu top-level window was not found")
    user32.PostMessageW(windows[0], 0x0010, 0, 0)
    try:
        code = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        process.kill()
        raise AssertionError("Soulu did not complete graceful shutdown") from error
    if code != 0:
        raise AssertionError(f"Soulu exited with code {code}")


def launch(executable: pathlib.Path, local_app_data: pathlib.Path, debug_port: int) -> subprocess.Popen:
    environment = os.environ.copy()
    environment["LOCALAPPDATA"] = str(local_app_data)
    environment["SOULU_UI_TEST_PORT"] = str(debug_port)
    return subprocess.Popen([str(executable)], env=environment)


def assert_disk_storage(profile_path: pathlib.Path):
    files = [path for path in profile_path.rglob("*") if path.is_file()]
    if not any(path.name == "Cookies" for path in files):
        listing = [str(path.relative_to(profile_path)) for path in files]
        data_root = profile_path.parents[3]
        all_files = [str(path.relative_to(data_root)) for path in data_root.rglob("*") if path.is_file()]
        raise AssertionError(
            f"CEF cookie database was not persisted under {profile_path}; "
            f"profile exists={profile_path.exists()}, files={listing}, "
            f"LOCALAPPDATA files={all_files}"
        )
    if not any("Local Storage" in path.parts for path in files):
        raise AssertionError("CEF localStorage was not persisted")
    if not any("IndexedDB" in path.parts for path in files):
        raise AssertionError("CEF IndexedDB was not persisted")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=pathlib.Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    if not executable.is_file():
        raise FileNotFoundError(executable)

    site_port = free_port()
    debug_port = free_port()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", site_port), SiteHandler)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    data_root = pathlib.Path(tempfile.mkdtemp(prefix="soulu-session-test-"))
    profile_path = (data_root / "Soulu" / "User Data" / "Profiles" / "personal").resolve()
    process = None

    try:
        process = launch(executable, data_root, debug_port)
        devtools = DevTools(content_target(debug_port), debug_port)
        try:
            navigate(devtools, f"http://127.0.0.1:{site_port}/")
            initial = write_site_state(devtools)
            if f"{COOKIE_NAME}={COOKIE_VALUE}" not in initial["cookie"]:
                raise AssertionError("Session cookie was not set")
            if initial["localStorage"] != STORAGE_VALUE:
                raise AssertionError("localStorage was not set")
        finally:
            devtools.close()
        close_gracefully(process)
        process = None
        assert_disk_storage(profile_path)

        second_profile_path = (data_root / "Soulu" / "User Data" / "Profiles" / "personal").resolve()
        if second_profile_path != profile_path:
            raise AssertionError("Profile path changed between launches")
        process = launch(executable, data_root, debug_port)
        devtools = DevTools(content_target(debug_port))
        try:
            navigate(devtools, f"http://127.0.0.1:{site_port}/")
            restored = read_site_state(devtools)
        finally:
            devtools.close()

        if f"{COOKIE_NAME}={COOKIE_VALUE}" not in restored["cookie"]:
            raise AssertionError("Session cookie did not survive restart")
        if restored["localStorage"] != STORAGE_VALUE:
            raise AssertionError("localStorage did not survive restart")
        if restored["indexedDB"] != STORAGE_VALUE:
            raise AssertionError("IndexedDB did not survive restart")
        close_gracefully(process)
        process = None
        print(f"Persistent CEF session passed; profile={profile_path}")
        return 0
    finally:
        if process and process.poll() is None:
            process.kill()
            process.wait(timeout=10)
        server.shutdown()
        server.server_close()
        shutil.rmtree(data_root, ignore_errors=True)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        import traceback
        traceback.print_exc()
        print(f"::error file=scripts/test-cef-session.py,line=1::Session test failed: {type(error).__name__}: {error}", flush=True)
        raise
