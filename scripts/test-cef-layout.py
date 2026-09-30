"""Real Win32 modal sizing loop + Chromium viewport, with a fresh test profile.

The check queries CDP while Windows is in its actual native sizing loop.
Bounds are driven by SetWindowPos rather than desktop mouse injection, which
does not work on service-hosted runners. Checking only after the loop exits
would hide the Chromium nested-loop starvation this test targets.
Visual quality/backdrop and startup flash still require human review.
"""
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import websocket
from PIL import ImageGrab

u = C.WinDLL('user32', use_last_error=True)
u.FindWindowW.argtypes = [W.LPCWSTR, W.LPCWSTR]
u.FindWindowW.restype = W.HWND
u.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u.GetParent.argtypes = [W.HWND]
u.GetParent.restype = W.HWND
u.GetDpiForWindow.argtypes = [W.HWND]
u.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
u.ShowWindow.argtypes = [W.HWND, C.c_int]
u.SetForegroundWindow.argtypes = [W.HWND]
u.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
u.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.MapWindowPoints.argtypes = [W.HWND, W.HWND, C.POINTER(W.POINT), W.UINT]
CALLBACK = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
u.EnumChildWindows.argtypes = [W.HWND, CALLBACK, W.LPARAM]
u.IsWindowVisible.argtypes = [W.HWND]
u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
u.GetWindowThreadProcessId.restype = W.DWORD
class GUIINFO(C.Structure):
    _fields_ = [('cbSize', W.DWORD), ('flags', W.DWORD),
               ('hwndActive', W.HWND), ('hwndFocus', W.HWND),
               ('hwndCapture', W.HWND), ('hwndMenuOwner', W.HWND),
               ('hwndMoveSize', W.HWND), ('hwndCaret', W.HWND), ('rcCaret', W.RECT)]
u.GetGUIThreadInfo.argtypes = [W.DWORD, C.POINTER(GUIINFO)]
seq = 0
checks = []
out = Path(sys.argv[2] if len(sys.argv) > 2 else 'layout-evidence')
out.mkdir(parents=True, exist_ok=True)

def wait(fn, timeout=20):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            value = fn()
            if value:
                return value
        except (OSError, AssertionError, KeyError) as e:
            last = e
        time.sleep(.04)
    raise AssertionError(f'timed out: {last}')

def targets():
    with urllib.request.urlopen(base + '/json/list', timeout=2) as r:
        return json.load(r)

def connect(fragment, exclude=()):
    target = wait(lambda: next((t for t in targets() if fragment in t.get('url', '') and t['type'] == 'page' and t['id'] not in exclude), None))
    return websocket.create_connection(target['webSocketDebuggerUrl'], timeout=4, origin=base)

def evaluate(ws, expression):
    global seq
    seq += 1
    ident = seq
    ws.send(json.dumps({'id': ident, 'method': 'Runtime.evaluate', 'params': {
        'expression': expression, 'returnByValue': True, 'awaitPromise': True}}))
    while True:
        r = json.loads(ws.recv())
        if r.get('id') == ident:
            assert 'error' not in r, r
            assert 'exceptionDetails' not in r['result'], r
            return r['result']['result'].get('value')

def client(hwnd):
    r = W.RECT()
    assert u.GetClientRect(hwnd, C.byref(r))
    return [r.right - r.left, r.bottom - r.top]

def child_rect(hwnd):
    r = W.RECT()
    assert u.GetWindowRect(hwnd, C.byref(r))
    pts = (W.POINT * 2)(W.POINT(r.left, r.top), W.POINT(r.right, r.bottom))
    u.MapWindowPoints(None, window, pts, 2)
    return [pts[0].x, pts[0].y, pts[1].x - pts[0].x, pts[1].y - pts[0].y]

def children():
    found = {}
    @CALLBACK
    def visit(hwnd, _):
        name = C.create_unicode_buffer(128)
        u.GetClassNameW(hwnd, name, len(name))
        if u.GetParent(hwnd) == window and u.IsWindowVisible(hwnd):
            found[name.value] = hwnd
        return True
    u.EnumChildWindows(window, visit, 0)
    return found

def check(label, sidebar=False, screenshot=False):
    def sample():
        width, height = client(window)
        scale = u.GetDpiForWindow(window) / 96
        ch = children()
        toolbar = child_rect(ch['SouluAlphaToolbar'])
        content = child_rect(ch['Chrome_WidgetWin_1'])
        left = round(276 * scale) if sidebar else 0
        top = round(48 * scale)
        assert content == [left, top, width - left, height - top], (label, content, width, height)
        assert toolbar == [0, 0, width, height if sidebar else top], (label, toolbar)
        dom = evaluate(shell, '({w:innerWidth,h:innerHeight,dpr:devicePixelRatio,toolbar:document.querySelector(".browser-toolbar").getBoundingClientRect().height})')
        assert abs(dom['toolbar'] - 48) <= 1, (label, dom)
        viewport = evaluate(page, '({w:innerWidth,h:innerHeight,dpr:devicePixelRatio})')
        assert abs(viewport['w'] * viewport['dpr'] - content[2]) <= 2, (label, viewport, content)
        assert abs(viewport['h'] * viewport['dpr'] - content[3]) <= 2, (label, viewport, content)
        assert abs(dom['w'] * dom['dpr'] - width) <= 2, (label, dom, width)
        return {'scenario': label, 'client': [width, height], 'shell': toolbar, 'content': content, 'viewport': viewport, 'shellDOM': dom}
    row = wait(sample)
    checks.append(row)
    if screenshot:
        r = W.RECT()
        u.GetWindowRect(window, C.byref(r))
        ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom)).save(out / (label + '.png'))
    print(json.dumps(row), flush=True)

