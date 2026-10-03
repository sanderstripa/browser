"""Actual CEF toolbar geometry, native movement and client-area surfaces.

CDP raster scale emulation is recorded separately from physical monitor DPI.
No authenticated profile or working VPN configuration is touched.
"""
import base64
import ctypes as C
from ctypes import wintypes as W
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
u=C.windll.user32
u.SetWindowPos.argtypes=[W.HWND,W.HWND,C.c_int,C.c_int,C.c_int,C.c_int,W.UINT]
u.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
u.SetForegroundWindow.argtypes=[W.HWND]
u.ShowWindow.argtypes=[W.HWND,C.c_int]
u.GetDpiForWindow.argtypes=[W.HWND]
u.IsZoomed.argtypes=[W.HWND]
u.IsIconic.argtypes=[W.HWND]
def wait(fn,timeout=30):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        result=fn()
        if result:return result
        time.sleep(.08)
    raise AssertionError('Toolbar state timeout')
def main():
    exe=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]);out.parent.mkdir(parents=True,exist_ok=True)
    visuals=out.parent/'toolbar-visuals';visuals.mkdir(exist_ok=True)
    report={'passed':False,'checks':[], 'matrix':[], 'limitations':['CDP emulation verifies raster scales, not moving between physical monitors.','Authenticated Google/Ozon and working VPN require the user profile.','Windows 11 Snap Layout flyout is not implemented by the existing OSR caption architecture.']}
    def check(ok,label):
        assert ok,label
        report['checks'].append(label);print('PASS:',label,flush=True)
    with tempfile.TemporaryDirectory(prefix='soulu-toolbar-',ignore_cleanup_errors=True) as temp:
        env=dict(os.environ,LOCALAPPDATA=temp,APPDATA=str(Path(temp)/'roaming'),SOULU_UI_TEST_PORT=str(s.DEBUG_PORT))
        process=subprocess.Popen([str(exe),'--no-proxy-server'],env=env)
        ws=None
        try:
            target=wait(lambda:next((t for t in s.targets() if '/ui/index.html' in t.get('url','')),None))
            ws=s.websocket.create_connection(target['webSocketDebuggerUrl'],timeout=30,origin=s.BASE)
            E=lambda js:s.evaluate(ws,js)
            def click_target(selector, edge=False):
                point=E('(()=>{const r=document.querySelector('+json.dumps(selector)+').getBoundingClientRect();return {x:r.right-'+('2' if edge else 'r.width/2')+',y:r.top+'+('4' if edge else 'r.height/2')+'}})()')
                for kind in ('mousePressed','mouseReleased'):
                    s.command(ws,'Input.dispatchMouseEvent',dict(point,type=kind,button='left',clickCount=1))
            wait(lambda:E('!!window.browserShell && document.documentElement.classList.contains("typography-ready")'))
            handles=[]
            @C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
            def visit(hwnd,_):
                pid=W.DWORD();u.GetWindowThreadProcessId(hwnd,C.byref(pid));name=C.create_unicode_buffer(128);u.GetClassNameW(hwnd,name,128)
                if pid.value==process.pid and name.value=='SouluBrowserWindow':handles.append(hwnd)
                return True
            u.EnumWindows(visit,0);hwnd=handles[0]
            def capture(name):
                E('new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))')
                shot=s.command(ws,'Page.captureScreenshot',{'format':'png'})
                (visuals/(name+'.png')).write_bytes(base64.b64decode(shot['data']))
            def bounds(selector):
                return E('(()=>{const n=document.querySelector('+json.dumps(selector)+');const r=n.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height,visible:!n.hidden,viewport:innerHeight}})()')
            def inside(selector):
                r=bounds(selector)
                return r['visible'] and r['y']>=0 and r['h']>40 and r['y']+r['h']<=r['viewport']+1
            def captions(layout):
                s.command(ws,'DOM.enable');s.command(ws,'CSS.enable')
                doc=s.command(ws,'DOM.getDocument')['root']['nodeId']
                prefix='#window' if layout=='classic' else '#compactWindow'
                for action in ('Minimize','Maximize','Close'):
                    selector=prefix+action
                    node=s.command(ws,'DOM.querySelector',{'nodeId':doc,'selector':selector})['nodeId']
                    baseline=bounds(selector)
                    colors=[]
                    for pseudo in ([],['hover'],['hover','active']):
                        s.command(ws,'CSS.forcePseudoState',{'nodeId':node,'forcedPseudoClasses':pseudo})
                        data=E('''(()=>{const n=document.querySelector('''+json.dumps(selector)+'''),r=n.getBoundingClientRect(),g=n.querySelector('svg').getBoundingClientRect(),c=getComputedStyle(n),p=getComputedStyle(n,'::before');return {w:r.width,h:r.height,bg:c.backgroundColor,pw:p.width,ph:p.height,radius:p.borderRadius,fill:p.backgroundColor,glyph:{w:g.width,h:g.height,x:g.x-r.x,y:g.y-r.y},transform:getComputedStyle(n.querySelector('svg')).transform}})()''')
                        check(data['w']==42 and data['h']==48 and data['bg'] in ('rgba(0, 0, 0, 0)','transparent'),f'{layout}/{action}/{pseudo}: full caption target transparent')
                        check(data['pw']=='30px' and data['ph']=='30px' and data['radius']=='50%' and data['transform']=='none',f'{layout}/{action}/{pseudo}: circular 30px feedback')
                        check(data['glyph']=={'w':16,'h':16,'x':13,'y':16},f'{layout}/{action}/{pseudo}: stable centered glyph')
                        colors.append(data['fill'])
                        if pseudo==['hover']:capture(layout+'-'+action+'-hover')
                    s.command(ws,'CSS.forcePseudoState',{'nodeId':node,'forcedPseudoClasses':[]})
                    check(colors[0]!=colors[1] and colors[1]!=colors[2],f'{layout}/{action}: hover and pressed fills')
            for layout in ('compact','classic','compact'):
                E('browserShell.setSettings('+json.dumps({'layout':layout,'showSidebar':True,'showBack':True,'showFavorites':True,'showNewTab':True,'showDownloads':True,'vpnToolbarVisible':True,'downloadsVisibility':'always'})+')')
                wait(lambda:E('document.body.dataset.layout')==layout)
                captions(layout)
                max_selector='#windowMaximize' if layout=='classic' else '#compactWindowMaximize'
                click_target(max_selector)
                wait(lambda:u.IsZoomed(hwnd) and E('document.querySelector('+json.dumps(max_selector)+'+" rect").getAttribute("width")==="7"'))
                check(True,layout+': maximize updates Restore glyph immediately')
                captions(layout)
                click_target(max_selector);wait(lambda:not u.IsZoomed(hwnd))
                min_selector='#windowMinimize' if layout=='classic' else '#compactWindowMinimize'
                click_target(min_selector);wait(lambda:u.IsIconic(hwnd));u.ShowWindow(hwnd,9)
                wait(lambda:not u.IsIconic(hwnd));check(True,layout+': minimize/restore native target')
                for theme in ('light','dark','system'):
                    for matte in (False,True):
                        E('browserShell.setSettings('+json.dumps({'theme':theme,'mattePanel':matte})+')')
                        errors=E('''(()=>{const host=document.querySelector('body[data-layout=classic] .classic-toolbar')||document.querySelector('.compact-toolbar');return [...host.querySelectorAll('.toolbar-button,.compact-control,.tab-action,.address-menu-button,.navigation-toolbar-button')].filter(n=>n.getClientRects().length).flatMap(n=>{const r=n.getBoundingClientRect(),g=n.querySelector('svg'),a=g?.getBoundingClientRect();return r.width!==30||r.height!==30||(a&&(a.width!==16||a.height!==16||Math.abs(a.x+a.width/2-r.x-r.width/2)>.1||Math.abs(a.y+a.height/2-r.y-r.height/2)>.1))?[{id:n.id||n.className,r:{w:r.width,h:r.height},glyph:a?{w:a.width,h:a.height}:null}]:[]})})()''')
                        check(not errors,f'{layout}/{theme}/{matte}: canonical controls '+json.dumps(errors))
                        check(E("getComputedStyle(document.querySelector(document.body.dataset.layout==='classic'?'.classic-toolbar':'.compact-toolbar')).borderBottomWidth==='0px'"),f'{layout}/{theme}/{matte}: lower divider removed')
                E('browserShell.pageMenu()');wait(lambda:inside('.site-popover'))
                capture(layout+'-site-info')
                check(inside('.site-popover'),layout+': about blank/menu bounded')
                E("document.querySelector('.site-shield').dispatchEvent(new PointerEvent('pointerdown'))")
                wait(lambda:E("document.querySelector('.site-popover').hidden"))
                # Real mouse movement through the native OSR host, not a JS call
                # to the drag bridge. Pick unoccupied current toolbar geometry.
                point=E('''(()=>{const host=document.querySelector(document.body.dataset.layout==='classic'?'.classic-main-row':'.compact-toolbar-surface');for(let y=12;y<46;y+=12)for(let x=100;x<innerWidth-160;x+=10){const n=document.elementFromPoint(x,y);if(n&&host.contains(n)&&!n.closest('button,input,select,textarea,form,a,[role="tab"],.compact-tab,.compact-tab-flow,.extension-slot,.bookmarks-bar'))return {x,y};}return null})()''')
                check(bool(point),layout+': free title region exists')
                u.SetForegroundWindow(hwnd);r=W.RECT();u.GetWindowRect(hwnd,C.byref(r));before=(r.left,r.top)
                scale=u.GetDpiForWindow(hwnd)/96
                x=r.left+round(point['x']*scale);y=r.top+round(point['y']*scale)
                u.SetCursorPos(x,y);time.sleep(.15);u.mouse_event(2,0,0,0,0);time.sleep(.2)
                u.SetCursorPos(x+65,y+45);time.sleep(.3);u.mouse_event(4,0,0,0,0);time.sleep(.2)
                u.GetWindowRect(hwnd,C.byref(r))
                check(abs(r.left-before[0])>20 or abs(r.top-before[1])>20,layout+': real window drag')
                for width in (620,1000,1280):
                    u.SetWindowPos(hwnd,None,30,30,width,700,0x14);time.sleep(.2)
                    E('browserShell.pageMenu()');wait(lambda:inside('.site-popover'))
                    check(inside('.site-popover'),f'{layout}/{width}: popup resize bounds')
                    E("document.querySelector('.site-shield').dispatchEvent(new PointerEvent('pointerdown'))")
                for scale in (1,1.25,1.5,2):
                    s.command(ws,'Emulation.setDeviceMetricsOverride',{'width':1100,'height':760,'deviceScaleFactor':scale,'mobile':False})
                    E('browserShell.pageMenu()');wait(lambda:inside('.site-popover'));capture(f'{layout}-scale-{scale}')
                    check(inside('.site-popover'),f'{layout}/{scale}: emulated raster scale popup bounds')
                    report['matrix'].append({'layout':layout,'scale':scale,'kind':'CDP emulation'})
                    E("document.querySelector('.site-shield').dispatchEvent(new PointerEvent('pointerdown'))")
                s.command(ws,'Emulation.clearDeviceMetricsOverride')
            click_target('#compactWindowClose',edge=True)
            wait(lambda:process.poll() is not None)
            check(True,'Close accepts input near the top-right target edge')
            report['passed']=True
        finally:
            if ws:
                try:ws.close()
                except Exception:pass
            if process.poll() is None:s.close_normally(process)
            out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
if __name__=='__main__':main()
