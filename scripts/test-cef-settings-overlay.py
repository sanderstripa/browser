"""Real Win32/CEF overlay checks, including composed (not CDP) backdrop images."""
import ctypes as c
from ctypes import wintypes as w
import http.server
import importlib.util
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import threading
import time

from PIL import ImageGrab, ImageStat, ImageChops

spec = importlib.util.spec_from_file_location('storage', Path(__file__).with_name('test-cef-storage.py'))
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)
u = c.windll.user32
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.MoveWindow.argtypes = [w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.BOOL]
u.ShowWindow.argtypes = [w.HWND, c.c_int]
u.IsChild.argtypes = [w.HWND, w.HWND]
u.IsWindowEnabled.argtypes = [w.HWND]
u.GetWindow.argtypes = [w.HWND, w.UINT]
u.GetWindow.restype = w.HWND
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.SetForegroundWindow.argtypes = [w.HWND]
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]

class GUI(c.Structure):
    _fields_ = [('cbSize', w.DWORD), ('flags', w.DWORD),
               ('active', w.HWND), ('focus', w.HWND), ('capture', w.HWND),
               ('menu', w.HWND), ('move', w.HWND), ('caret', w.HWND), ('rect', w.RECT)]

def wait(fn, timeout=35):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        result = fn()
        if result:
            return result
        time.sleep(.04)
    raise AssertionError('Overlay condition timed out')

def windows(pid, cls):
    found = []
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    @callback
    def visit(hwnd, _):
        process = w.DWORD()
        u.GetWindowThreadProcessId(hwnd, c.byref(process))
        name = c.create_unicode_buffer(128)
        u.GetClassNameW(hwnd, name, 128)
        if process.value == pid and name.value == cls:
            found.append(hwnd)
        return True
    u.EnumWindows(visit, 0)
    return found

def rect(hwnd):
    r = w.RECT()
    assert u.GetWindowRect(hwnd, c.byref(r))
    return (r.left, r.top, r.right, r.bottom)

def client_rect(hwnd):
    r=w.RECT();p=w.POINT()
    u.GetClientRect(hwnd,c.byref(r));u.ClientToScreen(hwnd,c.byref(p))
    return (p.x,p.y,p.x+r.right,p.y+r.bottom)

def focus(hwnd):
    g = GUI(); g.cbSize = c.sizeof(g)
    tid = u.GetWindowThreadProcessId(hwnd, None)
    assert u.GetGUIThreadInfo(tid, c.byref(g))
    return g.focus

class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b'''<!doctype html><title>Overlay live browser fixture</title>
<style>body{margin:0;min-height:4000px;background:repeating-linear-gradient(0deg,transparent 0px,transparent 120px,#0671ff88 120px,#0671ff88 240px),repeating-linear-gradient(90deg,#101020 0px,#101020 4px,#fafaff 4px,#fafaff 8px)}article{margin:30px 160px;background:#fff;padding:40px}p{font:20px sans-serif}</style>
<article><h1>Live article</h1>''' + b'<p>A real article paragraph with enough readable content to test Reader and scrolling. The background remains alive throughout Settings transitions.</p>' * 35 + b'''</article><canvas id=c width=64 height=64 hidden></canvas><video id=v autoplay muted></video>
<script>window.loads=1;window.clicks=0;window.keys=0;window.ticks=0;document.addEventListener('click',()=>clicks++);document.addEventListener('keydown',()=>keys++);sessionStorage.marker='preserved';setInterval(()=>{ticks++;c.getContext('2d').fillRect(0,0,64,64)},40);v.srcObject=c.captureStream(25);v.play();</script>'''
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers(); self.wfile.write(body)
    def log_message(self, *_):
        pass

