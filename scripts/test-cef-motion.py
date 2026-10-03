"""Motion assertions against the actual Windows executable, without a DOM mock."""
import base64
import importlib.util
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time

spec=importlib.util.spec_from_file_location('overlay',Path(__file__).with_name('test-cef-settings-overlay.py'))
o=importlib.util.module_from_spec(spec);spec.loader.exec_module(o)
s=o.s
os.environ['SOULU_REGRESSION_SKIP_FIRST_RUN']='1'
os.environ['NO_PROXY']='localhost,127.0.0.1,::1'

def main():
    exe=str(Path(sys.argv[1]).resolve());output=Path(sys.argv[2])
    visuals=output.parent/'motion-visuals';visuals.mkdir(parents=True,exist_ok=True)
    report={'passed':False,'checks':[],'deviceScales':[], 'limitations':[
        'Device scale emulation is not physical Windows DPI switching.',
        'CDP screenshots do not prove absence of every composed HWND artifact.',
        'CPU/GPU and frame pacing on a shared CI runner are diagnostic, not a hardware benchmark.']}
    def check(value,name):
        assert value,name
        report['checks'].append(name);print('PASS:',name,flush=True)
    with tempfile.TemporaryDirectory(prefix='soulu-motion-',ignore_cleanup_errors=True) as temp:
        env=dict(os.environ,LOCALAPPDATA=temp,SOULU_UI_TEST_PORT=str(s.DEBUG_PORT))
        process=subprocess.Popen([exe,'--no-proxy-server'],env=env);sockets=[]
        def connect(fragment):
            target=o.wait(lambda:next((t for t in s.targets() if fragment in t.get('url','')),None))
            ws=s.websocket.create_connection(target['webSocketDebuggerUrl'],timeout=30,origin=s.BASE);sockets.append(ws);return ws
        def evaluate(expression):return s.evaluate(settings,expression)
        def settled():o.wait(lambda:evaluate('souluMotion.activeCount===0 && !document.querySelector(".motion-shared,.motion-source-hidden")'))
        def click(selector):evaluate('document.querySelector('+json.dumps(selector)+').click()')
        def capture(name):
            shot=s.command(settings,'Page.captureScreenshot',{'format':'png'})
            (visuals/(name+'.png')).write_bytes(base64.b64decode(shot['data']))
        def card(key):return '#homeCards [data-section="'+key+'"]'
        try:
            shell=connect('/ui/index.html')
            o.wait(lambda:s.evaluate(shell,'typeof browserShell?.getState==="function"'))
            window=o.wait(lambda:next(iter(o.windows(process.pid,'SouluBrowserWindow')),None))
            # Class name can vary across shell revisions; discover the owner via overlay below.
        except AssertionError:
            window=None
        try:
            s.evaluate(shell,'browserShell.openSettingsWindow()');settings=connect('/ui/settings.html')
            o.wait(lambda:evaluate('document.body.classList.contains("ready")'))
            host=o.wait(lambda:next(iter(o.windows(process.pid,'SouluSettingsOverlay')),None))
            if not window:window=o.u.GetWindow(host,4)  # GW_OWNER
            keys=evaluate('[...document.querySelectorAll("#homeCards [data-section]")].map(n=>n.dataset.section)')
            check(len(keys)==10,'Ten Settings categories preserved')
            for key in keys:
                evaluate('document.querySelector('+json.dumps(card(key))+').scrollIntoView({block:"center"})')
                click(card(key))
                check(evaluate('!!document.querySelector(".motion-shared") && document.querySelector(".motion-shared").inert && document.querySelector(".motion-shared").getAttribute("aria-hidden")==="true"'),key+': shared container is visual only')
                settled()
                check(evaluate('document.querySelector("main").dataset.view==="section" && document.querySelector(".nav-item.active").dataset.section==='+json.dumps(key)),key+': correct destination')
                check(evaluate('document.activeElement.id==="settingsContent"'),key+': section focus')
                if key=='interface':capture('interface-final')
                click('#sectionNav button:first-child');settled()
                check(evaluate('document.querySelector("main").dataset.view==="home" && document.activeElement.dataset.section==='+json.dumps(key)),key+': reverse and card focus')
            # No source exists when search or internal links directly choose a section.
            evaluate('souluSettings.openSection("sites","permissions-camera")')
            check(evaluate('!document.querySelector(".motion-shared")'),'Deep link has no fabricated card');settled()
            click('#sectionNav button:first-child');settled()
            evaluate('settingsSearch.value="camera";settingsSearch.dispatchEvent(new Event("input"))')
            click('#searchResults .result')
            check(evaluate('!document.querySelector(".motion-shared")'),'Search uses section reveal');settled()
            # Native keyboard events exercise semantic button activation and back.
            click('#sectionNav button:first-child');settled()
            evaluate('document.querySelector('+json.dumps(card('interface'))+').focus()')
            for params in [{'type':'keyDown','key':'Enter','code':'Enter','windowsVirtualKeyCode':13},{'type':'keyUp','key':'Enter','code':'Enter','windowsVirtualKeyCode':13}]:s.command(settings,'Input.dispatchKeyEvent',params)
            settled();check(evaluate('document.querySelector("main").dataset.view==="section"'),'Enter activates card')
            s.command(settings,'Input.dispatchKeyEvent',{'type':'keyDown','key':'ArrowLeft','code':'ArrowLeft','windowsVirtualKeyCode':37,'modifiers':1})
            settled();check(evaluate('document.querySelector("main").dataset.view==="home"'),'Alt Left returns through same transition')
            for theme in ['light','dark','system']:
                for scale in [1,1.25,1.5,1.75,2]:
                    s.command(settings,'Emulation.setDeviceMetricsOverride',{'width':900,'height':740,'deviceScaleFactor':scale,'mobile':False})
                    evaluate('souluSettings.staged.settings.theme='+json.dumps(theme)+';document.body.dataset.theme='+json.dumps(theme))
                    click(card('interface'));settled()
                    check(evaluate('document.querySelector(".nav-item.active").getBoundingClientRect().width>0 && !document.querySelector(".motion-shared")'),f'{theme} scale {scale}: geometry and cleanup')
                    click('#sectionNav button:first-child');settled()
                    report['deviceScales'].append({'theme':theme,'scale':scale})
            s.command(settings,'Emulation.clearDeviceMetricsOverride')
            for cycle in range(30):
                evaluate('souluSettings.openSection("interface");souluSettings.openSection("");souluSettings.openSection("tabs");souluSettings.openSection("")')
                settled()
            check(evaluate('document.querySelector("main").dataset.view==="home"'),'Rapid repeated input settles to last intent')
            click(card('interface'))
            o.u.MoveWindow(window,70,60,980,680,True);settled()
            check(evaluate('!document.querySelector(".motion-shared")'),'Native resize interrupts cleanly')
            for show in [3,9]:
                o.u.ShowWindow(window,show);settled()
            click('#sectionNav button:first-child');settled()
            click(card('interface'))
            evaluate('document.body.dataset.theme="dark";document.querySelector("#control-settings-theme input")?.dispatchEvent(new Event("change"))')
            settled();check(evaluate('!document.querySelector(".motion-shared")'),'Theme interruption leaves no old layer')
            click('#sectionNav button:first-child');settled()
            s.command(settings,'Emulation.setEmulatedMedia',{'features':[{'name':'prefers-reduced-motion','value':'reduce'}]})
            click(card('interface'))
            check(evaluate('souluMotion.reduced && !document.querySelector(".motion-shared")'),'Reduced motion removes spatial morph');settled()
            click('#sectionNav button:first-child');settled()
            s.command(settings,'Emulation.setEmulatedMedia',{'features':[]})
            # Record monotonic rAF pacing and real transition duration over repeated cycles.
            samples=evaluate('''(async()=>{const cycles=[];for(let i=0;i<10;i++){const frames=[];let run=true,last=performance.now();function frame(now){frames.push(now-last);last=now;if(run)requestAnimationFrame(frame)}requestAnimationFrame(frame);const start=performance.now();souluSettings.openSection("interface");while(souluMotion.activeCount)await new Promise(r=>setTimeout(r,10));run=false;cycles.push({elapsed:performance.now()-start,frames});souluSettings.openSection("");while(souluMotion.activeCount)await new Promise(r=>setTimeout(r,10));}return cycles})()''')
            report['timing']=samples
            intervals=[n for cycle in samples for n in cycle['frames'] if n>0]
            report['frameIntervalsMs']={'median':statistics.median(intervals),'max':max(intervals)}
            check(all(cycle['elapsed']<1500 for cycle in samples),'Repeated motion remains responsive within runner allowance')
            # Close during an in-flight internal transition, then reopen canonical Settings.
            evaluate('souluSettings.cancel()')
            click(card('interface'));evaluate('souluSettingsRequestClose()')
            o.wait(lambda:not o.windows(process.pid,'SouluSettingsOverlay'))
            # Tab sidebar is an overlay: webpage geometry must stay unchanged.
            for layout in ['compact','classic']:
                s.evaluate(shell,'browserShell.setSettings({layout:'+json.dumps(layout)+'})')
                for cycle in range(30):
                    s.evaluate(shell,'browserShell.toggleSidebar()');time.sleep(.025)
                    s.evaluate(shell,'browserShell.toggleSidebar()')
                time.sleep(.35)
                check(s.evaluate(shell,'browserShell.getState().then(s=>!s.sidebarVisible)'),'Sidebar repeated close: '+layout)
            s.evaluate(shell,'browserShell.toggleSidebar()');time.sleep(.3)
            check(s.evaluate(shell,'document.querySelector("#sidebar").classList.contains("visible") && !document.querySelector("#sidebar").inert'),'Tab sidebar visible and interactive')
            s.evaluate(shell,'browserShell.toggleSidebar()')
            check(s.evaluate(shell,'document.querySelector("#sidebar").inert'),'Closing sidebar immediately releases keyboard targets')
            time.sleep(.35)
            check(s.evaluate(shell,'getComputedStyle(document.querySelector("#sidebar")).visibility==="hidden"'),'Sidebar exits before OSR host shrinks')
            report['passed']=True
        finally:
            for ws in sockets:
                try:ws.close()
                except Exception:pass
            if process.poll() is None:s.close_normally(process)
            output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')

if __name__=='__main__':main()
