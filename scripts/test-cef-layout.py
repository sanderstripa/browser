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
dwm = C.WinDLL('dwmapi')
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
u.EnumWindows.argtypes = [CALLBACK, W.LPARAM]
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
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(base + '/json/list', timeout=2) as r:
        return json.load(r)

def connect(fragment, exclude=()):
    target = wait(lambda: next((t for t in targets() if fragment in t.get('url', '') and t['type'] == 'page' and t['id'] not in exclude), None))
    return websocket.create_connection(target['webSocketDebuggerUrl'], timeout=4, origin=base, http_no_proxy=['127.0.0.1', 'localhost'])

def own_window():
    result = []
    @CALLBACK
    def visit(hwnd, _):
        pid = W.DWORD()
        u.GetWindowThreadProcessId(hwnd, C.byref(pid))
        name = C.create_unicode_buffer(128)
        u.GetClassNameW(hwnd, name, len(name))
        if pid.value == process.pid and name.value == 'SouluBrowserWindow':
            result.append(hwnd)
        return True
    u.EnumWindows(visit, 0)
    return result[0] if result else None

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
        # Alloy embeds a CefBrowserWindow; Chrome runtime used WidgetWin_1.
        # Measure the actual content host in either runtime, retaining the
        # same bounds, viewport and live modal-loop assertions below.
        host = ch['CefBrowserWindow'] if 'CefBrowserWindow' in ch else ch['Chrome_WidgetWin_1']
        content = child_rect(host)
        left = round(276 * scale) if sidebar else 0
        layout = evaluate(shell, 'document.body.dataset.layout')
        base_height = 82 if layout == 'classic' else 48
        bar_visible = evaluate(shell, 'document.body.dataset.bookmarksBar === "true"')
        toolbar_height = base_height + (28 if bar_visible else 0)
        top = round(toolbar_height * scale)
        assert content == [left, top, width - left, height - top], (label, content, width, height)
        assert toolbar == [0, 0, width, height if sidebar else top], (label, toolbar)
        dom = evaluate(shell, '({w:innerWidth,h:innerHeight,dpr:devicePixelRatio,toolbar:document.querySelector(document.body.dataset.layout==="classic"?".classic-toolbar":".compact-toolbar").getBoundingClientRect().height})')
        # Compact mode keeps the bar as a sibling of its 48px header.
        header_height = toolbar_height if layout == 'classic' else base_height
        assert abs(dom['toolbar'] - header_height) <= 1, (label, dom)
        if bar_visible:
            assert evaluate(shell, 'document.querySelector(".bookmarks-bar").getBoundingClientRect().height') == 28
        viewport = evaluate(page, '({w:innerWidth,h:innerHeight,dpr:devicePixelRatio})')
        assert abs(viewport['w'] * viewport['dpr'] - content[2]) <= 2, (label, viewport, content)
        assert abs(viewport['h'] * viewport['dpr'] - content[3]) <= 2, (label, viewport, content)
        assert abs(dom['w'] * dom['dpr'] - width) <= 2, (label, dom, width)
        assert abs(dom['h'] * dom['dpr'] - toolbar[3]) <= 2, (label, dom, toolbar)
        return {'scenario': label, 'client': [width, height], 'shell': toolbar, 'content': content, 'viewport': viewport, 'shellDOM': dom}
    row = wait(sample)
    if screenshot:
        # DOM/viewport acknowledgement precedes raster and DWM presentation.
        # Synchronize evidence with two renderer frames and compositor flush;
        # an immediate screenshot can still contain the previous backing frame.
        frame = 'new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))'
        frame_start = time.monotonic()
        evaluate(page, frame)
        evaluate(shell, frame)
        assert dwm.DwmFlush() == 0
        row['presentationWaitMs'] = round((time.monotonic() - frame_start) * 1000)
        r = W.RECT()
        u.GetWindowRect(window, C.byref(r))
        ImageGrab.grab(include_layered_windows=True, bbox=(r.left, r.top, r.right, r.bottom)).save(out / (label + '.png'))
    checks.append(row)
    print(json.dumps(row), flush=True)

