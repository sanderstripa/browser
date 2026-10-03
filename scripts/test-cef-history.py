"""History and clearing against real CEF, real SQLite and isolated test profiles."""
import base64
import ctypes
import http.server
import importlib.util
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time

spec=importlib.util.spec_from_file_location('storage',Path(__file__).with_name('test-cef-storage.py'))
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
os.environ['NO_PROXY']='localhost,127.0.0.1,::1'
checks=[]
def check(value,name):
    assert value,name
    checks.append(name);print('PASS:',name,flush=True)
def wait(fn,timeout=30):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        value=fn()
        if value:return value
        time.sleep(.1)
    raise AssertionError('History state timeout')
class Site(http.server.BaseHTTPRequestHandler):
    cache_requests=0
    def do_GET(self):
        cache=self.path.startswith('/cached')
        if cache:Site.cache_requests+=1
        body=(b'cache payload' if cache else '<!doctype html><meta charset="utf-8"><title>История Soulu fixture</title><h1>History fixture</h1>'.encode())
        self.send_response(200);self.send_header('Content-Type','text/plain' if cache else 'text/html')
        self.send_header('Cache-Control','max-age=86400' if cache else 'no-store')
        if self.path.startswith('/seed'):self.send_header('Set-Cookie','soulu_history_cookie=kept; Path=/; HttpOnly; SameSite=Lax; Max-Age=86400')
        self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def log_message(self,*args):pass
