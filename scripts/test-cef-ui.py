"""Exercise real CEF settings-page events and inspect the separate shell browser."""
import json
import os
import time
import urllib.request
import websocket

PORT = int(os.environ.get("SOULU_UI_TEST_PORT", "9223"))
BASE = f"http://127.0.0.1:{PORT}"
seq = 0

def targets():
    with urllib.request.urlopen(BASE + "/json/list", timeout=3) as response:
        return json.load(response)

def find_target(fragment, timeout=45):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            for target in targets():
                if fragment in target.get("url", "") and target.get("type") == "page":
                    return websocket.create_connection(
                        target["webSocketDebuggerUrl"], timeout=8,
                        origin=f"http://127.0.0.1:{PORT}")
        except (OSError, KeyError):
            pass
        time.sleep(.25)
    raise AssertionError(f"CEF target {fragment!r} did not appear")

def evaluate(ws, expression):
    global seq
    seq += 1
    ident = seq
    ws.send(json.dumps({"id": ident, "method": "Runtime.evaluate",
                        "params": {"expression": expression,
                                   "returnByValue": True, "awaitPromise": True}}))
    while True:
        response = json.loads(ws.recv())
        if response.get("id") != ident:
            continue
        if "error" in response:
            raise AssertionError(response["error"])
        result = response["result"]
        if "exceptionDetails" in result:
            raise AssertionError(result["exceptionDetails"])
        return result["result"].get("value")

