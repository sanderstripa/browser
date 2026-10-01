"""Exercise real CEF profile/incognito contexts and the native settings bridge."""
import http.server
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time

spec=importlib.util.spec_from_file_location('storage',Path(__file__).with_name('test-cef-storage.py'))
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)

def wait(fn,timeout=25):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        value=fn()
        if value:return value
        time.sleep(.1)
    raise AssertionError('Native state timeout')

def socket_for(target):
    return s.websocket.create_connection(target['webSocketDebuggerUrl'],timeout=30,origin=s.BASE)

def invoke(shell,method,payload=None):
    return s.evaluate(shell,'window.browserShell.'+method+'('+('' if payload is None else json.dumps(payload))+')')

def new_page(shell,method,payload=None):
    old={t['id'] for t in s.targets()}
    result=invoke(shell,method,payload)
    target=wait(lambda:next((t for t in s.targets() if t['id'] not in old and t.get('type')=='page'),None))
    return socket_for(target),result

def answer_native_dialog(process,title,yes=True):
    user32=ctypes.windll.user32;result=[]
    def answer():
        deadline=time.monotonic()+20
        while time.monotonic()<deadline:
            found=[]
            @ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)
            def visit(hwnd,_):
                pid=ctypes.c_ulong();user32.GetWindowThreadProcessId(hwnd,ctypes.byref(pid))
                caption=ctypes.create_unicode_buffer(256);kind=ctypes.create_unicode_buffer(80)
                user32.GetWindowTextW(hwnd,caption,256);user32.GetClassNameW(hwnd,kind,80)
                if pid.value==process.pid and kind.value=='#32770' and caption.value==title:found.append(hwnd)
                return True
            user32.EnumWindows(visit,0)
            if found:
                user32.GetDlgItem.restype=ctypes.c_void_p
                control=user32.GetDlgItem(ctypes.c_void_p(found[0]),6 if yes else 7)
                if control:user32.SendMessageW(ctypes.c_void_p(control),0x00F5,0,0);result.append(True);return
            time.sleep(.1)
        result.append(False)
    worker=threading.Thread(target=answer,daemon=True);worker.start()
    return worker,result

class LoginHandler(s.SiteHandler):
    def do_POST(self):
        self.rfile.read(int(self.headers.get('Content-Length','0')))
        body=b'<!doctype html><title>Signed in fixture</title>'
        self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)

def submit_form(connection,secret,username):
    form=f'<form action="/signed-in" method="post"><input name="username" autocomplete="username" value="{username}"><input name="password" type="password" autocomplete="current-password" value="{secret}"><button id="login">Sign in</button></form>'
    s.evaluate(connection,'document.body.innerHTML='+json.dumps(form))
    s.evaluate(connection,"window.fixtureSubmitted=false;document.addEventListener('submit',e=>{sessionStorage.setItem('fixtureSubmitTrusted',String(e.isTrusted));window.fixtureSubmitted=true},{once:true})")
    s.command(connection,'Page.bringToFront')
    point=s.evaluate(connection,"(()=>{const r=document.getElementById('login').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()")
    s.command(connection,'Input.dispatchMouseEvent',dict(type='mouseMoved',**point))
    s.command(connection,'Input.dispatchMouseEvent',dict(type='mousePressed',button='left',clickCount=1,**point))
    s.command(connection,'Input.dispatchMouseEvent',dict(type='mouseReleased',button='left',clickCount=1,**point))

IDB_WRITE="""new Promise((resolve,reject)=>{
 const r=indexedDB.open('isolation',1);r.onupgradeneeded=()=>r.result.createObjectStore('data');
 r.onerror=()=>reject(r.error);r.onsuccess=()=>{const t=r.result.transaction('data','readwrite');
 t.objectStore('data').put('private-value','key');t.oncomplete=()=>{r.result.close();resolve(true)}};
})"""