def resize(width, height):
    assert u.SetWindowPos(window, None, 20, 20, width, height, 0x14)

def modal_drag(label, dx, dy, delay):
    resize(850, 580)
    check(label + '-before')
    thread = u.GetWindowThreadProcessId(window, None)
    def in_size_loop():
        info = GUIINFO()
        info.cbSize = C.sizeof(info)
        assert u.GetGUIThreadInfo(thread, C.byref(info))
        return bool(info.flags & 2) and info.hwndMoveSize == window
    # SC_SIZE | WMSZ_BOTTOMRIGHT enters DefWindowProc's native modal loop.
    assert u.PostMessageW(window, 0x112, 0xF008, 0)
    try:
        wait(in_size_loop, timeout=5)
        for step in range(1, 9):
            resize(850 + dx * step // 8, 580 + dy * step // 8)
            time.sleep(delay)
        before = client(window)
        assert before != [850, 580], ('native sizing did not start', before)
        assert in_size_loop(), 'native sizing loop exited before viewport check'
        # This must finish before the native loop exits.
        check(label + '-loop-active', screenshot=True)
        evaluate(shell, 'document.querySelector(".browser-toolbar").dataset.layoutProbe="held"')
        assert in_size_loop(), 'native sizing loop exited during CDP query'
    finally:
        u.PostMessageW(window, 0x100, 0x0D, 0)
        u.PostMessageW(window, 0x101, 0x0D, 0)
        wait(lambda: not in_size_loop(), timeout=5)
    check(label + '-after')

with socket.socket() as s:
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
base = f'http://127.0.0.1:{port}'
with tempfile.TemporaryDirectory(prefix='soulu-layout-') as profile:
    env = dict(os.environ, LOCALAPPDATA=profile, SOULU_UI_TEST_PORT=str(port))
    process = subprocess.Popen([str(Path(sys.argv[1]).resolve())], env=env)
    try:
        shell = connect('/ui/index.html')
        page = connect('/ui/start.html')
        window = wait(lambda: u.FindWindowW('SouluBrowserWindow', None))
        wait(lambda: u.IsWindowVisible(window))
        wait(lambda: evaluate(shell, 'Boolean(window.browserShell && document.querySelector(".browser-toolbar"))'))
        resize(850, 580)
        check('startup', screenshot=True)
        for label, dx, dy, delay in [('horizontal-slow', 220, 0, .10), ('horizontal-slow-shrink', -180, 0, .10),
                ('horizontal-fast', -180, 0, .01), ('vertical-slow', 0, 100, .10),
                ('vertical-slow-shrink', 0, -100, .10), ('vertical-fast', 0, -100, .01), ('diagonal', 180, 90, .04)]:
            modal_drag(label, dx, dy, delay)
        for cycle in range(3):
            u.ShowWindow(window, 3)
            check(f'maximize-{cycle}', screenshot=cycle == 0)
            u.ShowWindow(window, 9)
            check(f'restore-{cycle}', screenshot=cycle == 0)
        assert u.SetWindowPos(window, None, 70, 60, 0, 0, 0x15)
        check('move')
        evaluate(shell, 'window.browserShell.toggleSidebar()')
        check('sidebar-open', sidebar=True, screenshot=True)
        resize(980, 650)
        check('sidebar-resize', sidebar=True)
        evaluate(shell, 'window.browserShell.toggleSidebar()')
        check('sidebar-close')
        first_id = evaluate(shell, 'window.browserShell.getState().then(s=>s.activeTabId)')
        old_targets = {t['id'] for t in targets()}
        first_page = page
        evaluate(shell, 'window.browserShell.newTab()')
        wait(lambda: evaluate(shell, 'window.browserShell.getState().then(s=>s.tabs.length===2)'))
        page = connect('/ui/start.html', exclude=old_targets)
        check('new-tab')
        evaluate(shell, f'window.browserShell.switchTab({first_id})')
        page.close()
        page = first_page
        check('switch-tab')
        # Websites are evidence, separate from deterministic native regressions.
        for name, url in [('google', 'https://www.google.com/'), ('apple', 'https://www.apple.com/'), ('youtube', 'https://www.youtube.com/')]:
            evaluate(shell, f'window.browserShell.navigate({json.dumps(url)})')
            page.close()
            page = connect(url.split('/')[2])
            wait(lambda: evaluate(page, 'document.readyState === "complete"'), timeout=40)
            resize(850, 580)
            check(name, screenshot=True)
            resize(1040, 700)
            check(name + '-resize', screenshot=True)
        diagnostics = evaluate(shell, 'new Promise((resolve,reject)=>cefQuery({request:JSON.stringify({action:"browser.surfaceDiagnostics"}),onSuccess:r=>resolve(JSON.parse(r)),onFailure:(_,m)=>reject(m)}))')
        assert diagnostics['paintError'] == 0, diagnostics
        (out / 'report.json').write_text(json.dumps({'checks': checks, 'diagnostics': diagnostics, 'visualReviewRequired': True}, indent=2), encoding='utf-8')
    finally:
        (out / 'checks.json').write_text(json.dumps(checks, indent=2), encoding='utf-8')
        process.terminate()
        try:
            process.wait(timeout=12)
        except subprocess.TimeoutExpired:
            process.kill()

