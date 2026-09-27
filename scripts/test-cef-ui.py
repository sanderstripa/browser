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
    user.SetCursorPos(rect.left+round(pos['x']*scale),rect.top+round(pos['y']*scale))
    user.mouse_event(2,0,0,0,0);user.mouse_event(4,0,0,0,0);time.sleep(.2)
    user.keybd_event(0x11,0,0,0);user.keybd_event(0x41,0,0,0);user.keybd_event(0x41,0,2,0);user.keybd_event(0x11,0,2,0)
    for character in 'SOULU TEST':
        user.keybd_event(ord(character),0,0,0);user.keybd_event(ord(character),0,2,0)
    time.sleep(2)
    typed=evaluate(shell,"document.querySelector('#compactAddress').value")
    assert typed.lower()=='soulu test', ('Physical address input failed',typed)
    assert evaluate(shell,"(() => {const row=document.querySelector('#compactSuggestions.visible .suggestion-row');if(!row)return false;const r=row.getBoundingClientRect();return !!document.elementFromPoint(r.x+r.width/2,r.y+r.height/2)?.closest('.suggestion-row')})()"), 'Suggestions are clipped or cannot receive mouse clicks'
    prior=evaluate(shell,"window.browserShell.getState()")
    user.keybd_event(0x0D,0,0,0);user.keybd_event(0x0D,0,2,0)
    wait_for(lambda: evaluate(shell,"window.browserShell.getState().then(s=>s.page.url.includes('soulu') && !s.page.url.includes('settings.html'))"),'typed search navigation')
    after_navigation=evaluate(shell,"window.browserShell.getState()")
    assert len(prior['tabs'])==len(after_navigation['tabs']), 'Address navigation created another tab'
    print('Physical address click, keyboard input and same-tab navigation passed.')
    evaluate(shell, "window.browserShell.newTab()")
    blank = find_target('/ui/start.html')
    wait_for(lambda: evaluate(blank, "Boolean(document.body) && getComputedStyle(document.body).backgroundColor === 'rgb(8, 9, 11)'"), 'black blank tab')
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
    # Pixel-level proof: change the actual window BEHIND Soulu, then check that
    # the toolbar changes and fine stripes are blurred rather than copied sharply.
    from PIL import ImageStat
    user.CreateWindowExW.argtypes=[wintypes.DWORD,wintypes.LPCWSTR,wintypes.LPCWSTR,wintypes.DWORD,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.HWND,wintypes.HMENU,wintypes.HINSTANCE,ctypes.c_void_p]
    user.CreateWindowExW.restype=wintypes.HWND
    user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT]
    user.SetClassLongPtrW.argtypes=[wintypes.HWND,ctypes.c_int,ctypes.c_ssize_t];user.SetClassLongPtrW.restype=ctypes.c_ssize_t
    user.RedrawWindow.argtypes=[wintypes.HWND,ctypes.c_void_p,ctypes.c_void_p,wintypes.UINT]
    user.DestroyWindow.argtypes=[wintypes.HWND]
    gdi=ctypes.windll.gdi32
    gdi.CreateSolidBrush.argtypes=[wintypes.DWORD];gdi.CreateSolidBrush.restype=wintypes.HBRUSH
    gdi.CreateBitmap.argtypes=[ctypes.c_int,ctypes.c_int,wintypes.UINT,wintypes.UINT,ctypes.c_void_p];gdi.CreateBitmap.restype=wintypes.HBITMAP
    gdi.CreatePatternBrush.argtypes=[wintypes.HBITMAP];gdi.CreatePatternBrush.restype=wintypes.HBRUSH
    gdi.DeleteObject.argtypes=[wintypes.HGDIOBJ]
    WNDPROC=ctypes.WINFUNCTYPE(ctypes.c_ssize_t,wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM)
    class WNDCLASS(ctypes.Structure):
        _fields_=[('style',wintypes.UINT),('lpfnWndProc',WNDPROC),('cbClsExtra',ctypes.c_int),('cbWndExtra',ctypes.c_int),('hInstance',wintypes.HINSTANCE),('hIcon',wintypes.HICON),('hCursor',wintypes.HANDLE),('hbrBackground',wintypes.HBRUSH),('lpszMenuName',wintypes.LPCWSTR),('lpszClassName',wintypes.LPCWSTR)]
    user.DefWindowProcW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM];user.DefWindowProcW.restype=ctypes.c_ssize_t
    user.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)]
    user.FillRect.argtypes=[wintypes.HDC,ctypes.POINTER(wintypes.RECT),wintypes.HBRUSH]
    test_brush=None
    @WNDPROC
    def background_proc(window,message,wp,lp):
        if message==0x14 and test_brush:
            area=wintypes.RECT();user.GetClientRect(window,ctypes.byref(area))
            user.FillRect(wp,ctypes.byref(area),test_brush);return 1
        return user.DefWindowProcW(window,message,wp,lp)
    klass=WNDCLASS();klass.lpfnWndProc=background_proc;klass.lpszClassName='SouluTestBackdrop'
    user.RegisterClassW.argtypes=[ctypes.POINTER(WNDCLASS)]
    assert user.RegisterClassW(ctypes.byref(klass))
    background=user.CreateWindowExW(0x08000080,'SouluTestBackdrop','Soulu blur test background',0x90000000,after.left-20,after.top-20,after.right-after.left+40,after.bottom-after.top+40,None,None,None,None)
    assert background
    user.SetWindowPos(background,hwnd,after.left-20,after.top-20,after.right-after.left+40,after.bottom-after.top+40,0x0010|0x0040)
    user.SetForegroundWindow(hwnd)
    brushes=[];bitmap=None;old_brush=None
    try:
        def behind(brush,name):
            global old_brush,test_brush
            test_brush=brush
            previous=user.SetClassLongPtrW(background,-10,brush)
            if old_brush is None: old_brush=previous
            user.RedrawWindow(background,None,None,0x0105)
            time.sleep(.7)
            reference=ImageGrab.grab(bbox=(after.left-15,after.top+20,after.left-10,after.top+30))
            print('Actual background reference',name,ImageStat.Stat(reference).mean)
            if name=='matte-red': assert ImageStat.Stat(reference).mean[0]>180, 'Test background did not paint red'
            if name=='matte-blue': assert ImageStat.Stat(reference).mean[2]>180, 'Test background did not paint blue'
            shot=ImageGrab.grab(bbox=(after.left,after.top,after.right,after.bottom))
            shot.save('browser/artifacts/'+name+'.png')
            print('SOULU_SCREENSHOT:'+name+'='+base64.b64encode(open('browser/artifacts/'+name+'.png','rb').read()).decode())
            return shot.crop((100,12,145,38))
        for color,name in [(0x3030E0,'matte-red'),(0xE03030,'matte-blue')]:
            brush=gdi.CreateSolidBrush(color);brushes.append(brush)
            sample=behind(brush,name)
            if name=='matte-red': red=ImageStat.Stat(sample).mean
            else: blue=ImageStat.Stat(sample).mean
        change=max(abs(a-b) for a,b in zip(red,blue))
        print('Backdrop color response:',red,blue,'delta',change)
        assert change>25, 'Toolbar is opaque: changing the real background has no visible effect'
        pixels=bytes(v for y in range(8) for x in range(8) for v in ((48,48,224,255) if x<4 else (224,48,48,255)))
        data=ctypes.create_string_buffer(pixels)
        bitmap=gdi.CreateBitmap(8,8,1,32,data)
        brush=gdi.CreatePatternBrush(bitmap);brushes.append(brush)
        stripes=behind(brush,'matte-blur')
        deviation=max(ImageStat.Stat(stripes).stddev)
        print('Fine-stripe variation after blur:',deviation)
        assert deviation<30, 'Backdrop is transparent but is not blurred'
        print('Real backdrop transparency and blur pixel checks passed.')
    finally:
        if old_brush is not None:user.SetClassLongPtrW(background,-10,old_brush)
        user.DestroyWindow(background)
        for brush in brushes:gdi.DeleteObject(brush)
        if bitmap:gdi.DeleteObject(bitmap)
    frame=ImageGrab.grab(bbox=(after.left,after.top,after.right,after.bottom))
    edge=frame.getpixel((3,200));inside=frame.getpixel((12,200))
    print('Content reaches window edge:',edge,inside)
    assert max(abs(a-b) for a,b in zip(edge,inside))<6, 'Visible border remains around browser content'
    print("CEF settings, alpha rendering, address editing, completions, black blank tab and mouse resize passed.")
finally:
    if settings: settings.close()
    shell.close()

