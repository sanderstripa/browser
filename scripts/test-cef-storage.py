"""Regression test for persistent CEF profile cookies and site storage."""
import ctypes
from ctypes import wintypes
import http.server
import hashlib
import json
import os
import socket
import sqlite3
import subprocess
import sys
import threading
import time
import tempfile
from pathlib import Path
import urllib.request

import websocket


DEBUG_PORT = int(os.environ.get("SOULU_UI_TEST_PORT", "9223"))
BASE = f"http://127.0.0.1:{DEBUG_PORT}"
COOKIE_VALUE = "soulu-session-cookie"
STORAGE_VALUE = "soulu-local-storage"
IDB_VALUE = "soulu-indexed-db"
sequence = 0
network_events = []
launch_number = 0
received_cookies = {}


class SiteHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        received_cookies[self.path] = self.headers.get("Cookie", "")
        body = b"<!doctype html><meta charset=utf-8><title>Soulu storage test</title>"
        self.send_response(200)
        if self.path.startswith("/seed"):
            # Deliberately a session cookie: no Expires or Max-Age.
            self.send_header("Set-Cookie", f"soulu_auth={COOKIE_VALUE}; Path=/; HttpOnly; SameSite=Lax")
            self.send_header("Set-Cookie", f"soulu_persistent={COOKIE_VALUE}; Path=/; HttpOnly; SameSite=Lax; Max-Age=86400")
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
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        for target in targets(timeout=max(1, deadline - time.monotonic())):
            if target.get("type") == "page" and "/ui/start.html" in target.get("url", ""):
                return websocket.create_connection(
                    target["webSocketDebuggerUrl"], timeout=30,
                    origin=f"http://127.0.0.1:{DEBUG_PORT}")
        time.sleep(0.25)
    raise AssertionError("CEF content target did not appear")


def command(ws, method, params=None):
    global sequence
    sequence += 1
    ident = sequence
    ws.send(json.dumps({"id": ident, "method": method, "params": params or {}}))
    while True:
        response = json.loads(ws.recv())
        if response.get("method") == "Network.requestWillBeSentExtraInfo":
            network_events.append(response["params"])
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
    user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
    user32.IsWindowVisible.argtypes = [ctypes.c_void_p]
    user32.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    handles = []
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    @callback_type
    def callback(hwnd, _):
        pid = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == process.pid and user32.IsWindowVisible(hwnd):
            handles.append(hwnd)
            return False
        return True

    user32.EnumWindows(callback, 0)
    if not handles:
        raise AssertionError("Soulu top-level window was not found")
    child_handles = descendant_handles(process.pid)
    user32.PostMessageW(handles[0], 0x0010, 0, 0)  # WM_CLOSE
    exit_code = process.wait(timeout=30)
    if exit_code:
        diagnostic = subprocess.run([
            "powershell", "-NoProfile", "-Command",
            "Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000} -MaxEvents 3 -ErrorAction SilentlyContinue | Select-Object TimeCreated,Message | Format-List"
        ], capture_output=True, text=True, errors="replace", timeout=20)
        print(diagnostic.stdout, flush=True)
    assert exit_code == 0, f"Soulu exited abnormally: {process.returncode}"
    deadline = time.monotonic() + 30
    kernel32 = ctypes.windll.kernel32
    try:
        for pid, handle in child_handles:
            result = kernel32.WaitForSingleObject(handle, max(0, int((deadline - time.monotonic()) * 1000)))
            assert result == 0, f"CEF child {pid} still running after normal shutdown"
    finally:
        for _, handle in child_handles:
            kernel32.CloseHandle(handle)


