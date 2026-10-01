"""Exercise the real CEF bridge, isolated extraction, trusted view and persistence."""
import ctypes
import http.server
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

spec = importlib.util.spec_from_file_location('storage', Path(__file__).with_name('test-cef-storage.py'))
s = importlib.util.module_from_spec(spec); spec.loader.exec_module(s)
checks = []

def wait(fn, timeout=25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = fn()
        if value: return value
        time.sleep(.1)
    raise AssertionError('Site/reader state timeout')

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/image.svg':
            body = b'<svg xmlns="http://www.w3.org/2000/svg" width="120" height="60"><rect width="120" height="60" fill="blue"/></svg>'
            kind = 'image/svg+xml'
        elif self.path == '/empty':
            body = b'<!doctype html><title>Search</title><nav>Home</nav><form><input></form>'; kind = 'text/html'
        else:
            paragraphs = ''.join('<p>Reading fixture paragraph %d. %s</p>' % (i, ('This is an original report about browsers, privacy, and careful reading. ' * 8)) for i in range(8))
            wrapper = 'article' if self.path == '/article' else 'div class="post-content"'
            closing = 'article' if self.path == '/article' else 'div'
            metadata = '<meta name="author" content="Fixture Author"><meta property="article:published_time" content="2026-01-02">' if self.path == '/article' else ''
            body = (f'<!doctype html><meta charset="utf-8"><title>Fixture article</title>{metadata}<nav>Navigation junk</nav>'
                    f'<{wrapper}><h1>Fixture article</h1>{paragraphs}<h2>Section heading</h2><ul><li>List item</li></ul>'
                    '<blockquote>Quoted text</blockquote><figure><img src="/image.svg"><figcaption>Image caption</figcaption></figure>'
                    '<p><a href="/linked">Article link</a></p><script>window.sourceScript=true</script>'
                    '<p onclick="window.readerXSS=true">Inline-handler text</p><iframe src="/empty"></iframe>'
                    f'<form>Form junk<input></form></{closing}><aside>Recommendation junk</aside>').encode()
            kind = 'text/html'
        self.send_response(200); self.send_header('Content-Type', kind); self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store'); self.end_headers(); self.wfile.write(body)
    def log_message(self, *args): pass

with tempfile.TemporaryDirectory(prefix='soulu-reader-', ignore_cleanup_errors=True) as root:
    os.environ['LOCALAPPDATA'] = root
    env = dict(os.environ, SOULU_UI_TEST_PORT=str(s.DEBUG_PORT))
    server = http.server.ThreadingHTTPServer(('127.0.0.1', s.free_port()), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f'http://127.0.0.1:{server.server_port}'
    other = f'http://localhost:{server.server_port}'
    process = shell = page = None
    def start():
        p = subprocess.Popen([sys.argv[1], '--no-proxy-server'], env=env)
        t = wait(lambda: next((t for t in s.targets() if '/ui/index.html' in t.get('url', '')), None))
        ws = s.websocket.create_connection(t['webSocketDebuggerUrl'], timeout=30, origin=s.BASE)
        wait(lambda: s.evaluate(ws, "typeof browserShell.siteAction==='function'"))
        return p, ws, s.page_socket()
    def current(): return s.evaluate(shell, 'browserShell.getCurrentSite()')
    def action(name, values=None, snapshot=None):
        snapshot = snapshot or current()
        args = {k:snapshot[k] for k in ('tabId','url','generation')}; args.update(values or {})
        return s.evaluate(shell, 'browserShell.siteAction('+json.dumps(name)+','+json.dumps(args)+')')
    def assert_check(value, name):
        assert value, name
        checks.append(name)
    def seed(ws):
        return s.evaluate(ws, "(async()=>{localStorage.setItem('keep','yes');document.cookie='keep=yes; path=/';await new Promise((resolve,reject)=>{const r=indexedDB.open('site-reader',1);r.onupgradeneeded=()=>r.result.createObjectStore('rows');r.onsuccess=()=>{r.result.close();resolve()};r.onerror=reject});await caches.open('reader-cache');return true})()")
    try:
        process, shell, page = start()
        for path in ('/article','/post'):
            s.navigate(page, origin+path)
            wait(lambda: current().get('url') == origin+path and not s.evaluate(shell,'browserShell.getState().then(s=>s.page.loading)'))
            snapshot = action('reader.probe')
            assert_check(snapshot['readerAvailable'], 'extract '+path)
            result = action('reader.enter')
            data = result['article']
            assert_check(data['title']=='Fixture article' and 'Reading fixture' in data['content'], 'title/body '+path)
            if path == '/article': assert_check(data['author']=='Fixture Author' and data['date']=='2026-01-02', 'metadata from source')
            else: assert_check(not data['author'] and not data['date'], 'no invented metadata')
            wait(lambda: s.evaluate(shell, "!document.querySelector('.reader-view').hidden"))
            assert_check(s.evaluate(shell, "!!document.querySelector('.reader-body img')&&!!document.querySelector('.reader-body a[href]')&&!!document.querySelector('.reader-body li')&&!!document.querySelector('.reader-body blockquote')"), 'article structure '+path)
            assert_check(s.evaluate(shell, "!document.querySelector('.reader-body script,.reader-body iframe,.reader-body form,.reader-body [onclick]')&&!window.readerXSS"), 'source executable content absent '+path)
            action('reader.exit')
            assert_check(s.evaluate(page, 'location.href') == origin+path, 'exit preserves source '+path)
        # Test sanitizer independently with malicious content Readability may discard.
        malicious = '<script>window.readerXSS=1</script><img src="javascript:alert(1)" onerror="window.readerXSS=2"><svg onload="window.readerXSS=3"></svg><a href="javascript:alert(1)">bad</a><iframe></iframe><form></form><p style="position:fixed" id="evil">safe</p>'
        clean = s.evaluate(shell, "(()=>{const n=document.createElement('div');n.append(souluReaderSafe.content("+json.dumps(malicious)+","+json.dumps(origin)+"));return n.innerHTML})()")
        assert_check(all(word not in clean for word in ('script','iframe','form','onclick','onerror','onload','javascript:','style=','id=')), 'allowlist sanitizer malicious payload')
        assert_check(s.evaluate(shell, '!window.readerXSS'), 'sanitizer executes no script')
        for command in ('in','out','reset'): action('zoom', {'command':command})
        assert_check(current()['zoom']==100, 'native zoom reset')
        action('permission', {'permission':'camera','value':2})
        action('blocking', {'value':1})
        assert_check(current()['rules']['sites']['127.0.0.1']['camera']==2 and current()['rules']['blocking']['sites']['127.0.0.1'], 'site permission/adblock override')
        action('reset')
        assert_check('127.0.0.1' not in current()['rules']['sites'] and '127.0.0.1' not in current()['rules']['blocking']['sites'], 'reset both models')
        action('permission', {'permission':'camera','value':2})
        action('reader.enter')
        for theme in ('light','sepia','dark'):
            action('reader.preferences', {'preferences':{'theme':theme}})
            assert_check(s.evaluate(shell,"document.querySelector('.reader-view').dataset.theme")==theme, 'theme '+theme)
        for font in ('sans','serif','system'): action('reader.preferences', {'preferences':{'font':font}})
        for value in (0,1,2): action('reader.preferences', {'preferences':{'width':value,'spacing':value}})
        action('reader.preferences', {'preferences':{'size':24,'images':False}})
        assert_check(s.evaluate(shell,"getComputedStyle(document.querySelector('.reader-body img')).display==='none'&&getComputedStyle(document.querySelector('.reader-article')).fontSize==='24px'"), 'text size and image toggle')
        # Both layout modes render inside the same native window.
        for layout in ('compact','classic'):
            s.evaluate(shell,'browserShell.setSettings('+json.dumps({'layout':layout})+')')
            wait(lambda: s.evaluate(shell,'document.body.dataset.layout')==layout)
            assert_check(s.evaluate(shell,"document.querySelector('.reader-view').getBoundingClientRect().top") == (82 if layout=='classic' else 48), 'reader '+layout)
        # CEF keyboard handler opens the shared find UI, rather than a prompt.
        s.command(page,'Input.dispatchKeyEvent',{'type':'rawKeyDown','windowsVirtualKeyCode':70,'modifiers':2,'key':'f','code':'KeyF'})
        wait(lambda:s.evaluate(shell,"!document.querySelector('.site-find').hidden"))
        assert_check(True, 'native Ctrl+F shared find')
        s.evaluate(shell,"document.querySelector('.site-find input').value='Reading';document.querySelector('.site-find input').dispatchEvent(new Event('input'))")
        s.evaluate(shell,"document.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}))")
        assert_check(s.evaluate(shell,"document.querySelector('.site-find').hidden"), 'find escape')
        # Open a background link without touching the original source tab.
        original = current()
        action('reader.link', {'target':origin+'/linked','mode':'background'})
        assert_check(current()['tabId']==original['tabId'] and s.evaluate(shell,'browserShell.getState().then(s=>s.tabs.length)')==2, 'reader background tab internal')
        action('reader.exit')
        s.evaluate(shell,'browserShell.pageMenu()'); wait(lambda:s.evaluate(shell,"!document.querySelector('.site-popover').hidden"))
        s.evaluate(shell,"document.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}))")
        assert_check(s.evaluate(shell,"document.querySelector('.site-popover').hidden"), 'popover escape')
        s.evaluate(shell,'browserShell.pageMenu()'); wait(lambda:s.evaluate(shell,"!document.querySelector('.site-popover').hidden"))
        s.evaluate(shell,"document.querySelector('.site-shield').dispatchEvent(new PointerEvent('pointerdown',{bubbles:true}))")
        assert_check(s.evaluate(shell,"document.querySelector('.site-popover').hidden"), 'popover outside click')
        stale = current(); s.navigate(page, origin+'/empty')
        assert_check(not action('reader.probe')['readerAvailable'], 'non-article refusal')
        assert_check(s.evaluate(shell,'browserShell.siteAction("zoom",'+json.dumps({**{k:stale[k] for k in ('tabId','url','generation')},'command':'in'})+').then(()=>false,()=>true)'), 'stale document rejected')
        # Native confirmation is exercised on disposable data only.
        seed(page)
        s.evaluate(shell,'browserShell.openTab('+json.dumps(other+'/empty')+',false)')
        other_target=wait(lambda:next((t for t in s.targets() if t.get('url')==other+'/empty'),None))
        other_ws=s.websocket.create_connection(other_target['webSocketDebuggerUrl'],timeout=30,origin=s.BASE);seed(other_ws)
        s.evaluate(shell,'browserShell.switchTab('+str(original['tabId'])+')')
        wait(lambda:current()['tabId']==original['tabId'])
        def confirm():
            user=ctypes.windll.user32;user.FindWindowW.restype=ctypes.c_void_p;user.GetDlgItem.restype=ctypes.c_void_p
            hwnd=wait(lambda:user.FindWindowW('#32770','Данные сайта'))
            user.PostMessageW(ctypes.c_void_p(hwnd),0x111,6,0)  # WM_COMMAND / IDYES
        worker=threading.Thread(target=confirm);worker.start()
        cleared=action('clear');worker.join(timeout=10)
        assert_check(cleared['cleared'], 'native confirmed storage cleanup')
        assert_check(s.evaluate(page,"localStorage.getItem('keep')===null&&document.cookie.includes('keep=yes')"), 'clear subset and retained cookies')
        assert_check(s.evaluate(page,"indexedDB.databases().then(ds=>!ds.some(d=>d.name==='site-reader'))"), 'clear indexeddb')
        assert_check(s.evaluate(page,"caches.keys().then(keys=>!keys.includes('reader-cache'))"), 'clear cache storage')
        assert_check(s.evaluate(other_ws,"localStorage.getItem('keep')==='yes'"), 'other origin retained');other_ws.close()
        page.close();shell.close();s.close_normally(process);process=None
        persisted=json.loads((Path(root)/'Soulu/User Data/Profiles/personal/soulu-reader.json').read_text())
        assert_check(persisted['size']==24 and persisted['theme']=='dark', 'preferences on disk')
        process,shell,page=start();s.navigate(page,origin+'/article')
        assert_check(current()['preferences']['size']==24 and current()['rules']['sites']['127.0.0.1']['camera']==2, 'preferences and permissions survive restart')
        s.evaluate(shell,"browserShell.newIncognito()")
        wait(lambda:s.evaluate(shell,'browserShell.getState().then(s=>s.incognito)'))
        private=current();action('reader.preferences',{'preferences':{'size':30}},private)
        assert_check(current()['preferences']['size']==30, 'private reader preferences in memory')
        assert_check(json.loads((Path(root)/'Soulu/User Data/Profiles/personal/soulu-reader.json').read_text())['size']==24, 'private preferences do not overwrite regular')
        s.evaluate(shell,'browserShell.closeTab('+str(private['tabId'])+')');wait(lambda:not s.evaluate(shell,'browserShell.getState().then(s=>s.incognito)'))
        s.evaluate(shell,'browserShell.newIncognito()');wait(lambda:s.evaluate(shell,'browserShell.getState().then(s=>s.incognito)'))
        assert_check(current()['preferences']['size']==20, 'private preferences discarded with last private tab')
        print(json.dumps({'passed':checks},ensure_ascii=False))
    finally:
        for ws in (page,shell):
            if ws:
                try: ws.close()
                except Exception: pass
        if process and process.poll() is None:
            try: s.close_normally(process)
            except Exception: process.kill()
        server.shutdown()