def main():
    os.environ['SOULU_REGRESSION_SKIP_FIRST_RUN']='1'
    exe = str(Path(sys.argv[1]).resolve())
    output = Path(sys.argv[2]); visuals = output.parent / 'settings-overlay-visuals'
    visuals.mkdir(parents=True, exist_ok=True)
    checks, timings, metrics = [], [], []
    def check(value, label):
        assert value, label
        checks.append(label); print('PASS:', label, flush=True)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = 'http://127.0.0.1:' + str(server.server_port)
    with tempfile.TemporaryDirectory(prefix='soulu-overlay-', ignore_cleanup_errors=True) as folder:
        env = os.environ.copy(); env['LOCALAPPDATA'] = folder
        env['APPDATA'] = str(Path(folder) / 'roaming')
        env['SOULU_REGRESSION_SKIP_FIRST_RUN'] = '1'
        env['SOULU_UI_TEST_PORT'] = str(s.DEBUG_PORT)
        data = Path(folder) / 'Soulu' / 'User Data'; data.mkdir(parents=True)
        (data / 'settings.json').write_text(json.dumps({'settings': {'startupMode':'blank',
            'newTabMode':'blank','theme':'light','mattePanel':False,'matteDefaultV15':True}}))
        process = subprocess.Popen([exe, '--no-proxy-server'], env=env)
        sockets = []
        def socket(fragment):
            def find():
                for t in s.targets():
                    if fragment in t.get('url', ''):
                        import websocket
                        ws = websocket.create_connection(t['webSocketDebuggerUrl'], timeout=15, origin=s.BASE)
                        sockets.append(ws); return ws
            return wait(find)
        shell = None; settings = None
        def call(method, value=None):
            return s.evaluate(shell, 'browserShell.' + method + '(' + ('' if value is None else json.dumps(value)) + ')')
        def state(): return call('getState')
        def edit(expr): return s.evaluate(settings, expr)
        def open_settings():
            nonlocal settings
            before = state(); start = time.monotonic(); call('openSettingsWindow')
            settings = socket('/ui/settings.html')
            wait(lambda:edit("document.body?.classList.contains('ready')"))
            wait(lambda:state()['settingsOverlayReady'])
            timings.append((time.monotonic()-start)*1000)
            after = state()
            check(after['activeTabId'] == before['activeTabId'] and after['tabs'] == before['tabs'],
                  'Settings leaves tab strip, selected tab and URLs unchanged')
            return wait(lambda:windows(process.pid, 'SouluSettingsOverlay'))[0]
        def close_settings():
            edit('souluSettingsRequestClose()')
            wait(lambda:not state()['settingsOverlayOpen'])
            settings.close()
        try:
            shell = socket('/ui/index.html')
            wait(lambda:s.evaluate(shell,"typeof window.browserShell?.getState==='function'"))
            main_window = wait(lambda:windows(process.pid,'SouluBrowserWindow'))[0]
            wait(lambda:s.targets() and u.IsWindowVisible(main_window))
            u.MoveWindow(main_window, 50, 50, 1280, 900, True)
            u.ShowWindow(main_window, 9); u.SetForegroundWindow(main_window)
            call('navigate', origin+'/fixture'); content=socket(origin+'/fixture')
            wait(lambda:s.evaluate(content,"document.readyState==='complete' && !!window.v?.srcObject"))
            s.evaluate(content,'scrollTo(0,320)'); time.sleep(.5)
            page_before = s.evaluate(content,"({url:location.href,scroll:scrollY,marker:sessionStorage.marker,loads,media:v.currentTime,paused:v.paused})")
            nav_before = s.command(content,'Page.getNavigationHistory')
            baseline = ImageGrab.grab(bbox=rect(main_window)); baseline.save(visuals/'background-sharp.png')
            previous_focus=focus(main_window)
            overlay = open_settings()
            panel=u.GetWindow(overlay,5)
            pr,br=rect(panel),rect(overlay)
            check(pr[1]==br[1] and abs((pr[0]+pr[2])-(br[0]+br[2]))<=1,'Native panel is top anchored and horizontally centered')
            check(u.GetWindow(overlay,4)==main_window, 'Native host is owned by the current Soulu window')
            check(bool(u.IsChild(overlay,focus(main_window))), 'Real Windows keyboard focus is inside Settings')
            check(state()['settingsOverlayDuration']==260, 'Normal transition duration is 260 ms')
            initial = edit('JSON.stringify(souluSettings.staged)')
            call('openSettingsWindow')
            check(len(windows(process.pid,'SouluSettingsOverlay'))==1 and edit('JSON.stringify(souluSettings.staged)')==initial,
                  'Repeated opening focuses one existing overlay without resetting staged state')
            time.sleep(.35)
            blurred=ImageGrab.grab(bbox=rect(main_window)); blurred.save(visuals/'settings-composed-blur.png')
            # The native Settings panel is capped at 1020 DIP. Inspect the live
            # stripes in the uncovered margin; screenshots of the Settings DOM
            # cannot prove that a separate native CEF browser is blurred.
            if blurred.width >= 1200:
                box=(24,150,90,min(650,blurred.height-50))
                crisp=baseline.crop(box).convert('L'); soft=blurred.crop(box).convert('L')
                def energy(im):
                    return ImageStat.Stat(ImageChops.difference(im.crop((1,0,im.width,im.height)),im.crop((0,0,im.width-1,im.height)))).mean[0]
                a,b=energy(crisp),energy(soft); metrics.append({'sharpEdgeEnergy':a,'blurredEdgeEnergy':b})
                check(a>12 and b<a*.65, 'Composed backdrop actually blurs the native webpage')
                check(ImageStat.Stat(soft).stddev[0]>1, 'Blurred background remains visible rather than an opaque fill')
            # A physical backdrop click must neither dismiss nor hit the page.
            r=rect(overlay); before_click=s.evaluate(content,'clicks')
            u.SetCursorPos(r[0]+30,r[1]+220); u.mouse_event(2,0,0,0,0);u.mouse_event(4,0,0,0,0)
            time.sleep(.2)
            check(state()['settingsOverlayOpen'] and s.evaluate(content,'clicks')==before_click,
                  'Physical backdrop click blocks browser interaction and does not close Settings')
            # CEF keyboard shortcut sent to the background must be consumed.
            count=len(state()['tabs']); s.evaluate(shell,"cefQuery({request:JSON.stringify({action:'browser.test.pageShortcut',payload:84}),onSuccess:()=>{}})")
            time.sleep(.2);check(len(state()['tabs'])==count,'Background Ctrl+T cannot change tabs while Settings is open')
            for theme in ['light','dark','system']:
                edit(f'''souluSettings.openSection("interface");document.querySelector('[name="settings-theme"][value="{theme}"]').click()''')
                for matte in [False,True]:
                    edit('(()=>{const n=document.querySelector("#mattePanel");if(n.checked!=='+str(matte).lower()+')n.click();})()')
                    u.MoveWindow(main_window,50,50,1280,900,True);time.sleep(.15)
                    check(rect(overlay)==client_rect(main_window),'Backdrop covers the current window in '+theme+' matte='+str(matte))
                    ImageGrab.grab(bbox=rect(main_window)).save(visuals/(theme+'-matte-'+str(matte)+'.png'))
            u.ShowWindow(main_window,3);time.sleep(.25)
            check(rect(overlay)==client_rect(main_window),'Maximize keeps overlay bound to the owner')
            u.ShowWindow(main_window,9);time.sleep(.2)
            for width,height in [(640,480),(800,600),(1280,900)]:
                u.MoveWindow(main_window,50,50,width,height,True);time.sleep(.15)
                check(rect(overlay)==client_rect(main_window),'Resize preserves backdrop bounds '+str((width,height)))
            for scale in [1,1.25,1.5,1.75,2]:
                s.command(settings,'Emulation.setDeviceMetricsOverride',{'width':760,'height':560,'deviceScaleFactor':scale,'mobile':False})
                check(edit("document.documentElement.scrollWidth<=innerWidth && document.querySelector('.settings-footer').getBoundingClientRect().bottom<=innerHeight+1"),
                      'Settings controls and footer fit at device scale '+str(scale))
            s.command(settings,'Emulation.clearDeviceMetricsOverride')
            edit('souluSettings.cancel()')
            edit('souluSettingsRequestClose()')
            time.sleep(.06)
            transition=state()
            check(transition['settingsOverlayOpen'] and 0<transition['settingsOverlayProgress']<1,'Close transition retains the overlay and blocker while blur fades')
            wait(lambda:not state()['settingsOverlayOpen']);settings.close()
            check(focus(main_window)==previous_focus or bool(u.IsChild(main_window,focus(main_window))),'Closing restores a valid previous browser focus target')
            after=s.evaluate(content,"({url:location.href,scroll:scrollY,marker:sessionStorage.marker,loads,media:v.currentTime,paused:v.paused})")
            check({k:after[k] for k in ['url','scroll','marker','loads']}=={k:page_before[k] for k in ['url','scroll','marker','loads']},
                  'Closing preserves live URL, scroll, DOM and session state')
            check(after['media']>page_before['media'] and not after['paused'],'Live video playback continues through Settings')
            check(s.command(content,'Page.getNavigationHistory')==nav_before,'Navigation history and back/forward stack are unchanged')
            check(not windows(process.pid,'SouluSettingsOverlay'),'Native host is released after close')
            expected=len(s.targets())
            class FT(c.Structure):
                _fields_=[('low',w.DWORD),('high',w.DWORD)]
            def cpu_ms():
                times=[FT() for _ in range(4)]
                c.windll.kernel32.GetProcessTimes(w.HANDLE(int(process._handle)),*[c.byref(t) for t in times])
                return sum((t.high<<32)+t.low for t in times[2:])/10000
            cpu_start,wall_start=cpu_ms(),time.monotonic()
            for i in range(30):
                open_settings();close_settings()
                check(wait(lambda:len(s.targets())==expected),'Repeated cycle '+str(i+1)+' releases its CEF browser')
            metrics.append({'hostCpuMsFor30Cycles':cpu_ms()-cpu_start,'wallMsFor30Cycles':(time.monotonic()-wall_start)*1000})
            for background in ['soulu://home','soulu://history','about:blank']:
                call('navigate',background);time.sleep(.4)
                open_settings();close_settings()
            # Reader uses the layered shell over the existing webpage; opening
            # Settings must leave that reader state intact as well.
            call('navigate',origin+'/reader');time.sleep(.5)
            snapshot=call('getCurrentSite')
            s.evaluate(shell,'browserShell.siteAction("reader.enter",'+json.dumps({'tabId':snapshot['tabId'],'url':snapshot['url'],'generation':snapshot['generation']})+')')
            wait(lambda:state()['site']['readerActive'] if 'site' in state() else call('getCurrentSite')['readerActive'])
            open_settings();close_settings()
            check(call('getCurrentSite')['readerActive'],'Reader stays active under Settings and after close')
            output.write_text(json.dumps({'passed':True,'checks':checks,'openMs':{'median':statistics.median(timings),'max':max(timings)},
                'blurMetrics':metrics,'visuals':str(visuals),'limitations':['Device scale probes are not physical OS DPI switches',
                'Physical multi-monitor migration requires an available second monitor']},ensure_ascii=False,indent=2),encoding='utf-8')
        finally:
            for ws in sockets:
                try:ws.close()
                except Exception:pass
            if process.poll() is None:
                try:s.close_normally(process)
                except Exception:process.terminate();process.wait(timeout=20)
            server.shutdown()

if __name__=='__main__':
    main()