def main():
    exe=str(Path(sys.argv[1]).resolve());output=Path(sys.argv[2]) if len(sys.argv)>2 else None
    server=http.server.ThreadingHTTPServer(('127.0.0.1',s.free_port()),Site)
    threading.Thread(target=server.serve_forever,daemon=True).start();origin=f'http://127.0.0.1:{server.server_port}'
    with tempfile.TemporaryDirectory(prefix='soulu-history-',ignore_cleanup_errors=True) as root:
        env=dict(os.environ,LOCALAPPDATA=root,SOULU_UI_TEST_PORT=str(s.DEBUG_PORT),SOULU_REGRESSION_SKIP_FIRST_RUN='1')
        os.environ['SOULU_REGRESSION_SKIP_FIRST_RUN']='1'
        process=None;shell=None;sockets=[];passed=False
        def socket(target):
            ws=s.websocket.create_connection(target['webSocketDebuggerUrl'],timeout=90,origin=s.BASE);sockets.append(ws);return ws
        def page(fragment,private=False):
            def find():
                for target in s.targets():
                    if fragment not in target.get('url',''):continue
                    ws=socket(target)
                    if not s.evaluate(ws,"typeof window.cefQuery==='function'"):continue
                    if fragment=='/ui/history.html' and bool(bridge(ws,'state')['incognito'])!=private:continue
                    if s.evaluate(ws,"document.readyState==='complete'"):return ws
                return None
            return wait(find)
        def bridge(ws,action,payload=None):
            return s.evaluate(ws,"new Promise((resolve,reject)=>cefQuery({request:"+json.dumps(json.dumps({'action':'history.'+action,'payload':payload or {}}))+",onSuccess:r=>resolve(r?JSON.parse(r):null),onFailure:(_,m)=>reject(Error(m))}))")
        def rejected(ws,action,payload=None):
            return s.evaluate(ws,"new Promise(resolve=>cefQuery({request:"+json.dumps(json.dumps({'action':'history.'+action,'payload':payload or {}}))+",onSuccess:()=>resolve(false),onFailure:()=>resolve(true)}))")
        def call(method,payload=None):return s.evaluate(shell,'browserShell.'+method+'('+('' if payload is None else json.dumps(payload))+')')
        def new_content(method,payload=None):
            old={t['id'] for t in s.targets()};result=call(method,payload)
            target=wait(lambda:next((t for t in s.targets() if t['id'] not in old and t.get('type')=='page'),None))
            ws=socket(target);wait(lambda:s.evaluate(ws,"document.readyState==='complete'"));return ws,result
        def state():return call('getState')
        def query(ws,search='',begin=0,end=864e13,offset=0):return bridge(ws,'query',dict(search=search,begin=begin,end=end,offset=offset))
        def clear(ws,history=False,sites=False,cache=False,range='all'):
            return bridge(ws,'clear',dict(history=history,sites=sites,cache=cache,range=range,webAllTimeAcknowledged=True))
        def start():
            nonlocal process,shell
            process=subprocess.Popen([exe,'--no-proxy-server'],env=env)
            shell=page('/ui/index.html');wait(lambda:s.evaluate(shell,"typeof window.browserShell?.openHistory==='function'"))
        def stop():
            nonlocal process
            for ws in sockets:
                try:ws.close()
                except Exception:pass
            sockets.clear();s.close_normally(process);process=None
        profile=Path(root)/'Soulu'/'User Data'/'Profiles'/'personal'
        try:
            start();content=s.page_socket();sockets.append(content);s.navigate(content,origin+'/seed')
            s.evaluate(content,"localStorage.setItem('history-test','keep');sessionStorage.setItem('history-session','keep')")
            s.evaluate(content,"new Promise((resolve,reject)=>{const r=indexedDB.open('history-test',1);r.onupgradeneeded=()=>r.result.createObjectStore('values');r.onerror=()=>reject(r.error);r.onsuccess=()=>{const db=r.result,tx=db.transaction('values','readwrite');tx.objectStore('values').put('keep','key');tx.oncomplete=()=>{db.close();resolve(true)}}})")
            s.evaluate(content,"caches.open('history-cache').then(c=>c.put('/cached-storage',new Response('keep')))")
            check(rejected(content,'query',dict(search='',begin=0,end=864e13,offset=0)),'An ordinary website cannot call the History bridge')
            call('addBookmark');call('addPassword',dict(origin=origin,username='tester',password='history-vault-control'))
            s.command(content,'Input.dispatchKeyEvent',dict(type='rawKeyDown',windowsVirtualKeyCode=72,nativeVirtualKeyCode=72,modifiers=2))
            s.command(content,'Input.dispatchKeyEvent',dict(type='keyUp',windowsVirtualKeyCode=72,nativeVirtualKeyCode=72,modifiers=2))
            history=page('/ui/history.html')
            check(state()['tabs'][-1]['url']=='soulu://history','Ctrl+H opens the internal History page')
            s.command(history,'Input.dispatchKeyEvent',dict(type='rawKeyDown',windowsVirtualKeyCode=46,nativeVirtualKeyCode=46,modifiers=10))
            check(wait(lambda:s.evaluate(history,"document.getElementById('clearDialog').open")),'Ctrl+Shift+Delete opens the clearing modal')
            s.command(history,'Input.dispatchKeyEvent',dict(type='rawKeyDown',windowsVirtualKeyCode=27,nativeVirtualKeyCode=27,key='Escape',code='Escape'))
            s.command(history,'Input.dispatchKeyEvent',dict(type='keyUp',windowsVirtualKeyCode=27,nativeVirtualKeyCode=27,key='Escape',code='Escape'))
            check(wait(lambda:s.evaluate(history,"!document.getElementById('clearDialog').open")),'Esc cancels the idle modal')
            visits=wait(lambda:query(history)['rows'])
            check(any(r['url']==origin+'/seed' for r in visits),'Successful main-frame visits persist in the canonical history')
            check(query(history,'ИСТОРИЯ')['rows'],'Unicode case-insensitive title search')
            check(query(history,'127.0.0.1')['rows'],'Domain search')
            check(query(history,'/seed')['rows'],'URL search')
            check(not query(history,'no-such-result')['rows'],'Empty search result')
            call('switchTab',state()['tabs'][0]['id'])
            u=ctypes.windll.user32
            u.SendMessageW.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_ssize_t];u.SendMessageW.restype=ctypes.c_ssize_t
            u.GetMenuItemCount.argtypes=[ctypes.c_void_p];u.GetMenuStringW.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_wchar_p,ctypes.c_int,ctypes.c_uint]
            class Rect(ctypes.Structure):_fields_=[('left',ctypes.c_long),('top',ctypes.c_long),('right',ctypes.c_long),('bottom',ctypes.c_long)]
            u.GetMenuItemRect.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint,ctypes.POINTER(Rect)]
            def menu_window():
                found=[]
                @ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)
                def collect(hwnd,_):
                    pid=ctypes.c_ulong();u.GetWindowThreadProcessId(hwnd,ctypes.byref(pid));kind=ctypes.create_unicode_buffer(80);u.GetClassNameW(ctypes.c_void_p(hwnd),kind,80)
                    if pid.value==process.pid and kind.value=='#32768':found.append(hwnd)
                    return True
                u.EnumWindows(collect,0);return found[0] if found else None
            box=s.evaluate(shell,"(()=>{const n=document.querySelector('.browser-toolbar');const r=n.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+2}})()")
            for event in ['mouseMoved','mousePressed','mouseReleased']:
                params=dict(type=event,x=box['x'],y=box['y'])
                if event!='mouseMoved':params.update(button='right',clickCount=1)
                s.sequence+=1;shell.send(json.dumps(dict(id=s.sequence,method='Input.dispatchMouseEvent',params=params)))
            hwnd=wait(menu_window);menu=u.SendMessageW(hwnd,0x01E1,0,0);labels=[]
            for i in range(u.GetMenuItemCount(menu)):
                label=ctypes.create_unicode_buffer(256);u.GetMenuStringW(menu,i,label,256,0x400);labels.append(label.value)
            check(any(label.startswith('История') for label in labels),'Actual top-toolbar context menu contains History')
            index=next(i for i,label in enumerate(labels) if label.startswith('История'));rect=Rect();check(u.GetMenuItemRect(None,menu,index,ctypes.byref(rect)),'History menu item has a native hit target')
            u.SetCursorPos((rect.left+rect.right)//2,(rect.top+rect.bottom)//2);time.sleep(.1);u.mouse_event(0x0002,0,0,0,0);u.mouse_event(0x0004,0,0,0,0)
            wait(lambda:not menu_window());check(wait(lambda:state()['activeTabId']==bridge(history,'state')['tabId']),'Selecting History from the actual native menu opens its internal page')
            history_id=bridge(history,'state')['tabId'];count=len(state()['tabs'])
            bridge(history,'open',dict(url=origin+'/from-history',newTab=True))
            check(wait(lambda:len(state()['tabs'])==count+1 and next(t['url'] for t in state()['tabs'] if t['active'])==origin+'/from-history'),'A history entry opens in a new native tab')
            call('switchTab',history_id);wait(lambda:s.evaluate(history,"document.querySelector('.visit-link')!==null"))
            s.command(history,'Runtime.evaluate',dict(expression="document.querySelector('.visit-link').click()",awaitPromise=False))
            check(wait(lambda:next(t['url'] for t in state()['tabs'] if t['active']).startswith(origin)),'A history row opens in its current tab')
            call('back');wait(lambda:s.evaluate(history,"location.href.includes('/ui/history.html')&&typeof window.souluHistoryClear==='function'"))
            db=sqlite3.connect(profile/'soulu-history.sqlite')
            now=time.time()*1000
            fixtures=[('old-control',origin+'/old','Old control','',now-40*864e5,'old control '+origin+'/old'),('recent-control',origin+'/recent','Recent control','',now-5*60e3,'recent control '+origin+'/recent')]
            db.executemany('INSERT INTO visits VALUES(?,?,?,?,?,?)',fixtures);db.commit()
            check(any(r['id']=='recent-control' for r in query(history,begin=now-7*864e5)['rows']) and not any(r['id']=='old-control' for r in query(history,begin=now-7*864e5)['rows']),'Last-seven-days filtering excludes older visits')
            check(query(history)['rows']==sorted(query(history)['rows'],key=lambda r:(r['visited'],r['id']),reverse=True),'Newest-first sorting')
            bridge(history,'remove',dict(id='recent-control'));check(not query(history,'/recent')['rows'],'Individual deletion reaches SQLite')
            check(not db.execute("SELECT 1 FROM visits WHERE id='recent-control'").fetchall(),'Deleted visit absent from the persisted database')
            check(clear(history,history=True,range='15m')['ok'],'Last-15-minutes history clearing succeeds')
            check(query(history)['rows'][0]['id']=='old-control','Time-limited clearing preserves older history')
            check(s.evaluate(content,"localStorage.getItem('history-test')")== 'keep','History-only clearing preserves site data')
            check(any(c['name']=='soulu_history_cookie' for c in s.command(content,'Network.getAllCookies')['cookies']),'History-only clearing preserves cookies')
            for period in ['hour','day','week','month']:
                check(clear(history,history=True,range=period)['ok'],f'Native history range {period}')
            # Seed more than one page of results using the already-created store.
            db.executemany('INSERT INTO visits VALUES(?,?,?,?,?,?)',[(f'page-{i:03}',origin+f'/page-{i}',f'Visit {i}','',now-40*864e5-i,f'visit {i} '+origin) for i in range(125)]);db.commit()
            first=query(history);second=query(history,offset=100)
            check(len(first['rows'])==100 and first['more'] and len(second['rows'])==26,'History pagination returns every visit without truncation')
            check(not set(r['id'] for r in first['rows'])&set(r['id'] for r in second['rows']),'History pages do not duplicate rows')
            b,_=new_content('createProfile','History isolation');profile_b=state()['activeProfileId'];s.navigate(b,origin+'/profile-b')
            s.evaluate(b,"localStorage.setItem('history-test','profile-b');document.cookie='profile_b=keep; Path=/'")
            call('openHistory');h_b=page('/ui/history.html')
            # Select the target for B explicitly (A's document remains alive).
            for target in s.targets():
                if '/ui/history.html' in target.get('url',''):
                    candidate=socket(target)
                    if bridge(candidate,'state')['profile']==profile_b:h_b=candidate;break
            check(all('/profile-b' in r['url'] for r in query(h_b)['rows']),'Profile B history excludes Profile A')
            call('switchProfile','personal');check(rejected(h_b,'open',dict(url=origin+'/wrong-profile',newTab=True)),'A background History document cannot open a foreground tab in another active profile')
            check(clear(history,history=True)['ok'],'Full history clearing succeeds')
            check(not query(history)['rows'] and query(h_b)['rows'],'Clearing A preserves B history')
            # Site data still exists after its visits were deleted: no origin list shortcut.
            check(clear(history,sites=True)['ok'],'Profile-wide cookies and site storage clearing completes through Chromium')
            check(not any(c['name']=='soulu_history_cookie' for c in s.command(content,'Network.getAllCookies')['cookies']),'Cookies actually removed')
            check(s.evaluate(content,"localStorage.getItem('history-test')") is None,'LocalStorage actually removed')
            check(s.evaluate(content,"indexedDB.databases().then(d=>!d.some(x=>x.name==='history-test'))"),'IndexedDB actually removed')
            check(s.evaluate(content,"caches.keys().then(k=>!k.includes('history-cache'))"),'Cache Storage actually removed')
            check(s.evaluate(b,"localStorage.getItem('history-test')")=='profile-b','Site clearing A preserves Profile B storage')
            check(any(c['name']=='profile_b' for c in s.command(b,'Network.getAllCookies')['cookies']),'Site clearing A preserves Profile B cookies')
            s.evaluate(content,"fetch('/cached').then(r=>r.text())");before=Site.cache_requests
            s.evaluate(content,"fetch('/cached').then(r=>r.text())");check(Site.cache_requests==before,'HTTP-cache fixture is a real cache hit')
            check(clear(history,cache=True)['ok'],'HTTP cache clearing completes')
            s.evaluate(content,"fetch('/cached').then(r=>r.text())");check(Site.cache_requests>before,'HTTP-cache clear causes a real network request')
            check(call('getBookmarks') and call('getPasswords') and call('revealPassword',call('getPasswords')[0]['id'])=='history-vault-control','Clearing preserves bookmarks and password vault')
            call('setSettings',dict(saveHistory=False));s.navigate(content,origin+'/not-recorded')
            check(not query(history,'/not-recorded')['rows'],'Save-history setting prevents new records')
            call('setSettings',dict(saveHistory=True));s.navigate(content,origin+'/recorded')
            check(query(history,'/recorded')['rows'],'Re-enabling history records real visits')
            private,_=new_content('newIncognito');s.navigate(private,origin+'/private')
            check(not query(history,'/private')['rows'],'Incognito navigation does not enter normal history')
            call('openHistory');h_private=page('/ui/history.html',True)
            check(rejected(h_private,'query',dict(search='',begin=0,end=864e13,offset=0)),'Private history document cannot read normal history')
            check(rejected(h_private,'clear',dict(history=True,sites=True,cache=True,range='all',webAllTimeAcknowledged=True)),'Private history document cannot clear normal profile data')
            call('switchProfile','personal')
            # Visual checks use the native renderer at every requested device scale.
            if output:
                visuals=output.parent/'history-visuals';visuals.mkdir(parents=True,exist_ok=True)
                db.executemany('INSERT INTO visits VALUES(?,?,?,?,?,?)',[(f'visual-{i}',origin+f'/visual/{i}','Название страницы '+str(i)+' · Soulu browser history','',now-i*864e5,'visual '+origin) for i in range(25)]);db.commit()
                call('switchTab',bridge(history,'state')['tabId'])
                s.evaluate(history,"window.dispatchEvent(new Event('focus'))")
                wait(lambda:s.evaluate(history,"document.querySelectorAll('.visit').length>=25"))
                for theme in ['light','dark','system']:
                    call('setSettings',dict(theme=theme));time.sleep(1.7)
                    for dpi in [1,1.25,1.5,1.75,2]:
                        for width,height in [(1100,800),(420,600)]:
                            s.command(history,'Emulation.setDeviceMetricsOverride',dict(width=width,height=height,deviceScaleFactor=dpi,mobile=False))
                            check(s.evaluate(history,"document.documentElement.scrollWidth<=innerWidth+1"),f'History has no horizontal overflow: {theme}, {dpi}, {width}')
                            image=s.command(history,'Page.captureScreenshot',dict(format='png'))['data'];(visuals/f'history-{theme}-{dpi}-{width}.png').write_bytes(base64.b64decode(image))
                            s.evaluate(history,"souluHistoryClear();document.getElementById('sitesChoice').click()")
                            check(s.evaluate(history,"(()=>{const r=document.getElementById('clearDialog').getBoundingClientRect();return r.left>=0&&r.right<=innerWidth&&r.top>=0&&r.bottom<=innerHeight})()"),f'Clear dialog fits: {theme}, {dpi}, {width}')
                            check(s.evaluate(history,"(()=>{const d=document.getElementById('clearDialog').getBoundingClientRect(),r=document.getElementById('confirmClear').getBoundingClientRect();return r.top>=d.top&&r.bottom<=d.bottom})()"),f'Clear actions stay visible: {theme}, {dpi}, {width}')
                            image=s.command(history,'Page.captureScreenshot',dict(format='png'))['data'];(visuals/f'{theme}-{dpi}-{width}.png').write_bytes(base64.b64decode(image))
                            s.evaluate(history,"document.getElementById('clearDialog').close();document.getElementById('sitesChoice').checked=false")
                    s.command(history,'Emulation.clearDeviceMetricsOverride')
            db.close();stop();start();call('openHistory');history=page('/ui/history.html')
            check(query(history,'/recorded')['rows'],'History persists after a normal browser restart')
            check(not query(history,'/old')['rows'],'Cleared history stays deleted after restart')
            passed=True
        finally:
            if output:output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(dict(checks=checks,passed=passed),ensure_ascii=False,indent=2),encoding='utf-8')
            if process:
                try:stop()
                except Exception:process.kill();process.wait()
            server.shutdown()
if __name__=='__main__':main()
