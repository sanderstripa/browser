"""First-run behavior against the actual CEF app and profile files, never a mock UI."""
import base64
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sqlite3
import sys
import tempfile
import time

spec=importlib.util.spec_from_file_location('storage',Path(__file__).with_name('test-cef-storage.py'))
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
checks=[]
os.environ['NO_PROXY']='localhost,127.0.0.1,::1'
os.environ.pop('SOULU_REGRESSION_SKIP_FIRST_RUN',None)
def wait(fn,timeout=30):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        result=fn()
        if result:return result
        time.sleep(.1)
    raise AssertionError('Onboarding timeout')
def check(condition,name):
    assert condition,name
    checks.append(name);print('PASS:',name,flush=True)
def main():
    exe=str(Path(sys.argv[1]).resolve());output=Path(sys.argv[2]) if len(sys.argv)>2 else None
    with tempfile.TemporaryDirectory(prefix='soulu-first-run-',ignore_cleanup_errors=True) as temp:
        root=Path(temp);local=root/'local';roaming=root/'roaming';local.mkdir();roaming.mkdir()
        env=dict(os.environ,LOCALAPPDATA=str(local),APPDATA=str(roaming),SOULU_UI_TEST_PORT=str(s.DEBUG_PORT))
        profile=local/'Soulu'/'User Data'/'Profiles'/'personal'
        process=None;connections=[]
        def socket(fragment):
            target=wait(lambda:next((t for t in s.targets() if fragment in t.get('url','')),None))
            ws=s.websocket.create_connection(target['webSocketDebuggerUrl'],timeout=30,origin=s.BASE);connections.append(ws);return ws
        def start():
            nonlocal process
            process=subprocess.Popen([exe,'--no-proxy-server'],env=env)
            shell=socket('/ui/index.html');wait(lambda:s.evaluate(shell,"typeof window.browserShell?.getState==='function'"));return shell
        def call(ws,action,payload=None):
            return s.evaluate(ws,"new Promise((resolve,reject)=>cefQuery({request:"+json.dumps(json.dumps({'action':'onboarding.'+action,'payload':payload}))+",onSuccess:v=>resolve(v?JSON.parse(v):null),onFailure:(_,m)=>reject(Error(m))}))")
        def finish(ws,skip=False):
            s.command(ws,'Runtime.evaluate',{'expression':"cefQuery({request:JSON.stringify({action:'onboarding.finish',payload:{skip:"+str(skip).lower()+"}}),onSuccess:()=>{},onFailure:()=>{}})"})
            wait(lambda:json.loads((profile/'soulu-settings.json').read_text())['onboarding']['status']!='not_started')
        def stop():
            nonlocal process
            for ws in connections:
                try:ws.close()
                except Exception:pass
            connections.clear();s.close_normally(process);process=None
        def flow():return json.loads((profile/'soulu-settings.json').read_text())['onboarding']
        def ui(ws):return wait(lambda:call(ws,'get'))
        def click(ws,text):
            s.evaluate(ws,"[...document.querySelectorAll('button')].find(n=>n.textContent==="+json.dumps(text)+").click()")
            wait(lambda:not s.evaluate(ws,"document.querySelector('main').getAttribute('aria-busy')==='true'"))
        try:
            shell=start();page=socket('/ui/onboarding.html');wait(lambda:s.evaluate(page,"document.querySelector('main').getAttribute('aria-busy')==='false'"))
            check(ui(page)['flow']['status']=='not_started','New normal profile launches first-run')
            check(len(s.evaluate(shell,'browserShell.getState()')['tabs'])==1,'Onboarding uses one native tab')
            check(s.evaluate(page,"document.querySelector('.brand img').getAttribute('src')==='branding/soulu-128.png'"),'Production brand asset used')
            check(s.evaluate(page,"performance.getEntriesByType('resource').every(r=>r.name.startsWith('file:'))"),'Onboarding has only local assets')
            click(page,'Начать');check(flow()['step']==2,'Welcome primary advances')
            check(ui(page)['sources']==[],'No readable supported source produces honest empty state')
            check(all(r['browser'] in ['Chrome','Edge','Firefox'] for r in ui(page)['sources']),'Only supported importer catalogs are exposed')
            check(s.evaluate(page,"!document.querySelector('#art').textContent.match(/Chrome|Edge|Firefox/)"),'Neutral import hero has no browser brands')
            click(page,'Назад');check(flow()['step']==1,'Back retains flow')
            click(page,'Начать')
            # Real local importer fixtures; dummy installation files exercise
            # discovery without installing or touching the user's browsers.
            secret='first-run-local-fixture-secret'
            class Blob(ctypes.Structure):_fields_=[('size',ctypes.c_ulong),('data',ctypes.POINTER(ctypes.c_ubyte))]
            buffer=ctypes.create_string_buffer(secret.encode());source_blob=Blob(len(secret),ctypes.cast(buffer,ctypes.POINTER(ctypes.c_ubyte)));encrypted=Blob()
            assert ctypes.windll.crypt32.CryptProtectData(ctypes.byref(source_blob),None,None,None,None,1,ctypes.byref(encrypted))
            protected=ctypes.string_at(encrypted.data,encrypted.size);ctypes.windll.kernel32.LocalFree(encrypted.data)
            fixtures={}
            for browser,folder,program in [('Chrome','Google/Chrome','Google/Chrome/Application/chrome.exe'),('Edge','Microsoft/Edge','Microsoft/Edge/Application/msedge.exe')]:
                installation=local/program;installation.parent.mkdir(parents=True,exist_ok=True);installation.write_bytes(b'fixture-only')
                store=local/folder/'User Data'/'Default';store.mkdir(parents=True)
                (store.parent/'Local State').write_text('{}')
                db=sqlite3.connect(store/'Login Data');db.execute('CREATE TABLE logins(origin_url TEXT,username_value TEXT,password_value BLOB,blacklisted_by_user INTEGER)')
                db.execute('INSERT INTO logins VALUES(?,?,?,0)',('https://example.com','fixture',protected))
                db.execute('INSERT INTO logins VALUES(?,?,?,0)',('https://example.com','app-bound',b'v20'+b'x'*64));db.commit();db.close();fixtures[browser]=store/'Login Data'
            firefox=roaming/'Mozilla/Firefox';ff=firefox/'Profiles/test';ff.mkdir(parents=True)
            (firefox/'profiles.ini').write_text('[Profile0]\nName=Fixture Firefox\nIsRelative=1\nPath=Profiles/test\n')
            fixtures['Firefox']=ff/'logins.json';fixtures['Firefox'].write_text('{"logins":[]}')
            installation=local/'Mozilla Firefox/firefox.exe';installation.parent.mkdir(parents=True);installation.write_bytes(b'fixture-only')
            for selected in ['Chrome','Edge','Firefox',None]:
                for browser,path in fixtures.items():
                    inactive=path.with_suffix('.inactive')
                    if browser==selected:
                        if inactive.exists():inactive.rename(path)
                    elif path.exists():path.rename(inactive)
                actual={r['browser'] for r in ui(page)['sources']}
                check(actual==({selected} if selected else set()),f'Only {selected or "no"} supported browser profile is exposed')
            # An unsupported provider with files must never appear.
            brave=local/'BraveSoftware/Brave-Browser/User Data/Default';brave.mkdir(parents=True);(brave/'Login Data').write_bytes(b'unsupported')
            check(ui(page)['sources']==[],'Unsupported browser is excluded')
            for path in fixtures.values():path.with_suffix('.inactive').rename(path)
            check({r['browser'] for r in ui(page)['sources']}=={'Chrome','Edge','Firefox'},'Multiple supported sources are discovered without multi-import claims')
            s.command(page,'Page.reload');wait(lambda:s.evaluate(page,"document.querySelector('main').getAttribute('aria-busy')==='false'"))
            click(page,'Далее');check(flow()['step']==3,'Selected source advances to types')
            check(s.evaluate(page,"document.querySelectorAll('.type input:disabled').length===2"),'Unsupported bookmarks/history categories are disabled')
            click(page,'Назад');check(flow()['source']=='Chrome:Default','Back retains selected source')
            click(page,'Далее');click(page,'Импортировать')
            check(flow()['importReport']['imported']==1 and flow()['importReport']['protected']==1 and flow()['importReport']['status']=='partial','First-run invokes actual DPAPI importer and reports partial result')
            check(secret not in (profile/'soulu-passwords.json').read_text(),'Imported passwords are encrypted on disk')
            check(flow()['step']==3,'Partial result remains visible before continuing')
            call(page,'import');check(flow()['importReport']['imported']==1,'Repeated import action returns saved report without importing again')
            click(page,'Продолжить');check(flow()['step']==4,'Partial import can continue')
            call(page,'progress',{'step':4,'adblock':True,'askPermissions':False});call(page,'privacy')
            policy=json.loads((profile/'soulu-site-rules.json').read_text())
            check(policy['blocking']['enabled'] and all(policy['defaults'][k]==2 for k in ['camera','microphone','geolocation','notifications']),'Privacy writes existing site policy with deny when prompt off')
            call(page,'progress',{'step':5,'adblock':True,'askPermissions':False});stop()
            shell=start();page=socket('/ui/onboarding.html');wait(lambda:s.evaluate(page,"document.querySelector('#vpnKey')!==null"))
            check(ui(page)['flow']['step']==5 and ui(page)['flow']['privacyApplied'],'Restart resumes per-profile step and outcomes')
            click(page,'Добавить');check(flow()['step']==5 and s.evaluate(page,"document.querySelector('#message').textContent.length>0"),'Empty VPN is rejected')
            s.evaluate(page,"document.querySelector('#vpnKey').value='unsupported://host';document.querySelector('#vpnKey').dispatchEvent(new Event('input'))")
            click(page,'Добавить');check(flow()['step']==5,'Unsupported VPN is rejected')
            click(page,'Пропустить');check(flow()['step']==6,'VPN skip reaches completion')
            check(s.evaluate(page,"document.querySelector('.summary').textContent.includes('Импортировано паролей: 1') && document.querySelector('.summary').textContent.includes('частично') && document.querySelector('.summary').textContent.includes('Приватность настроена') && document.querySelector('.summary').textContent.includes('VPN можно добавить позже')"),'Summary reports actual partial/applied/skipped outcomes')
            for width,height,scale in [(1280,800,1),(800,500,1),(480,360,1),(900,600,1.5),(900,600,2)]:
                s.command(page,'Emulation.setDeviceMetricsOverride',{'width':width,'height':height,'deviceScaleFactor':scale,'mobile':False})
                check(s.evaluate(page,"document.documentElement.scrollWidth<=innerWidth && document.querySelector('.card').getBoundingClientRect().bottom<=innerHeight+1"),f'No overflow at {width}x{height} scale {scale}')
                s.evaluate(page,"document.querySelector('.content').scrollTop=10000")
                check(s.evaluate(page,"document.querySelector('#actions button:last-child').getBoundingClientRect().bottom<=innerHeight+1"),'Final actions reachable after internal scroll')
            s.command(page,'Emulation.clearDeviceMetricsOverride')
            s.command(page,'Input.dispatchKeyEvent',{'type':'keyDown','key':'Escape','windowsVirtualKeyCode':27})
            check(flow()['status']=='not_started','Escape does not dismiss flow')
            finish(page);socket('/ui/home.html');check(flow()['status']=='completed','Completion opens ordinary Soulu')
            check(len(s.evaluate(shell,'browserShell.getState()')['tabs'])==1,'Completion adds no extra tab')
            stop();shell=start();socket('/ui/home.html');check(not any('/ui/onboarding.html' in t.get('url','') for t in s.targets()),'Completed profile has no automatic onboarding on restart')
            state=s.evaluate(shell,"browserShell.createProfile('Second first-run')")
            page=socket('/ui/onboarding.html');wait(lambda:s.evaluate(page,"document.querySelector('main').getAttribute('aria-busy')==='false'"))
            check(ui(page)['flow']['status']=='not_started','Second new profile owns a separate first-run state')
            second=s.evaluate(shell,'browserShell.getState()')['activeProfileId']
            call(page,'progress',{'step':5});s.command(page,'Page.reload')
            wait(lambda:s.evaluate(page,"document.querySelector('#vpnKey')!==null"))
            key='vless://11111111-1111-4111-8111-111111111111@127.0.0.1:443?encryption=none&security=none&type=xhttp&path=%2F#Fixture'
            parsed=s.evaluate(page,'souluParseVpnKey('+json.dumps(key)+')')
            saved=call(page,'vpn',parsed);check(saved['ok'],'Valid VLESS key is saved through existing native helper without connecting')
            second_flow=json.loads((profile.parent/second/'soulu-settings.json').read_text())['onboarding']
            check(second_flow['vpnAdded'] and second_flow['vpnId'],'VPN success persists a real native profile ID')
            vpn=s.evaluate(shell,'vpn.settingsGet()');check(vpn['link']==key and vpn['lastProfileId']==second_flow['vpnId'],'Ordinary VPN settings see onboarding key')
            status=s.evaluate(shell,"vpn.send('status')");check(not status.get('connected',False),'Adding VPN never auto-connects')
            # Dispatch a skip and inspect the correct second profile through native state.
            s.command(page,'Runtime.evaluate',{'expression':"cefQuery({request:JSON.stringify({action:'onboarding.finish',payload:{skip:true}}),onSuccess:()=>{},onFailure:()=>{}})"})
            wait(lambda:not any('/ui/onboarding.html' in t.get('url','') for t in s.targets()))
            check(json.loads((profile.parent/second/'soulu-settings.json').read_text())['onboarding']['status']=='skipped','Skip state is profile-scoped and persisted')
            s.evaluate(shell,'browserShell.newIncognito()');time.sleep(.5)
            check(not any('/ui/onboarding.html' in t.get('url','') for t in s.targets()),'Incognito never starts first-run')
            check(flow()['status']=='completed','Incognito does not mutate normal first-run status')
            stop()
            # Simulate an existing install without the new marker, preserving values.
            settings=json.loads((profile/'soulu-settings.json').read_text());settings.pop('onboarding');settings['theme']='dark';settings['startupMode']='continue'
            (profile/'soulu-settings.json').write_text(json.dumps(settings))
            (profile/'soulu-session.json').write_text(json.dumps({'tabs':['soulu://home','about:blank'],'active':1}))
            shell=start();wait(lambda:len(s.evaluate(shell,'browserShell.getState()')['tabs'])==2)
            check(not any('/ui/onboarding.html' in t.get('url','') for t in s.targets()),'Existing profile update restores its session without first-run')
            check(json.loads((profile/'soulu-settings.json').read_text())['theme']=='dark','Migration preserves existing settings')
            check(flow()['status']=='skipped','Existing profile migration records skipped')
        finally:
            if process:stop()
    if output:output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps({'passed':True,'checks':checks},indent=2),encoding='utf-8')
if __name__=='__main__':main()
