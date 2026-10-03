"""Actual CEF font selection and visual matrix on a private test profile."""
import base64
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

def wait(fn):
    until=time.monotonic()+40
    while time.monotonic()<until:
        result=fn()
        if result:return result
        time.sleep(.15)
    raise AssertionError('Typography state timeout')

def main():
    exe=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]);out.parent.mkdir(parents=True,exist_ok=True)
    visuals=out.parent/'typography-visuals';visuals.mkdir(exist_ok=True)
    report={'passed':False,'checks':[], 'matrix':[], 'platformFonts':[], 'limitations':['CDP scale emulation does not replace physical Windows monitor DPI testing.','Authenticated external sessions and VPN need credentials/service access.']}
    def check(value,label):
        assert value,label
        report['checks'].append(label);print('PASS:',label,flush=True)
    sockets=[]
    with tempfile.TemporaryDirectory(prefix='soulu-typography-',ignore_cleanup_errors=True) as temp:
        env=dict(os.environ,LOCALAPPDATA=temp,APPDATA=str(Path(temp)/'roaming'),SOULU_UI_TEST_PORT=str(s.DEBUG_PORT))
        process=subprocess.Popen([str(exe),'--no-proxy-server'],env=env)
        def connect(fragment):
            target=wait(lambda:next((t for t in s.targets() if fragment in t.get('url','')),None))
            ws=s.websocket.create_connection(target['webSocketDebuggerUrl'],timeout=30,origin=s.BASE);sockets.append(ws)
            wait(lambda:s.evaluate(ws,'document.documentElement?.classList.contains("typography-ready")'))
            return ws
        def audit(ws,name):
            check(s.evaluate(ws,'document.fonts.check("400 14px Onest") && document.fonts.check("500 14px Onest") && document.fonts.check("600 14px Onest")'),name+': all real faces loaded')
            bad=s.evaluate(ws,'''[...document.querySelectorAll('body *')].filter(n=>n.children.length===0&&n.textContent.trim()&&!n.closest('.reader-article')&&n.getBoundingClientRect().width&&getComputedStyle(n).visibility==='visible').map(n=>({tag:n.tagName,font:getComputedStyle(n).fontFamily,weight:getComputedStyle(n).fontWeight,text:n.textContent.slice(0,70)})).filter(n=>!n.font.startsWith('Onest')||!["400","500","600"].includes(n.weight))''')
            check(not bad,name+': text family and weights '+json.dumps(bad,ensure_ascii=False))
            check(s.evaluate(ws,"performance.getEntriesByType('resource').filter(r=>r.name.includes('Onest-')).every(r=>r.name.startsWith('file:'))"),name+': offline bundled resources')
        def actual_face(ws,selector,label):
            s.command(ws,'DOM.enable');s.command(ws,'CSS.enable')
            doc=s.command(ws,'DOM.getDocument')['root']['nodeId']
            node=s.command(ws,'DOM.querySelector',{'nodeId':doc,'selector':selector})['nodeId']
            fonts=s.command(ws,'CSS.getPlatformFontsForNode',{'nodeId':node})['fonts']
            report['platformFonts'].append({'surface':label,'fonts':fonts})
            check(any(f['familyName'].startswith('Onest') and f['isCustomFont'] and f['glyphCount'] for f in fonts),label+': actual bundled glyph rasterization')
        def capture(ws,name):
            shot=s.command(ws,'Page.captureScreenshot',{'format':'png'})
            (visuals/(name+'.png')).write_bytes(base64.b64decode(shot['data']))
        try:
            shell=connect('/ui/index.html');home=connect('/ui/home.html')
            wait(lambda:s.evaluate(shell,'typeof browserShell?.getState==="function"'))
            audit(shell,'browser chrome');audit(home,'Home');actual_face(home,'#logo','Home wordmark');capture(home,'home')
            s.evaluate(shell,'document.querySelector("#compactVpnButton").dispatchEvent(new PointerEvent("pointerover",{bubbles:true}))')
            wait(lambda:s.evaluate(shell,'!document.querySelector(".soulu-type-tooltip").hidden'))
            actual_face(shell,'.soulu-type-tooltip','Toolbar title tooltip');capture(shell,'toolbar-tooltip')
            s.evaluate(shell,'document.dispatchEvent(new PointerEvent("pointerdown",{bubbles:true}))')
            s.evaluate(shell,'browserShell.openSettingsWindow()');settings=connect('/ui/settings.html')
            wait(lambda:s.evaluate(settings,'document.body.classList.contains("ready")'))
            actual_face(settings,'#settingsTitle','Settings title')
            for lang in ('ru','en'):
                s.evaluate(settings,"(()=>{souluSettings.openSection('general');const n=document.querySelector('#language');n.value="+json.dumps(lang)+";n.dispatchEvent(new Event('change',{bubbles:true}));})()")
                for theme in ('light','dark','system'):
                    s.evaluate(settings,"(()=>{souluSettings.openSection('interface');const n=document.querySelector('[name=settings-theme][value="+theme+"]');n.click();})()")
                    for scale in (1,1.25,1.5,2):
                        s.command(settings,'Emulation.setDeviceMetricsOverride',{'width':1000,'height':760,'deviceScaleFactor':scale,'mobile':False})
                        s.evaluate(settings,"souluSettings.openSection('')")
                        wait(lambda:s.evaluate(settings,'!window.souluMotion?.activeCount'))
                        audit(settings,f'Settings {lang}/{theme}/{scale}')
                        check(s.evaluate(settings,'document.documentElement.scrollWidth<=innerWidth'),f'No horizontal page overflow {lang}/{theme}/{scale}')
                        name=f'settings-{lang}-{theme}-{scale}';capture(settings,name)
                        report['matrix'].append({'language':lang,'theme':theme,'scale':scale,'screenshot':name+'.png'})
                sections=s.evaluate(settings,'[...document.querySelectorAll("#homeCards [data-section]")].map(n=>n.dataset.section)')
                for section in sections:
                    s.evaluate(settings,'souluSettings.openSection('+json.dumps(section)+')')
                    wait(lambda:s.evaluate(settings,'!window.souluMotion?.activeCount'))
                    audit(settings,'Settings '+lang+'/'+section);capture(settings,'section-'+lang+'-'+section)
            s.evaluate(settings,'souluSettings.cancel()')
            s.evaluate(shell,"browserShell.openHistory()")
            history=connect('/ui/history.html');audit(history,'History');actual_face(history,'h1','History title');capture(history,'history')
            # File-origin iframes are isolated by CEF; inspect their own execution
            # context through CDP rather than weakening file-origin security.
            tree=s.command(shell,'Page.getFrameTree')['frameTree']
            frame=next(f['frame'] for f in tree.get('childFrames',[]) if '/vpn/popup.html' in f['frame']['url'])
            context=s.command(shell,'Page.createIsolatedWorld',{'frameId':frame['id'],'worldName':'soulu-typography-test'})['executionContextId']
            result=s.command(shell,'Runtime.evaluate',{'contextId':context,'expression':'document.documentElement.classList.contains("typography-ready") && document.fonts.check("500 14px Onest")','returnByValue':True})
            check(result['result'].get('value'),'VPN iframe bundled face')
            s.evaluate(shell,'document.querySelector("#compactVpnButton").click()');time.sleep(.4);capture(shell,'vpn-panel')
            report['passed']=True
        finally:
            for ws in sockets:
                try:ws.close()
                except Exception:pass
            if process.poll() is None:s.close_normally(process)
            out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')

if __name__=='__main__':main()