with tempfile.TemporaryDirectory(prefix='soulu-profiles-',ignore_cleanup_errors=True) as root:
    os.environ['LOCALAPPDATA']=root
    home=http.server.ThreadingHTTPServer(('127.0.0.1',s.free_port()),LoginHandler)
    threading.Thread(target=home.serve_forever,daemon=True).start()
    origin=f'http://127.0.0.1:{home.server_port}'
    process=s.launch(sys.argv[1]);sockets=[]
    try:
        shell=socket_for(wait(lambda:next((t for t in s.targets() if '/ui/index.html' in t.get('url','')),None)));sockets.append(shell)
        wait(lambda:s.evaluate(shell,"typeof window.browserShell==='object'"))
        first=s.page_socket();sockets.append(first);s.navigate(first,origin+'/seed')
        s.evaluate(first,"localStorage.setItem('isolation','first');sessionStorage.setItem('session','first')")
        s.evaluate(first,IDB_WRITE)
        invoke(shell,'setSettings',{'theme':'dark'})
        legacy=json.loads((Path(root)/'Soulu'/'User Data'/'settings.json').read_text(encoding='utf-8'))
        assert legacy['settings']['theme']!='dark','Profile settings changed the global migration template'
        assert 'vpn' in legacy,'The existing global VPN owner was lost'
        secret='vault-integration-test-'+str(time.time_ns())
        private_secret='private-web-test-'+str(time.time_ns())
        invoke(shell,'addPassword',{'origin':origin,'username':'user','password':secret})
        password_id=invoke(shell,'getPasswords')[0]['id']
        worker,answer=answer_native_dialog(process,'Пароли Soulu')
        submit_form(first,secret+'-form','form-user');worker.join(22)
        assert answer==[True],'Save prompt was not shown for a real form submit'
        wait(lambda:len(invoke(shell,'getPasswords'))==2)
        # The consent prompt can close before the submitted POST navigation
        # commits. Do not replace its document while that navigation is pending.
        wait(lambda:s.evaluate(first,"location.pathname==='/signed-in'&&document.readyState==='complete'"))
        s.navigate(first,origin+'/fixture')
        worker,answer=answer_native_dialog(process,'Пароли Soulu',False)
        submit_form(first,secret+'-declined','declined-user');worker.join(22)
        assert answer==[True],('Decline prompt was not shown',s.evaluate(first,"({path:location.pathname,submitted:window.fixtureSubmitted,trusted:sessionStorage.getItem('fixtureSubmitTrusted')})"))
        assert len(invoke(shell,'getPasswords'))==2,'Declined credentials were saved'
        second,state=new_page(shell,'createProfile','Second');sockets.append(second)
        second_id=state['activeProfileId'];s.navigate(second,origin+'/verify')
        assert s.evaluate(second,"localStorage.getItem('isolation')") is None
        assert s.evaluate(second,"sessionStorage.getItem('session')") is None
        assert not s.evaluate(second,"indexedDB.databases()")
        assert not any(c['name']=='soulu_auth' for c in s.command(second,'Network.getAllCookies')['cookies'])
        assert invoke(shell,'getPasswords')==[]
        assert invoke(shell,'getSettings')['theme']!='dark'
        s.evaluate(second,"localStorage.setItem('isolation','second')")
        invoke(shell,'switchProfile','personal')
        assert s.evaluate(first,"localStorage.getItem('isolation')")=='first'
        assert invoke(shell,'getSettings')['theme']=='dark'
        assert invoke(shell,'revealPassword',password_id)==secret
        invoke(shell,'setSiteRule',{'domain':'','permission':'camera','value':2})
        rules=invoke(shell,'setSiteRule',{'domain':'HTTPS://Example.COM:443/','permission':'camera','value':0})
        assert rules['sites']['example.com']['camera']==0
        private,_=new_page(shell,'newIncognito');sockets.append(private)
        s.navigate(private,origin+'/verify')
        assert s.evaluate(private,"localStorage.getItem('isolation')") is None
        assert not any(c['name']=='soulu_auth' for c in s.command(private,'Network.getAllCookies')['cookies'])
        s.evaluate(private,"localStorage.setItem('isolation',"+json.dumps(private_secret)+
                   ");document.cookie="+json.dumps('private_cookie='+private_secret+'; Path=/'))
        s.evaluate(private,IDB_WRITE.replace('private-value',private_secret))
        assert s.evaluate(shell,"window.browserShell.getPasswords().then(()=>false,()=>true)")
        private_id=invoke(shell,'getState')['activeTabId'];invoke(shell,'closeTab',private_id)
        wait(lambda:not invoke(shell,'getState')['incognito'])
        fresh,_=new_page(shell,'newIncognito');sockets.append(fresh);s.navigate(fresh,origin+'/verify')
        assert s.evaluate(fresh,"localStorage.getItem('isolation')") is None
        assert not s.evaluate(fresh,"indexedDB.databases()")
        assert not any(c['name']=='private_cookie' for c in s.command(fresh,'Network.getAllCookies')['cookies'])
        invoke(shell,'closeTab',invoke(shell,'getState')['activeTabId'])
        wait(lambda:not invoke(shell,'getState')['incognito'])
        for connection in sockets:
            connection.close()
        sockets=[];s.close_normally(process)
        for path in (Path(root)/'Soulu'/'User Data').rglob('*'):
            if path.is_file():
                data=path.read_bytes()
                for marker in (secret,private_secret):
                    assert marker.encode() not in data and marker.encode('utf-16-le') not in data,f'Plaintext credential or private web storage in {path.name}'
        process=s.launch(sys.argv[1])
        shell=socket_for(wait(lambda:next((t for t in s.targets() if '/ui/index.html' in t.get('url','')),None)));sockets.append(shell)
        wait(lambda:s.evaluate(shell,"typeof window.browserShell==='object'"))
        assert invoke(shell,'revealPassword',password_id)==secret
        assert invoke(shell,'getSiteRules')['sites']['example.com']['camera']==0
        invoke(shell,'switchProfile',second_id)
        assert invoke(shell,'getPasswords')==[]
        assert 'example.com' not in invoke(shell,'getSiteRules')['sites']
        invoke(shell,'switchProfile','personal')
        worker,answer=answer_native_dialog(process,'Soulu')
        rules=invoke(shell,'resetSiteRules','');worker.join(22)
        assert answer==[True] and rules['sites']=={},'Reset-all confirmation did not apply'
        worker,answer=answer_native_dialog(process,'Удаление профиля')
        state=invoke(shell,'deleteProfile',second_id);worker.join(22)
        assert answer==[True] and len(state['profiles'])==1,'Confirmed profile deletion failed'
        for connection in sockets:connection.close()
        sockets=[];s.close_normally(process)
        assert not (Path(root)/'Soulu'/'User Data'/'Profiles'/second_id).exists(),'Deleted profile files remained after shutdown'
        assert (Path(root)/'Soulu'/'User Data'/'Profiles'/'personal'/'soulu-passwords.json').exists(),'Deletion touched another profile'
        print(json.dumps({'profile_storage_isolation':True,'incognito_new_session_empty':True,
                          'password_restart':True,'plaintext_absent':True,'rules_restart_isolation':True}))
    finally:
        for connection in sockets:connection.close()
        if process.poll() is None:process.kill();process.wait()
        home.shutdown()
