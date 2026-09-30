"""Regression test for persistent CEF profile cookies and site storage."""
import ctypes
import http.server
import json
import os
import pathlib
import sqlite3
import socket
import subprocess
import sys
import threading
import time
import urllib.request

import websocket


DEBUG_PORT = int(os.environ.get("SOULU_UI_TEST_PORT", "9223"))
BASE = f"http://127.0.0.1:{DEBUG_PORT}"
COOKIE_VALUE = "soulu-session-cookie"
STORAGE_VALUE = "soulu-local-storage"
IDB_VALUE = "soulu-indexed-db"
sequence = 0
received_cookie = ""


class SiteHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        global received_cookie
        received_cookie = self.headers.get("Cookie", "")
        body = b"<!doctype html><meta charset=utf-8><title>Soulu storage test</title>"
        self.send_response(200)
        if self.path.startswith("/seed"):
            # Deliberately a session cookie: no Expires or Max-Age.
            self.send_header("Set-Cookie", f"soulu_auth={COOKIE_VALUE}; Path=/; HttpOnly; SameSite=Lax")
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def targets(timeout=45):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(BASE + "/json/list", timeout=2) as response:
                return json.load(response)
        except OSError:
            time.sleep(0.25)
    raise AssertionError("CEF remote debugging did not start")


def page_socket():
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        for target in targets():
            if target.get("type") == "page" and "/ui/index.html" not in target.get("url", ""):
                return websocket.create_connection(
                    target["webSocketDebuggerUrl"], timeout=30,
                    origin=f"http://127.0.0.1:{DEBUG_PORT}")
        time.sleep(.1)
    raise AssertionError("CEF content target did not appear")


def command(ws, method, params=None):
    global sequence
    sequence += 1
    ident = sequence
    ws.send(json.dumps({"id": ident, "method": method, "params": params or {}}))
    while True:
        response = json.loads(ws.recv())
        if response.get("id") != ident:
            continue
        if "error" in response:
            raise AssertionError(response["error"])
        return response.get("result", {})


def evaluate(ws, expression):
    result = command(ws, "Runtime.evaluate", {
        "expression": expression, "returnByValue": True, "awaitPromise": True
    })
    if "exceptionDetails" in result:
        raise AssertionError(result["exceptionDetails"])
    return result["result"].get("value")


def navigate(ws, url):
    command(ws, "Page.enable")
    command(ws, "Page.navigate", {"url": url})
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if evaluate(ws, "location.href") == url and evaluate(ws, "document.readyState") == "complete":
            return
        time.sleep(0.2)
    raise AssertionError(f"Page did not load: {url}")


def close_normally(process):
    user32 = ctypes.windll.user32
    handles = []
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    @callback_type
    def callback(hwnd, _):
        pid = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        class_name = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, class_name, len(class_name))
        # Close the application host, not a Chromium-owned popup/tooltip that
        # can precede it in EnumWindows and leave the application running.
        if pid.value == process.pid and class_name.value == 'SouluBrowserWindow':
            handles.append(hwnd)
            return False
        return True

    user32.EnumWindows(callback, 0)
    if not handles:
        raise AssertionError("Soulu top-level window was not found")
    user32.PostMessageW(handles[0], 0x0010, 0, 0)  # WM_CLOSE
    process.wait(timeout=30)