def descendant_handles(parent_pid):
    class ProcessEntry(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD),
                    ("pid", wintypes.DWORD), ("heap", ctypes.c_size_t),
                    ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
                    ("parent", wintypes.DWORD), ("priority", wintypes.LONG),
                    ("flags", wintypes.DWORD), ("exe", wintypes.WCHAR * 260)]
    kernel32 = ctypes.windll.kernel32
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel32.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    snapshot = kernel32.CreateToolhelp32Snapshot(2, 0)
    entry = ProcessEntry()
    entry.size = ctypes.sizeof(entry)
    parents = {}
    try:
        more = kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
        while more:
            parents[entry.pid] = entry.parent
            more = kernel32.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        kernel32.CloseHandle(snapshot)
    descendants = {parent_pid}
    while True:
        expanded = descendants | {pid for pid, parent in parents.items() if parent in descendants}
        if expanded == descendants:
            break
        descendants = expanded
    result = []
    for pid in descendants - {parent_pid}:
        handle = kernel32.OpenProcess(0x00100000, False, pid)
        if handle:
            result.append((pid, handle))
    return result


def launch(executable, data_root):
    global launch_number
    launch_number += 1
    environment = os.environ.copy()
    environment["SOULU_UI_TEST_PORT"] = str(DEBUG_PORT)
    environment["LOCALAPPDATA"] = str(data_root)
    return subprocess.Popen([executable, f"--log-file={data_root / 'cef-debug.log'}",
                             f"--log-net-log={data_root / f'netlog-{launch_number}.json'}",
                             "--net-log-capture-mode=Everything",
                             "--vmodule=*cookie*=2,*os_crypt*=2"], env=environment)


def verify_profile_cookies(data_root):
    # Inspect the content profile, never the shell/global cookie manager.
    profile = data_root / "Soulu" / "User Data" / "Profiles" / "personal"
    database = profile / "Network" / "Cookies"
    assert database.is_file(), f"Content profile cookie database missing: {database}"
    with sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True) as connection:
        rows = dict(connection.execute(
            "SELECT name, is_persistent FROM cookies WHERE host_key = ?",
            ("127.0.0.1",)))
    assert rows.get("soulu_auth") == 0, rows
    assert rows.get("soulu_persistent") == 1, rows


def profile_diagnostics(data_root):
    database = data_root / "Soulu" / "User Data" / "Profiles" / "personal" / "Network" / "Cookies"
    if database.exists():
        with sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True) as connection:
            print(json.dumps({"cookie_database_rows": connection.execute(
                "SELECT name, is_persistent, has_expires, length(value), length(encrypted_value), hex(substr(encrypted_value,1,3)) FROM cookies"
            ).fetchall(), "cookie_database_meta": connection.execute("SELECT * FROM meta").fetchall()}), flush=True)
    prefs_path = database.parent.parent / "Preferences"
    if prefs_path.exists():
        prefs = json.loads(prefs_path.read_text(encoding="utf-8"))
        print(json.dumps({"session_prefs": prefs.get("session"), "profile_prefs": prefs.get("profile", {}).get("exit_type"),
                          "cookie_policy": prefs.get("profile", {}).get("default_content_setting_values")}), flush=True)
    for state_path in data_root.rglob("Local State"):
        state = json.loads(state_path.read_text(encoding="utf-8"))
        print(json.dumps({"local_state": str(state_path),
                          "os_crypt_fingerprints": {key: hashlib.sha256(str(value).encode()).hexdigest()
                                                    for key, value in state.get("os_crypt", {}).items()}}), flush=True)
    context_path = data_root / "Soulu" / "User Data" / "auth-context.json"
    context = json.loads(context_path.read_text())
    print(json.dumps({"actual_content_context": context}), flush=True)
    assert context["same_context"] and context["profile_id"] == "personal", context
    assert Path(context["cache_path"]) == data_root / "Soulu" / "User Data" / "Profiles" / "personal", context


