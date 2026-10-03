"""Exercise Settings through real CEF controls and canonical disk stores."""
import base64
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

spec=importlib.util.spec_from_file_location('storage',Path(__file__).with_name('test-cef-storage.py'))
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
os.environ['NO_PROXY']='localhost,127.0.0.1,::1'
os.environ['SOULU_REGRESSION_SKIP_FIRST_RUN']='1'
checks=[]

def wait(fn,timeout=30):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        value=fn()
        if value:return value
        time.sleep(.1)
    raise AssertionError('Settings state timeout')

def check(value,name):
    assert value,name
    checks.append(name);print('PASS:',name,flush=True)

def main():
    exe=str(Path(sys.argv[1]).resolve())
    output=Path(sys.argv[2]) if len(sys.argv)>2 else Path('settings-evidence.json')
    evidence=output.parent/'settings-visuals';evidence.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='soulu-settings-',ignore_cleanup_errors=True) as temp:
        root=Path(temp);local=root/'local';roaming=root/'roaming';local.mkdir();roaming.mkdir()
        data=local/'Soulu'/'User Data';profile=data/'Profiles'/'personal';profile.mkdir(parents=True)
        legacy={'theme':'dark','layout':'classic','mattePanel':False,'matteDefaultV15':True,
          'language':'ru','showBack':False,'showNewTab':False,'showDownloads':True,
          'vpnToolbarVisible':False,'addressPosition':'left','downloadsMode':'always',
          'searchEngine':'bing','startPageMode':'blank','startPageUrl':'',
          'newTabMode':'soulu','newTabUrl':'','homeMode':'custom','homeUrl':'https://example.com/home',
          'bookmarksBarMode':'never','bookmarksIconsOnly':True,'downloadPath':str(root),
          'askDownloadLocation':False,'homeShowSearch':False,'homeShowShortcuts':True,
          'homeShortcuts':[{'name':'Legacy shortcut','url':'https://example.com/legacy'}]}
        vpn={'protocol':'vless','link':'','address':'','region':'legacy marker','lastProfileId':''}
        (data/'settings.json').write_text(json.dumps({'settings':legacy,'vpn':vpn}),encoding='utf-8')
        reader={'theme':'sepia','font':'sans','size':21,'width':2,'spacing':0,'images':False}
        (profile/'soulu-reader.json').write_text(json.dumps(reader),encoding='utf-8')
        rules={'defaults':{'camera':2,'microphone':1,'geolocation':1,'notifications':2,'sound':1,'popups':1,'downloads':0},
          'sites':{'legacy.example':{'camera':0}},'blocking':{'enabled':True,'sites':{'ads.example':False}}}
        (profile/'soulu-site-rules.json').write_text(json.dumps(rules),encoding='utf-8')
        env=dict(os.environ,LOCALAPPDATA=str(local),APPDATA=str(roaming),SOULU_UI_TEST_PORT=str(s.DEBUG_PORT))
        process=None;connections=[];shell=None;settings=None
        def socket(fragment):
            def find():
                for target in s.targets():
                    if fragment not in target.get('url',''):continue
                    ws=s.websocket.create_connection(target['webSocketDebuggerUrl'],timeout=30,origin=s.BASE)
                    if fragment=='/ui/settings.html':
                        try:
                            active=s.evaluate(shell,"window.browserShell.getState()")
                            if s.evaluate(ws,"window.souluSettings?.persisted?.profile")!=active["activeProfileId"]:
                                ws.close();continue
                        except Exception:ws.close();continue
                    connections.append(ws);return ws
                return None
            return wait(find)
        def evaluate(expression):return s.evaluate(settings,expression)
        def shellcall(method,value=None):return s.evaluate(shell,'browserShell.'+method+'('+('' if value is None else json.dumps(value))+')')
        def start():
            nonlocal process,shell,settings
            process=subprocess.Popen([exe,'--no-proxy-server'],env=env)
            shell=socket('/ui/index.html');wait(lambda:s.evaluate(shell,"typeof window.browserShell?.getState==='function'"))
            shellcall('openSettingsWindow');settings=socket('/ui/settings.html')
            wait(lambda:evaluate("document.body.classList.contains('ready')"))
        def stop():
            nonlocal process
            for ws in connections:
                try:ws.close()
                except Exception:pass
            connections.clear();s.close_normally(process);process=None
        def section(name):evaluate('souluSettings.openSection('+json.dumps(name)+')')
        def change(selector,value):
            evaluate("(()=>{const n=document.querySelector("+json.dumps(selector)+");if(!n)throw Error('Missing control');n.value="+json.dumps(value)+";n.dispatchEvent(new Event('input',{bubbles:true}));n.dispatchEvent(new Event('change',{bubbles:true}));return true;})()")
        def click(selector):evaluate('document.querySelector('+json.dumps(selector)+').click()')
        def saved():return json.loads((profile/'soulu-settings.json').read_text(encoding='utf-8'))
        def capture(name):
            result=s.command(settings,'Page.captureScreenshot',{'format':'png','captureBeyondViewport':False})
            (evidence/(name+'.png')).write_bytes(base64.b64decode(result['data']))
        try:
            start()
            check(evaluate("document.querySelector('main').dataset.view==='home' && document.querySelector('aside').hidden && document.querySelectorAll('.category-card').length===10"),'Card home has ten sections and no sidebar')
            check(evaluate("document.querySelector('#applySettings').disabled"),'Clean Apply is disabled')
            snapshot=evaluate('souluSettings.persisted')
            for key in ['theme','layout','mattePanel','showBack','showNewTab','vpnToolbarVisible','searchEngine','downloadPath','askDownloadLocation','homeMode','homeUrl','homeShortcuts']:
                check(snapshot['settings'][key]==legacy[key],'Legacy value retained: '+key)
            check(snapshot['settings']['startupMode']=='blank' and snapshot['settings']['bookmarksBarPosition']=='hidden','Existing Startup/bookmarks migrations retained')
            check(snapshot['reader']==reader and snapshot['rules']==rules and snapshot['vpn']==vpn,'Reader, policies and global VPN legacy values retained')
            capture('cards-dark')
            for name in ['general','interface','tabs','search','bookmarks','sites','profiles','downloads','vpn','updates']:
                section(name)
                check(evaluate("!document.querySelector('aside').hidden && document.querySelector('.nav-item.active').dataset.section==="+json.dumps(name)),'Section/icon opens: '+name)
            click('.nav-item');check(evaluate("document.querySelector('aside').hidden"),'All settings returns to card home')
            for query,target in [('камера','permissions-camera'),('микрофон','permissions-microphone'),('тёмная','settings-theme'),('новая вкладка','settings-newTabMode'),('домашняя','settings-homeMode'),('пароль','settings-passwords'),('Reader','reader-theme'),('закладки','settings-bookmarksBarMode'),('загрузки','settings-downloadPath')]:
                change('#settingsSearch',query)
                results=evaluate("[...document.querySelectorAll('.result')].map(n=>n.textContent)")
                check(bool(results),'Control search finds '+query)
                # Select the matching result via the same rendered click handler.
                label={'permissions-camera':'Камера','permissions-microphone':'Микрофон','settings-theme':'Тема','settings-newTabMode':'Новая вкладка','settings-homeMode':'Домашняя страница','settings-passwords':'Открыть менеджер паролей','reader-theme':'Тема чтения','settings-bookmarksBarMode':'Панель закладок','settings-downloadPath':'Папка загрузок'}[target]
                path='Вкладки и страницы' if target in ['settings-newTabMode','settings-homeMode'] else ''
                evaluate("(()=>{const found=[...document.querySelectorAll('.result')].find(n=>n.querySelector('strong').textContent==="+json.dumps(label)+" && n.textContent.includes("+json.dumps(path)+"));if(!found)throw Error('Result missing');found.click();})()")
                check(evaluate('document.getElementById('+json.dumps('control-'+target)+').classList.contains("setting-highlight")'),'Search scroll/highlight: '+query)
            change('#settingsSearch','CEF');check(evaluate("document.querySelectorAll('.result').length===1"),'Runtime components are searchable')
            change('#settingsSearch','размытие');check(evaluate("document.querySelectorAll('.result').length===1"),'Search indexes control descriptions')
            change('#settingsSearch','VPN');check(len(evaluate("[...document.querySelectorAll('.result')].filter(n=>n.textContent.includes('Интерфейс'))"))>0,'VPN search includes toolbar canonical control')
            change('#settingsSearch','zz-no-such-setting');check(evaluate("document.querySelectorAll('.result').length===0"),'Search has no false results')
            section('interface');before=saved();click('[name="settings-theme"][value="light"]')
            wait(lambda:s.evaluate(shell,"document.body.dataset.theme==='light'"))
            check(saved()==before,'Live theme preview never persists')
            check(not evaluate("document.querySelector('#applySettings').disabled"),'Change enables Apply')
            click('#mattePanel');click('[name="settings-layout"][value="compact"]')
            wait(lambda:s.evaluate(shell,"document.body.dataset.layout==='compact'"))
            matteAvailable=evaluate('souluSettings.state.capabilities.matteAvailable')
            check(evaluate('souluSettings.staged.settings.mattePanel') is (matteAvailable is not False),'Matte staging respects Windows capability')
            evaluate('souluSettings.cancel()');wait(lambda:s.evaluate(shell,"document.body.dataset.theme==='dark' && document.body.dataset.layout==='classic' && document.body.dataset.matte==='false'"))
            check(saved()==before and evaluate("document.querySelector('#applySettings').disabled"),'Cancel rolls back preview and leaves disk unchanged')
            section('tabs');change('#startupMode','custom');change('#startupUrl','javascript:alert(1)')
            check(evaluate('souluSettings.apply()') is False and saved()==before,'Invalid custom URL does not persist or discard draft')
            change('#startupUrl','https://example.com/start');change('#newTabMode','blank');change('#homeMode','custom');change('#homeUrl','https://example.com/home2')
            section('search');change('#searchEngine','google')
            section('sites');change('#permissions-camera','1');change('#reader-theme','dark');click('#blocking-contentBlocking')
            applied=evaluate('souluSettings.apply()')
            if not applied:print('Apply error:',evaluate("document.querySelector('#dataMessage').textContent"),flush=True)
            check(applied,'Multi-section Apply succeeds')
            check(saved()['startupUrl']=='https://example.com/start' and saved()['homeUrl']=='https://example.com/home2' and saved()['newTabMode']=='blank','Startup/New Tab/Home persist independently')
            check(json.loads((profile/'soulu-site-rules.json').read_text())['defaults']['camera']==1 and json.loads((profile/'soulu-reader.json').read_text())['theme']=='dark','Policy and Reader canonical stores persist')
            check(evaluate("document.querySelector('#applySettings').disabled"),'Successful Apply clears dirty state')
            # Force an existing canonical writer to fail without touching user data.
            rulesFile=profile/'soulu-site-rules.json';rulesBackup=profile/'rules.backup'
            rulesFile.rename(rulesBackup);rulesFile.mkdir()
            section('search');change('#searchEngine','yandex')
            section('sites');change('#permissions-microphone','2');change('#reader-theme','sepia')
            check(evaluate('souluSettings.apply()') is False,'Canonical writer failure reports failure')
            check(saved()['searchEngine']=='yandex' and json.loads((profile/'soulu-reader.json').read_text())['theme']=='dark','Partial Apply reports actual committed groups')
            check(evaluate('souluSettings.dirty() && souluSettings.staged.reader.theme==="sepia" && souluSettings.staged.rules.defaults.microphone===2'),'Failed Apply retains remaining editable draft')
            rulesFile.rmdir();rulesBackup.rename(rulesFile)
            check(evaluate('souluSettings.apply()'),'Apply can retry after writer recovery')
            section('search');change('#searchEngine','google');check(evaluate('souluSettings.apply()'),'Restore Profile A search after failure fixture')
            evaluate("souluSettings.staged.settings.homeShortcuts=[{name:'Unsafe',url:'javascript:alert(1)'}]")
            check(evaluate('souluSettings.apply()') is False and saved()['homeShortcuts']==legacy['homeShortcuts'],'Native shared Home validator rejects unsafe shortcuts')
            evaluate('souluSettings.cancel()')
            section('vpn');change('#vpn-link','unsupported://invalid')
            check(evaluate('souluSettings.apply()') is False and json.loads((data/'settings.json').read_text())['vpn']==vpn,'Invalid VPN key retains saved config and editable draft')
            evaluate('souluSettings.cancel()')
            section('interface');click('[name="settings-theme"][value="light"]');wait(lambda:s.evaluate(shell,"document.body.dataset.theme==='light'"))
            settingsId=shellcall('getState')['activeTabId'];shellcall('closeTab',settingsId)
            wait(lambda:evaluate("document.querySelector('#closeDialog').open"));click('#cancelClose')
            check(evaluate('souluSettings.dirty()'),'Cancel close retains draft')
            shellcall('closeTab',settingsId);wait(lambda:evaluate("document.querySelector('#closeDialog').open"));click('#saveClose')
            wait(lambda:not any(t['id']==settingsId for t in shellcall('getState')['tabs']))
            check(saved()['theme']=='light','Save on native tab close applies before closing')
            shellcall('openSettingsWindow');settings=socket('/ui/settings.html');wait(lambda:evaluate("document.body.classList.contains('ready')"))
            section('interface');click('[name="settings-theme"][value="dark"]');wait(lambda:s.evaluate(shell,"document.body.dataset.theme==='dark'"))
            settingsId=shellcall('getState')['activeTabId'];shellcall('closeTab',settingsId);wait(lambda:evaluate("document.querySelector('#closeDialog').open"));click('#discardClose')
            wait(lambda:not any(t['id']==settingsId for t in shellcall('getState')['tabs']))
            check(saved()['theme']=='light','Discard on native tab close rolls back')
            shellcall('openSettingsWindow');settings=socket('/ui/settings.html');wait(lambda:evaluate("document.body.classList.contains('ready')"))
            section('general');change('#language','en');wait(lambda:evaluate("document.documentElement.lang==='en'"))
            change('#settingsSearch','камера');check(evaluate("document.querySelector('.result strong').textContent==='Camera'"),'Search preserves bilingual synonyms after locale preview')
            evaluate('souluSettings.cancel()');section('interface');capture('section-light')
            # Chromium device metrics validate real rendered overflow and hit targets.
            for scale in [1,1.25,1.5,2]:
                s.command(settings,'Emulation.setDeviceMetricsOverride',{'width':max(360,round(1000/scale)),'height':max(400,round(740/scale)),'deviceScaleFactor':scale,'mobile':False})
                section('');capture('cards-dpi-'+str(scale))
                check(evaluate("document.documentElement.scrollWidth<=innerWidth && document.querySelector('#applySettings').getBoundingClientRect().bottom<=innerHeight"),'Responsive cards/footer at DPI '+str(scale))
                section('sites');capture('sites-dpi-'+str(scale))
                check(evaluate("document.documentElement.scrollWidth<=innerWidth && document.querySelector('aside').getBoundingClientRect().width<=64"),'Responsive section/rail at DPI '+str(scale))
            s.command(settings,'Emulation.clearDeviceMetricsOverride')
            evaluate('souluSettings.cancel()');section('interface');click('[name="settings-theme"][value="dark"]')
            wait(lambda:s.evaluate(shell,"document.body.dataset.theme==='dark'"))
            # Actual Win32 close request must keep the entire browser alive on Cancel.
            handles=[];callbackType=ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)
            @callbackType
            def findWindow(hwnd,_):
                pid=ctypes.c_ulong();ctypes.windll.user32.GetWindowThreadProcessId(hwnd,ctypes.byref(pid))
                name=ctypes.create_unicode_buffer(128);ctypes.windll.user32.GetClassNameW(hwnd,name,128)
                if pid.value==process.pid and name.value=='SouluBrowserWindow':handles.append(hwnd)
                return True
            ctypes.windll.user32.EnumWindows(findWindow,0)
            check(bool(handles),'Native Settings window host found')
            ctypes.windll.user32.PostMessageW(handles[0],0x0010,0,0)
            wait(lambda:evaluate("document.querySelector('#closeDialog').open"));click('#cancelClose')
            check(process.poll() is None and evaluate('souluSettings.dirty()'),'Cancel on whole-window close retains browser and draft')
            evaluate('souluSettings.cancel()');stop();start()
            check(evaluate('souluSettings.persisted.settings.startupUrl')=='https://example.com/start' and evaluate('souluSettings.persisted.settings.theme')=='light','Applied settings survive real restart')
            a=saved();settingsA=settings;shellcall('createProfile','Settings B');bId=shellcall('getState')['activeProfileId']
            wait(lambda:s.evaluate(settingsA,"!document.body.classList.contains('ready') && !souluSettings.persisted && !document.querySelector('#actionDialog').open"))
            check(s.evaluate(settingsA,"window.browserShell.getPasswords().then(()=>false,()=>true)"),'Background Settings cannot read another profile credentials')
            wait(lambda:not any('/ui/onboarding.html' in t.get('url','') for t in s.targets()))
            shellcall('openSettingsWindow');settings=socket('/ui/settings.html');wait(lambda:evaluate("document.body.classList.contains('ready')"))
            check(evaluate('souluSettings.persisted.profile')==bId and evaluate('souluSettings.persisted.settings.searchEngine')==legacy['searchEngine'],'Profile B keeps legacy template and does not inherit A edits')
            section('search');change('#searchEngine','duckduckgo');check(evaluate('souluSettings.apply()'),'Profile B Apply succeeds')
            shellcall('switchProfile','personal');check(saved()==a,'Profile B edits leave Profile A unchanged')
            shellcall('newIncognito');wait(lambda:shellcall('getState')['incognito']);shellcall('openSettingsWindow')
            settings=socket('/ui/settings.html');wait(lambda:evaluate("document.body.classList.contains('ready')"))
            check(not shellcall('getState')['incognito'] and evaluate('souluSettings.persisted.profile')=='personal','Opening settings from incognito uses ordinary profile safely')
            evaluate('souluSettings.cancel()');stop()
            output.parent.mkdir(parents=True,exist_ok=True)
            output.write_text(json.dumps({'passed':True,'checks':checks,'baseline':'legacy settings fixture','visuals':str(evidence)},indent=2,ensure_ascii=False),encoding='utf-8')
        finally:
            if process and process.poll() is None:
                try:
                    if settings:evaluate('souluSettings.cancel()')
                    stop()
                except Exception:process.terminate();process.wait(timeout=20)

if __name__=='__main__':main()