def launch(executable):
    environment = os.environ.copy()
    environment["SOULU_UI_TEST_PORT"] = str(DEBUG_PORT)
    return subprocess.Popen([executable], env=environment)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test-cef-storage.py <Soulu.exe>")
    executable = os.path.abspath(sys.argv[1])
    site_port = free_port()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", site_port), SiteHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f"http://127.0.0.1:{site_port}"

    first = launch(executable)
    try:
        ws = page_socket()
        navigate(ws, origin + "/seed")
        evaluate(ws, f"localStorage.setItem('soulu-test', {json.dumps(STORAGE_VALUE)})")
        evaluate(ws, """new Promise((resolve, reject) => {
          const request = indexedDB.open('soulu-test-db', 1);
          request.onupgradeneeded = () => request.result.createObjectStore('values');
          request.onerror = () => reject(request.error);
          request.onsuccess = () => {
            const tx = request.result.transaction('values', 'readwrite');
            tx.objectStore('values').put('soulu-indexed-db', 'auth');
            tx.oncomplete = () => { request.result.close(); resolve(true); };
            tx.onerror = () => reject(tx.error);
          };
        })""")
        indexed_seed = evaluate(ws, """new Promise((resolve, reject) => {
          const request = indexedDB.open('soulu-test-db', 1);
          request.onerror = () => reject(request.error);
          request.onsuccess = () => {
            const db = request.result;
            const get = db.transaction('values').objectStore('values').get('auth');
            get.onsuccess = () => { db.close(); resolve(get.result); };
            get.onerror = () => reject(get.error);
          };
        })""")
        assert indexed_seed == IDB_VALUE, indexed_seed
        time.sleep(2)
        cookies = command(ws, "Network.getAllCookies").get("cookies", [])
        assert any(item["name"] == "soulu_auth" and item["value"] == COOKIE_VALUE for item in cookies)
        ws.close()
        close_normally(first)
        root = pathlib.Path(os.environ["LOCALAPPDATA"]) / "Soulu" / "User Data" / "Profiles"
        for pref_file in root.rglob("Preferences"):
            prefs = json.loads(pref_file.read_text(encoding="utf-8"))
            print(json.dumps({"profile": str(pref_file.parent.relative_to(root)),
                              "session": prefs.get("session"),
                              "exit_type": prefs.get("profile", {}).get("exit_type")}), flush=True)
        for cookie_file in root.rglob("Cookies"):
            with sqlite3.connect(f"file:{cookie_file.as_posix()}?mode=ro", uri=True) as db:
                rows = db.execute("SELECT host_key,name,is_persistent,has_expires,expires_utc FROM cookies WHERE name='soulu_auth'").fetchall()
                print(json.dumps({"cookie_file": str(cookie_file.relative_to(root)), "test_cookie_rows": rows}), flush=True)
    finally:
        if first.poll() is None:
            first.kill()

    second = launch(executable)
    try:
        ws = page_socket()
        navigate(ws, origin + "/verify")
        local_value = evaluate(ws, "localStorage.getItem('soulu-test')")
        cookies = command(ws, "Network.getAllCookies").get("cookies", [])
        # Read in the page's own storage context, exactly as a website would.
        # Aborting an upgrade prevents a missing database from being recreated.
        indexed_value = evaluate(ws, """new Promise((resolve, reject) => {
          const request = indexedDB.open('soulu-test-db', 1);
          request.onupgradeneeded = () => {
            request.transaction.abort();
            reject(new Error('IndexedDB database missing after restart'));
          };
          request.onerror = () => reject(request.error);
          request.onsuccess = () => {
            const db = request.result;
            const get = db.transaction('values').objectStore('values').get('auth');
            get.onsuccess = () => { db.close(); resolve(get.result); };
            get.onerror = () => { db.close(); reject(get.error); };
          };
        })""")
        cookie_value = next((item["value"] for item in cookies if item["name"] == "soulu_auth"), None)
        print(json.dumps({"restored_session_cookie": cookie_value == COOKIE_VALUE,
                          "cookie_sent_to_server": f"soulu_auth={COOKIE_VALUE}" in received_cookie,
                          "localStorage": local_value, "IndexedDB": indexed_value}), flush=True)
        assert cookie_value == COOKIE_VALUE, f"session cookie missing after restart: {cookie_value!r}"
        assert f"soulu_auth={COOKIE_VALUE}" in received_cookie, received_cookie
        assert local_value == STORAGE_VALUE, local_value
        assert indexed_value == IDB_VALUE, indexed_value
        print(json.dumps({
            "session_cookie": "preserved",
            "cookie_sent_to_server": True,
            "localStorage": "preserved",
            "IndexedDB": "preserved",
            "clean_restart": True,
        }))
        ws.close()
        close_normally(second)
    finally:
        if second.poll() is None:
            second.kill()
        server.shutdown()


if __name__ == "__main__":
    main()

