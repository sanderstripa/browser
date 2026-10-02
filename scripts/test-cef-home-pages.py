"""Real native CEF checks for page intents, offline home, persistence and privacy."""
import base64
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

spec=importlib.util.spec_from_file_location('storage',Path(__file__).with_name('test-cef-storage.py'))
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
checks=[]

def wait(fn,timeout=30):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        result=fn()
        if result:return result
        time.sleep(.1)
    raise AssertionError('Home state timeout')

def check(value,name):
    assert value,name
    checks.append(name)
    print('PASS:',name,flush=True)

class Site(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'<!doctype html><title>Page intent fixture</title><a href="/two" target="_blank">Open</a>'
        self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def log_message(self,*args):pass

def socket(target):return s.websocket.create_connection(target['webSocketDebuggerUrl'],timeout=30,origin=s.BASE)

def main():
    exe=os.path.abspath(sys.argv[1]);output=Path(sys.argv[2]) if len(sys.argv)>2 else None
    server=http.server.ThreadingHTTPServer(('127.0.0.1',s.free_port()),Site)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    origin=f'http://127.0.0.1:{server.server_port}'
    with tempfile.TemporaryDirectory(prefix='soulu-home-',ignore_cleanup_errors=True) as root:
        os.environ['LOCALAPPDATA']=root
        env=dict(os.environ,SOULU_UI_TEST_PORT=str(s.DEBUG_PORT))
        process=None;shell=None;connections=[]
        profile=Path(root)/'Soulu'/'User Data'/'Profiles'/'personal'
        def start():
            nonlocal process,shell
            process=subprocess.Popen([exe,'--no-proxy-server'],env=env)
            shell=socket(wait(lambda:next((t for t in s.targets() if '/ui/index.html' in t.get('url','')),None)))
            connections.append(shell)
            wait(lambda:s.evaluate(shell,"typeof window.browserShell?.getState==='function'"))
            wait(lambda:state().get('tabs'))
        def state():return s.evaluate(shell,'browserShell.getState()')
        def call(method,payload=None):return s.evaluate(shell,'browserShell.'+method+'('+('' if payload is None else json.dumps(payload))+')')
        def page(fragment='/ui/home.html'):
            def find():
                for target in s.targets():
                    if fragment not in target.get('url',''):continue
                    ws=socket(target);connections.append(ws)
                    try:
                        if fragment=='/ui/home.html' and home(ws,'home.get')['tabId']!=state()['activeTabId']:continue
                        if s.evaluate(ws,"document.readyState==='complete'"):return ws
                    except Exception:continue
                return None
            return wait(find)
        def home(ws,action,payload=None):
            if action=='home.navigate':
                s.command(ws,'Runtime.evaluate',{'expression':"cefQuery({request:"+json.dumps(json.dumps({'action':action,'payload':payload}))+",onSuccess:()=>{},onFailure:()=>{}})",'awaitPromise':False})
                return None
            return s.evaluate(ws,"new Promise((resolve,reject)=>cefQuery({request:"+json.dumps(json.dumps({'action':action,'payload':payload}))+",onSuccess:v=>resolve(v?JSON.parse(v):null),onFailure:(_,m)=>reject(Error(m))}))")
        def stop():
            nonlocal process
            for ws in connections:
                try:ws.close()
                except Exception:pass
            connections.clear();s.close_normally(process);process=None
        def current_url():
            data=state();return next(t['url'] for t in data['tabs'] if t['active'])
        def one_tab():
            data=state();keep=data['activeTabId']
            for tab in data['tabs']:
                if tab['id']!=keep:call('closeTab',tab['id'])
            wait(lambda:len(state()['tabs'])==1)
        try:
            start();h=page()
            check(current_url()=='soulu://home' and len(state()['tabs'])==1,'Fresh startup is one offline local Soulu page')
            check(s.evaluate(h,"document.querySelector('#logo').textContent==='Soulu' && typeof window.souluHomeApply==='function'"),'Native home shell and bridge rendered')
            check(s.evaluate(h,"performance.getEntriesByType('resource').every(r=>r.name.startsWith('file:'))"),'Shell assets have no external dependencies')
            s.command(h,'Network.enable');s.command(h,'Network.emulateNetworkConditions',{'offline':True,'latency':0,'downloadThroughput':0,'uploadThroughput':0})
            s.command(h,'Page.reload');wait(lambda:s.evaluate(h,"typeof window.souluHomeApply==='function'"))
            check(home(h,'home.get')['homeShortcuts']==[],'Offline reload and scoped bridge work')
            check(home(h,'home.weather')['reason']=='provider-not-configured','Weather failure is explicit and independent')
            s.evaluate(h,"document.querySelector('#weatherToggle').click()")
            check(s.evaluate(h,"document.querySelector('#weatherToggle').getAttribute('aria-expanded')==='true'"),'Weather compact expansion is accessible')
            s.command(h,'Network.emulateNetworkConditions',{'offline':False,'latency':0,'downloadThroughput':-1,'uploadThroughput':-1})
            s.evaluate(h,"document.querySelector('#add').click();document.querySelector('#shortcutName').value='UI shortcut';document.querySelector('#shortcutUrl').value="+json.dumps(origin+'/ui')+";document.querySelector('#shortcutForm').requestSubmit()")
            wait(lambda:len(home(h,'home.get')['homeShortcuts'])==1)
            check(s.evaluate(h,"!document.querySelector('#editor').open"),'Shortcut dialog saves through real form')
            s.evaluate(h,"document.querySelector('.shortcut-tools button').click();document.querySelector('#shortcutName').value='UI edited';document.querySelector('#shortcutForm').requestSubmit()")
            wait(lambda:home(h,'home.get')['homeShortcuts'][0]['name']=='UI edited')
            s.evaluate(h,"document.querySelectorAll('.shortcut-tools button')[1].click()")
            wait(lambda:home(h,'home.get')['homeShortcuts']==[])
            check(True,'Shortcut edit/delete controls persist changes')
            rows=[{'name':'<img src=x onerror=alert(1)>','url':origin+'/one'},{'name':'Second','url':origin+'/two'}]
            saved=home(h,'home.set',{'homeShortcuts':rows,'homeWeatherCity':'City'})
            check(len(saved['homeShortcuts'])==2,'Shortcut add persists')
            check(s.evaluate(h,"document.querySelectorAll('.shortcut').length===2 && !document.querySelector('#links [onerror]')"),'Shortcut text cannot inject HTML')
            rows.reverse();rows[0]['name']='Edited';home(h,'home.set',{'homeShortcuts':rows})
            check(home(h,'home.get')['homeShortcuts'][0]['name']=='Edited','Shortcut edit and ordering')
            home(h,'home.set',{'homeShortcuts':rows[:1]})
            check(len(home(h,'home.get')['homeShortcuts'])==1,'Shortcut delete')
            check(s.evaluate(h,"new Promise(resolve=>cefQuery({request:JSON.stringify({action:'home.set',payload:{homeShortcuts:[{name:'X',url:'javascript:alert(1)'}]}}),onSuccess:()=>resolve(false),onFailure:()=>resolve(true)}))"),'Executable shortcut URL rejected')
            check(s.evaluate(h,"new Promise(resolve=>cefQuery({request:JSON.stringify({action:'browser.passwords.get'}),onSuccess:()=>resolve(false),onFailure:()=>resolve(true)}))"),'Home cannot invoke privileged shell actions')
            for theme in ('light','dark','system'):
                call('setSettings',{'theme':theme});wait(lambda:home(h,'home.get')['theme']==theme)
                check(s.evaluate(h,"['light','dark'].includes(document.body.dataset.theme)"),'Resolved '+theme+' theme')
            for key in ('homeShowLogo','homeShowSearch','homeShowShortcuts','homeShowWeather','homeShowBackground'):
                home(h,'home.set',{key:False})
            check(s.evaluate(h,"['logo','search','shortcuts','weather'].every(id=>document.getElementById(id).hidden)"),'Each home element can be disabled')
            home(h,'home.set',{key:True for key in ('homeShowLogo','homeShowSearch','homeShowShortcuts','homeShowWeather','homeShowBackground')})
            # Route via the same omnibox parser, retaining selected engine and current tab.
            home(h,'home.navigate',origin+'/search-url');wait(lambda:current_url()==origin+'/search-url')
            check(len(state()['tabs'])==1,'Home URL input navigates inside current Soulu tab')
            call('home');h=page();home(h,'home.navigate','openai.com');wait(lambda:current_url().startswith('https://openai.com'))
            check(len(state()['tabs'])==1,'Bare domain uses omnibox URL parser')
            call('home');h=page();call('setSettings',{'searchEngine':'bing'})
            home(h,'home.navigate','cef chromium architecture');wait(lambda:current_url().startswith('https://www.bing.com/search?q='))
            check(True,'Search uses selected engine')
            call('setSettings',{'startupMode':'custom','startupUrl':origin+'/startup','newTabMode':'blank','homeMode':'soulu'})
            call('newTab');wait(lambda:current_url()=='');check(True,'New Tab blank is independent of custom Startup')
            call('home');wait(lambda:current_url()=='soulu://home');check(True,'Home Soulu is independent of New Tab blank')
            call('setSettings',{'newTabMode':'custom','newTabUrl':origin+'/new','homeMode':'custom','homeUrl':origin+'/home'})
            call('newTab');wait(lambda:current_url()==origin+'/new');call('home');wait(lambda:current_url()==origin+'/home')
            check(call('getSettings')['startupUrl']==origin+'/startup','Three custom intent URLs remain independent')
            check(s.evaluate(shell,"browserShell.setSettings({newTabUrl:'file:///C:/Windows/win.ini'}).then(()=>false,()=>true)"),'Custom file URL is rejected')
            check(s.evaluate(shell,"browserShell.setSettings({startupUrl:'javascript:alert(1)'}).then(()=>false,()=>true)"),'Invalid startup URL is rejected')
            call('setSettings',{'newTabMode':'soulu','homeMode':'soulu','startupMode':'soulu'})
            before=len(state()['tabs']);s.evaluate(shell,"Promise.all(Array.from({length:8},()=>browserShell.newTab()))")
            wait(lambda:len(state()['tabs'])==before+8);check(True,'Eight rapid new tabs produce exactly eight tabs')
            wait(lambda:s.evaluate(shell,"document.activeElement?.id==='compactAddress' || document.activeElement?.id==='classicAddress'"));check(True,'New tab keeps omnibox focus')
            h=page();s.evaluate(shell,"new Promise((resolve,reject)=>cefQuery({request:JSON.stringify({action:'browser.test.pageShortcut',payload:84}),onSuccess:resolve,onFailure:reject}))")
            wait(lambda:len(state()['tabs'])==before+9);check(True,'Native Ctrl+T creates one foreground tab')
            s.evaluate(shell,"new Promise((resolve,reject)=>cefQuery({request:JSON.stringify({action:'browser.test.pageShortcut',payload:76}),onSuccess:resolve,onFailure:reject}))")
            wait(lambda:s.evaluate(shell,"['compactAddress','classicAddress'].includes(document.activeElement?.id)"));check(True,'Native Ctrl+L focuses omnibox')
            one_tab();call('setSettings',{'openStartPageAfterLastTab':True,'startupMode':'custom','startupUrl':origin+'/startup','newTabMode':'soulu'})
            call('closeTab',state()['activeTabId']);wait(lambda:len(state()['tabs'])==1 and current_url()==origin+'/startup')
            check(True,'Last-tab ON uses Startup custom and creates one tab')
            call('setSettings',{'startupMode':'soulu'});call('closeTab',state()['activeTabId']);wait(lambda:len(state()['tabs'])==1 and current_url()=='soulu://home')
            check(True,'Last-tab ON uses Startup Soulu')
            call('setSettings',{'openStartPageAfterLastTab':False});call('closeTab',state()['activeTabId']);wait(lambda:len(state()['tabs'])==1 and current_url()=='')
            check(True,'Last-tab OFF retains legacy blank replacement')
            call('home');h=page();marker='private-home-marker'
            call('newIncognito');wait(lambda:state()['incognito']);h=page();
            # Select the private target by reading its scoped state, not global active state.
            for target in s.targets():
                if '/ui/home.html' not in target.get('url',''):continue
                candidate=socket(target);connections.append(candidate)
                if home(candidate,'home.get')['incognito']:h=candidate;break
            home(h,'home.set',{'homeShortcuts':[{'name':marker,'url':origin+'/private'}],'homeWeatherCity':marker})
            check(home(h,'home.get')['incognito'],'Incognito home uses temporary state')
            call('closeTab',state()['activeTabId']);wait(lambda:not state()['incognito'])
            check(marker not in (profile/'soulu-settings.json').read_text(encoding='utf-8'),'Incognito home never writes profile settings')
            call('createProfile','Home B');b=state()['activeProfileId'];h=page()
            for target in s.targets():
                if '/ui/home.html' in target.get('url',''):
                    candidate=socket(target);connections.append(candidate)
                    if home(candidate,'home.get')['homeWeatherCity']=='':h=candidate;break
            check(home(h,'home.get')['homeShortcuts']==[],'Profile B does not inherit A shortcuts')
            home(h,'home.set',{'homeWeatherCity':'Profile B'})
            call('setSettings',{'startupMode':'blank','newTabMode':'blank','homeMode':'blank'})
            call('switchProfile','personal');call('home');h=page()
            check(call('getSettings')['newTabMode']=='soulu','Page modes are isolated by profile')
            # Save ordered ordinary URLs and an active index, excluding every incognito tab.
            one_tab();call('navigate',origin+'/first');wait(lambda:current_url()==origin+'/first')
            call('newTab');call('navigate',origin+'/second');wait(lambda:current_url()==origin+'/second')
            normal=state()['tabs'];call('setSettings',{'startupMode':'continue'})
            call('newIncognito');wait(lambda:state()['incognito']);call('navigate',origin+'/private-session')
            stop();saved=json.loads((profile/'soulu-session.json').read_text(encoding='utf-8'))
            check(saved['tabs']==[origin+'/first',origin+'/second'],'Saved session order excludes incognito')
            start();wait(lambda:len(state()['tabs'])==2)
            check([t['url'] for t in state()['tabs']]==saved['tabs'] and current_url()==origin+'/second','Startup restores ordered session without an extra home tab')
            check(call('getSettings')['homeShortcuts'][0]['name']=='Edited','Shortcuts persist after restart')
            stop()
            # Explicit startup modes are tested at full launch, rather than as Ctrl+T.
            for mode,expected in [('blank',''),('custom',origin+'/startup'),('soulu','soulu://home')]:
                data=json.loads((profile/'soulu-settings.json').read_text(encoding='utf-8'));data.update(startupMode=mode,startupUrl=origin+'/startup')
                (profile/'soulu-settings.json').write_text(json.dumps(data),encoding='utf-8')
                start();wait(lambda:current_url()==expected);check(len(state()['tabs'])==1,'Full launch startup '+mode);stop()
            report={'passed':True,'checks':checks,'count':len(checks),'weather':'provider-not-configured','session':'ordered URL restore after normal shutdown'}
            if output:output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
            print(json.dumps(report,ensure_ascii=False))
        finally:
            if process and process.poll() is None:
                try:stop()
                except Exception:process.terminate();process.wait(timeout=20)
            server.shutdown()

if __name__=='__main__':main()
