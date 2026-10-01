"""Exercise real CEF profile/incognito contexts and the native settings bridge."""
import http.server
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

IDB_WRITE="""new Promise((resolve,reject)=>{
 const r=indexedDB.open('isolation',1);r.onupgradeneeded=()=>r.result.createObjectStore('data');
 r.onerror=()=>reject(r.error);r.onsuccess=()=>{const t=r.result.transaction('data','readwrite');
 t.objectStore('data').put('private-value','key');t.oncomplete=()=>{r.result.close();resolve(true)}};
})"""

with tempfile.TemporaryDirectory(prefix='soulu-profiles-',ignore_cleanup_errors=True) as root:
    os.environ['LOCALAPPDATA']=root
    home=http.server.ThreadingHTTPServer(('127.0.0.1',s.free_port()),s.SiteHandler)
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
        secret='vault-integration-test-'+str(time.time_ns())
        invoke(shell,'addPassword',{'origin':origin,'username':'user','password':secret})
        password_id=invoke(shell,'getPasswords')[0]['id']
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
        s.evaluate(private,"localStorage.setItem('isolation','incognito');document.cookie='private_cookie=test; Path=/'")
        s.evaluate(private,IDB_WRITE)
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
            if path.is_file():assert secret.encode() not in path.read_bytes(),f'Plaintext credential in {path.name}'
        process=s.launch(sys.argv[1])
        shell=socket_for(wait(lambda:next((t for t in s.targets() if '/ui/index.html' in t.get('url','')),None)));sockets.append(shell)
        wait(lambda:s.evaluate(shell,"typeof window.browserShell==='object'"))
        assert invoke(shell,'revealPassword',password_id)==secret
        assert invoke(shell,'getSiteRules')['sites']['example.com']['camera']==0
        invoke(shell,'switchProfile',second_id)
        assert invoke(shell,'getPasswords')==[]
        assert 'example.com' not in invoke(shell,'getSiteRules')['sites']
        for connection in sockets:connection.close()
        sockets=[];s.close_normally(process)
        print(json.dumps({'profile_storage_isolation':True,'incognito_new_session_empty':True,
                          'password_restart':True,'plaintext_absent':True,'rules_restart_isolation':True}))
    finally:
        for connection in sockets:connection.close()
        if process.poll() is None:process.kill();process.wait()
        home.shutdown()