def wait_for(predicate, label, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except (OSError, KeyError):
            pass
        time.sleep(.25)
    raise AssertionError(f"UI state not reached: {label}")

shell = find_target("/ui/index.html")
settings = None
try:
    assert wait_for(lambda: evaluate(shell, "Boolean(window.browserShell && document.querySelector('.browser-toolbar'))"), "shell ready")
    evaluate(shell, "window.browserShell.openSettingsWindow()")
    settings = find_target("/ui/settings.html")
    assert wait_for(lambda: evaluate(settings, "document.body.classList.contains('ready')"), "settings ready")
    # These are real change/click handlers from the settings page, not a
    # direct write to settings.json or a synthetic state injected into shell.
    evaluate(settings, """(() => {
      const theme = document.querySelector('#theme');
      theme.value = 'dark';
      theme.dispatchEvent(new Event('change', {bubbles:true}));
      return true;
    })()""")
    wait_for(lambda: evaluate(shell, "document.body.dataset.theme === 'dark'"), "dark shell theme")
    evaluate(settings, """(() => {
      const checkbox = document.querySelector('#mattePanel');
      if (!checkbox.checked) checkbox.click();
      return true;
    })()""")
    wait_for(lambda: evaluate(shell, "document.body.dataset.matte === 'true'"), "matte shell")
    for key in ("vpnToolbarVisible", "showSidebar", "showBack",
                "showFavorites", "showNewTab", "showDownloads"):
        evaluate(settings, f"""(() => {{
          const control = document.querySelector('[data-setting="{key}"]');
          if (!control) throw new Error('Missing setting {key}');
          if (control.checked) control.click();
          return true;
        }})()""")
        wait_for(lambda key=key: evaluate(shell, f"document.body.dataset.{ 'showVpn' if key == 'vpnToolbarVisible' else key } === 'false'"), f"{key} hidden")
    styles = evaluate(shell, """(() => {
      const pick = selector => {
        const element = document.querySelector(selector);
        return element ? getComputedStyle(element).display : 'absent';
      };
      return {
        theme: document.body.dataset.theme,
        matte: document.body.dataset.matte,
        background: getComputedStyle(document.querySelector('.compact-toolbar')).backgroundColor,
        vpn: pick('#compactVpnButton'), sidebar: pick('#compactSidebarButton'),
        back: pick('#compactBackButton'), favorite: pick('.compact-active-tab [data-favorites]'),
        newTab: pick('#compactNewTabButton'), downloads: pick('#compactDownloadsButton')
      };
    })()""")
    print("CEF shell after settings-page interaction:", json.dumps(styles, ensure_ascii=False))
    assert styles["theme"] == "dark" and styles["matte"] == "true"
    assert styles["background"].startswith("rgba"), styles["background"]
    for key in ("vpn", "sidebar", "back", "favorite", "newTab", "downloads"):
        assert styles[key] == "none", f"{key} did not hide: {styles[key]}"
    # Check the reverse direction too: controls must reappear, not merely hide.
    for key, selector in (("vpnToolbarVisible", "#compactVpnButton"),
                          ("showSidebar", "#compactSidebarButton"),
                          ("showBack", "#compactBackButton"),
                          ("showNewTab", "#compactNewTabButton")):
        evaluate(settings, f"""(() => {{
          const control = document.querySelector('[data-setting="{key}"]');
          if (!control.checked) control.click();
          return true;
        }})()""")
        wait_for(lambda selector=selector: evaluate(shell, f"getComputedStyle(document.querySelector('{selector}')).display !== 'none'"), f"{key} restored")
    # Verify light and dark are distinct on the toolbar, not only in Settings.
    evaluate(settings, """(() => {
      const theme = document.querySelector('#theme');
      theme.value = 'light';
      theme.dispatchEvent(new Event('change', {bubbles:true}));
      return true;
    })()""")
    wait_for(lambda: evaluate(shell, "document.body.dataset.theme === 'light'"), "light shell theme")
    light = evaluate(shell, "getComputedStyle(document.querySelector('.compact-toolbar')).backgroundColor")
    assert light != styles["background"], "Toolbar gradient did not change with theme"
    evaluate(settings, """(() => {
      const theme = document.querySelector('#theme');
      theme.value = 'dark';
      theme.dispatchEvent(new Event('change', {bubbles:true}));
      return true;
    })()""")
    wait_for(lambda: evaluate(shell, "document.body.dataset.theme === 'dark'"), "dark restored")
    persisted = evaluate(shell, """(async () => await window.browserShell.getSettings())()""")
    assert persisted["theme"] == "dark" and persisted["mattePanel"] is True
    assert persisted["vpnToolbarVisible"] is True
    # The native OSR buffer must contain actual non-opaque toolbar pixels.
    surface = evaluate(shell, "new Promise((resolve,reject)=>cefQuery({request:JSON.stringify({action:'browser.surfaceDiagnostics'}),onSuccess:s=>resolve(JSON.parse(s)),onFailure:reject}))")
    print("Native toolbar alpha:", surface)
    assert surface["windowless"] and surface["paintCount"] > 0
    assert 0 < surface["toolbarAlpha"] < 200, surface

    # Preserve the exact input node and text through native state updates.
    evaluate(shell, "(() => {const x=document.querySelector('#compactAddress');x.focus();x.value='example.org/new-address';window.testAddressNode=x;return true})()")
    evaluate(shell, "window.browserShell.setSettings({showBack:false})")
    evaluate(shell, "window.browserShell.setSettings({showBack:true})")
    time.sleep(2)
    assert evaluate(shell, "document.activeElement===window.testAddressNode && window.testAddressNode.isConnected && window.testAddressNode.value==='example.org/new-address'"), "Address focus or draft lost during state updates"
    # A domain completion must remain usable without relying on a public server.
    local = evaluate(shell, "Promise.race([window.browserShell.suggestions('example.org'),new Promise((_,reject)=>setTimeout(()=>reject(Error('suggestion timeout')),12000))])")
    assert any(row.get('url') == 'https://example.org' for row in local), local
    # Exercise OSR mouse hit testing and native key forwarding, not only DOM focus.
    import ctypes
    from ctypes import wintypes
    user=ctypes.windll.user32
    user.FindWindowW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR];user.FindWindowW.restype=wintypes.HWND
    user.GetWindowRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)]
    user.SetForegroundWindow.argtypes=[wintypes.HWND]
    user.GetDpiForWindow.argtypes=[wintypes.HWND]
    hwnd=user.FindWindowW('SouluBrowserWindow',None)
    assert hwnd, 'Browser HWND missing'
    rect=wintypes.RECT();user.GetWindowRect(hwnd,ctypes.byref(rect))
    user.SetForegroundWindow(hwnd)
    pos=evaluate(shell,"(() => {const r=document.querySelector('#compactAddress').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()")
    scale=user.GetDpiForWindow(hwnd)/96
    user.SetCursorPos(rect.left+round((6+pos['x'])*scale),rect.top+round((6+pos['y'])*scale))
    user.mouse_event(2,0,0,0,0);user.mouse_event(4,0,0,0,0);time.sleep(.2)
    user.keybd_event(0x11,0,0,0);user.keybd_event(0x41,0,0,0);user.keybd_event(0x41,0,2,0);user.keybd_event(0x11,0,2,0)
    for character in 'SOULU TEST':
        user.keybd_event(ord(character),0,0,0);user.keybd_event(ord(character),0,2,0)
    time.sleep(2)
    typed=evaluate(shell,"document.querySelector('#compactAddress').value")
    assert typed.lower()=='soulu test', ('Physical address input failed',typed)
    prior=evaluate(shell,"window.browserShell.getState()")
    user.keybd_event(0x0D,0,0,0);user.keybd_event(0x0D,0,2,0)
    wait_for(lambda: evaluate(shell,"window.browserShell.getState().then(s=>s.page.url.includes('soulu') && !s.page.url.includes('settings.html'))"),'typed search navigation')
    after_navigation=evaluate(shell,"window.browserShell.getState()")
    assert len(prior['tabs'])==len(after_navigation['tabs']), 'Address navigation created another tab'
    print('Physical address click, keyboard input and same-tab navigation passed.')
    evaluate(shell, "window.browserShell.newTab()")
    blank = find_target('/ui/start.html')
    wait_for(lambda: evaluate(blank, "getComputedStyle(document.body).backgroundColor === 'rgb(8, 9, 11)'"), 'black blank tab')
    blank.close()

    # Resize using the real desktop pointer at the right frame edge.
    import ctypes
    from ctypes import wintypes
    user=ctypes.windll.user32
    user.FindWindowW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR];user.FindWindowW.restype=wintypes.HWND
    hwnd=user.FindWindowW('SouluBrowserWindow',None)
    assert hwnd, 'Browser HWND missing'
    before=wintypes.RECT();user.GetWindowRect(hwnd,ctypes.byref(before));user.SetForegroundWindow(hwnd)
    user.SetCursorPos(before.right-2,(before.top+before.bottom)//2)
    user.mouse_event(2,0,0,0,0);time.sleep(.2)
    user.SetCursorPos(before.right-142,(before.top+before.bottom)//2);time.sleep(.5)
    user.mouse_event(4,0,0,0,0);time.sleep(.4)
    after=wintypes.RECT();user.GetWindowRect(hwnd,ctypes.byref(after))
    from PIL import ImageGrab
    ImageGrab.grab(bbox=(after.left,after.top,after.right,after.bottom)).save('browser/artifacts/browser-dark.png')
    import base64
    print('SOULU_SCREENSHOT:browser-dark='+base64.b64encode(open('browser/artifacts/browser-dark.png','rb').read()).decode())
    print('Mouse resize:',before.right-before.left,'->',after.right-after.left)
    assert (before.right-before.left)-(after.right-after.left)>80, 'Browser frame cannot resize by mouse'
    print("CEF settings, alpha rendering, address editing, completions, black blank tab and mouse resize passed.")
finally:
    if settings: settings.close()
    shell.close()