def run(data_root):
    if len(sys.argv) != 2:
        raise SystemExit("usage: test-cef-storage.py <Soulu.exe>")
    executable = os.path.abspath(sys.argv[1])
    site_port = free_port()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", site_port), SiteHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f"http://127.0.0.1:{site_port}"

    first = launch(executable, data_root)
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
        verify_profile_cookies(data_root)
        profile_diagnostics(data_root)
    finally:
        if first.poll() is None:
            first.kill()

    second = launch(executable, data_root)
    try:
        ws = page_socket()
        profile_diagnostics(data_root)
        command(ws, "Network.enable")
        navigate(ws, origin + "/verify")
        local_value = evaluate(ws, "localStorage.getItem('soulu-test')")
        cookies = command(ws, "Network.getAllCookies").get("cookies", [])
        command(ws, "IndexedDB.enable")
        frame_id = command(ws, "Page.getFrameTree")["frameTree"]["frame"]["id"]
        storage_key = command(ws, "Storage.getStorageKeyForFrame", {
            "frameId": frame_id
        })["storageKey"]
        databases = command(ws, "IndexedDB.requestDatabaseNames", {
            "storageKey": storage_key
        }).get("databaseNames", [])
        assert "soulu-test-db" in databases, databases
        entries = command(ws, "IndexedDB.requestData", {
            "storageKey": storage_key,
            "databaseName": "soulu-test-db",
            "objectStoreName": "values",
            "skipCount": 0,
            "pageSize": 10,
        }).get("objectStoreDataEntries", [])
        indexed_value = next((
            item.get("value", {}).get("value")
            for item in entries
            if item.get("key", {}).get("value") == "auth"
        ), None)
        cookie_value = next((item["value"] for item in cookies if item["name"] == "soulu_auth"), None)
        print(json.dumps({"restored_cookies": cookies, "request_cookie_diagnostics": network_events,
                          "server_requests": received_cookies, "localStorage": local_value,
                          "IndexedDB": indexed_value}), flush=True)
        assert cookie_value == COOKIE_VALUE, f"session cookie missing after restart: {cookie_value!r}"
        assert any(item["name"] == "soulu_persistent" and item["value"] == COOKIE_VALUE for item in cookies), cookies
        verify_cookie = received_cookies.get("/verify", "")
        assert f"soulu_auth={COOKIE_VALUE}" in verify_cookie, received_cookies
        assert f"soulu_persistent={COOKIE_VALUE}" in verify_cookie, received_cookies
        assert local_value == STORAGE_VALUE, local_value
        assert indexed_value == IDB_VALUE, indexed_value
        print(json.dumps({
            "session_cookie": "preserved",
            "cookie_sent_to_server": True,
            "localStorage": "preserved",
            "IndexedDB": "preserved",
            "clean_restart": True,
            "content_profile_cookie_database": "verified",
            "persistent_cookie": "preserved",
        }))
        ws.close()
        close_normally(second)
    finally:
        print(f"Restarted Soulu exit status before cleanup: {second.poll()}", flush=True)
        if second.poll() is None:
            try:
                close_normally(second)
            except Exception as error:
                print(f"Cleanup failed: {error}", flush=True)
                second.kill()
                second.wait(timeout=30)
        server.shutdown()


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="soulu-auth-regression-") as directory:
        try:
            run(Path(directory))
        finally:
            diagnostic = Path(directory) / "cef-debug.log"
            if diagnostic.exists():
                print("\n".join(diagnostic.read_text(errors="replace").splitlines()[-80:]), flush=True)
            for netlog in Path(directory).glob("netlog-*.json"):
                try:
                    data = json.loads(netlog.read_text())
                    event_types = {value: key for key, value in data["constants"]["logEventTypes"].items()}
                    events = [{"type": event_types.get(event["type"]), "params": event.get("params"), "source": event["source"], "phase": event["phase"]}
                              for event in data["events"] if "COOKIE" in event_types.get(event["type"], "")]
                    print(json.dumps({"netlog": netlog.name, "cookie_events": events}), flush=True)
                except (ValueError, KeyError) as error:
                    print(f"Netlog unavailable: {error}", flush=True)