def resize(width, height):
    assert u.SetWindowPos(window, None, 20, 20, width, height, 0x14)

def modal_drag(label, dx, dy, delay):
    resize(850, 580)
    check(label + '-before')
    if os.environ.get('SOULU_LAYOUT_SKIP_MODAL') == '1':
        # Optional local evidence collection without taking over the desktop's
        # native sizing loop. These samples do not count as modal-loop tests.
        for step in range(1, 9):
            resize(850 + dx * step // 8, 580 + dy * step // 8)
            time.sleep(delay)
        check(label + '-programmatic', screenshot=True)
        return
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
window = None
with tempfile.TemporaryDirectory(prefix='soulu-layout-', ignore_cleanup_errors=True) as profile:
    # This suite exercises ordinary browsing; seed an existing install fixture.
    baseline=Path(profile)/'Soulu'/'User Data'
    baseline.mkdir(parents=True)
    (baseline/'settings.json').write_text('{}')
    env = dict(os.environ, LOCALAPPDATA=profile, SOULU_UI_TEST_PORT=str(port))
    process = subprocess.Popen([str(Path(sys.argv[1]).resolve())], env=env)
    try:
        shell = connect('/ui/index.html')
        page = connect('/ui/home.html')
        window = wait(own_window)
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
        page = connect('/ui/home.html', exclude=old_targets)
        check('new-tab')
        evaluate(shell, f'window.browserShell.switchTab({first_id})')
        page.close()
        page = first_page
        check('switch-tab')
        # Real native settings changes: both layouts, themes and alpha modes.
        for layout in ('classic', 'compact'):
            for theme in ('light', 'dark'):
                for matte in (False, True):
                    settings = {'layout': layout, 'theme': theme, 'mattePanel': matte}
                    evaluate(shell, f'window.browserShell.setSettings({json.dumps(settings)})')
                    wait(lambda: evaluate(shell, f'document.body.dataset.layout==={json.dumps(layout)} && document.body.dataset.theme==={json.dumps(theme)} && document.body.dataset.matte==={json.dumps(str(matte).lower())}'))
                    label = f'{layout}-{theme}-matte-{matte}'
                    check(label, screenshot=True)
                    style = evaluate(shell, '''(() => {
                        const t=document.querySelector(document.body.dataset.layout==='classic'?'.classic-toolbar':'.compact-toolbar'),s=getComputedStyle(t);
                        return {background:s.backgroundColor,image:s.backgroundImage,blur:s.backdropFilter};
                    })()''')
                    expected = 'rgb(255, 255, 255)' if theme == 'light' else 'rgb(8, 9, 11)'
                    if not matte:
                        assert style == {'background':expected,'image':'none','blur':'none'}, (label, style)
                        def opaque_surface():
                            v = evaluate(shell, 'new Promise((resolve,reject)=>cefQuery({request:JSON.stringify({action:"browser.surfaceDiagnostics"}),onSuccess:r=>resolve(JSON.parse(r)),onFailure:(_,m)=>reject(m)}))')
                            return v if v['toolbarAlpha'] == 255 and not v['nativeBlur'] else None
                        wait(opaque_surface)
                    else:
                        assert style['background'].startswith('rgba('), (label, style)
                    if layout == 'classic':
                        structure = evaluate(shell, '''(() => {
                            const r=s=>document.querySelector(s).getBoundingClientRect();
                            const a=r('.classic-address-pill'),n=r('.classic-main-row'),t=r('.classic-toolbar .tabs-zone'),p=r('.classic-tab');
                            return {nav:n.height,tabs:t.height,addressBottom:a.bottom,tabsTop:t.top,tabBottom:p.bottom,tabsBottom:t.bottom,center:a.x+a.width/2,width:innerWidth,tabsBackground:getComputedStyle(document.querySelector('.classic-toolbar .tabs-zone')).backgroundColor};
                        })()''')
                        assert structure['nav'] == 48 and structure['tabs'] == 34, structure
                        assert structure['addressBottom'] <= structure['tabsTop'], structure
                        assert structure['tabBottom'] <= structure['tabsBottom'], structure
                        assert abs(structure['center'] - structure['width']/2) <= 1, structure
                        assert structure['tabsBackground'] == 'rgba(0, 0, 0, 0)', structure
                        resize(640, 480)
                        check(label+'-narrow')
                        u.ShowWindow(window, 3)
                        check(label+'-maximize')
                        u.ShowWindow(window, 9)
                        check(label+'-restore')
                        resize(980, 650)
        evaluate(shell, 'window.browserShell.setSettings({layout:"classic",theme:"light",mattePanel:false})')
        wait(lambda: evaluate(shell, 'document.body.dataset.layout==="classic"'))
        for _ in range(12):
            evaluate(shell, 'window.browserShell.newTab()')
        wait(lambda: evaluate(shell, 'document.querySelectorAll(".classic-tab").length===14'))
        resize(640, 480)
        def overflow_sample():
            return evaluate(shell, '''(() => {
                const strip=document.querySelector('#tabStrip'),s=strip.getBoundingClientRect(),a=strip.querySelector('.active').getBoundingClientRect(),b=document.querySelector('#newTabButton').getBoundingClientRect();
                return {overflow:strip.scrollWidth>strip.clientWidth,visible:a.left>=s.left-1&&a.right<=s.right+1,buttonFits:b.left>=s.right&&b.right<=innerWidth,widths:[...strip.children].map(t=>t.getBoundingClientRect().width)};
            })()''')
        wait(lambda: (v if v['visible'] else None) if (v:=overflow_sample()) else None)
        overflow = overflow_sample()
        assert overflow['overflow'] and overflow['buttonFits'], overflow
        assert all(112 <= w <= 220 for w in overflow['widths']), overflow
        evaluate(shell, f'window.browserShell.switchTab({first_id})')
        wait(lambda: overflow_sample()['visible'])
        check('classic-many-tabs', screenshot=True)
        evaluate(shell, 'window.browserShell.toggleSidebar()')
        check('classic-sidebar', sidebar=True)
        evaluate(shell, 'window.browserShell.toggleSidebar()')
        modal_drag('classic-horizontal', 220, 0, .04)
        modal_drag('classic-vertical', 0, 100, .04)
        # System follows Chromium's effective OS color scheme.
        evaluate(shell, 'window.browserShell.setSettings({theme:"system",mattePanel:false})')
        wait(lambda: evaluate(shell, 'document.body.dataset.theme==="system"'))
        assert evaluate(shell, 'getComputedStyle(document.querySelector(".classic-toolbar")).backgroundColor === (matchMedia("(prefers-color-scheme:dark)").matches?"rgb(8, 9, 11)":"rgb(255, 255, 255)")')
        evaluate(shell, 'window.browserShell.setSettings({layout:"compact",theme:"light",mattePanel:true})')
        wait(lambda: evaluate(shell, 'document.body.dataset.layout==="compact"'))
        check('compact-return')
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
        (out / 'report.json').write_text(json.dumps({'checks': checks, 'diagnostics': diagnostics,
            'nativeSizingLoopTested': os.environ.get('SOULU_LAYOUT_SKIP_MODAL') != '1',
            'visualReviewRequired': True}, indent=2), encoding='utf-8')
    finally:
        (out / 'checks.json').write_text(json.dumps(checks, indent=2), encoding='utf-8')
        if window:
            u.PostMessageW(window, 0x10, 0, 0)
        try:
            process.wait(timeout=12)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
