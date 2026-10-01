"""Native bookmarks persistence, import, child-window overview and thumbnail smoke checks."""
import ctypes
import http.server
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time

spec = importlib.util.spec_from_file_location('storage', Path(__file__).with_name('test-cef-storage.py'))
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)

def wait(fn, timeout=20):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        result=fn()
        if result: return result
        time.sleep(.1)
    raise AssertionError('Navigation state timeout')

def windows(pid):
    result=[]
    callback=ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)
    @callback
    def visit(hwnd,_):
        value=ctypes.c_ulong()
        ctypes.windll.user32.GetWindowThreadProcessId(hwnd,ctypes.byref(value))
        if value.value==pid: result.append(hwnd)
        return True
    ctypes.windll.user32.EnumWindows(visit,0)
    return sorted(result)

def shell_socket():
    target=wait(lambda:next((t for t in s.targets() if '/ui/index.html' in t.get('url','')),None))
    return s.websocket.create_connection(target['webSocketDebuggerUrl'],timeout=30,origin=s.BASE)

with tempfile.TemporaryDirectory(prefix='soulu-navigation-', ignore_cleanup_errors=True) as isolated:
    os.environ['LOCALAPPDATA']=isolated
    home=http.server.ThreadingHTTPServer(('127.0.0.1',s.free_port()),s.SiteHandler)
    threading.Thread(target=home.serve_forever,daemon=True).start()
    home_url=f'http://127.0.0.1:{home.server_port}'
    process=s.launch(sys.argv[1])
    try:
        shell=shell_socket()
        wait(lambda:s.evaluate(shell,"typeof window.browserShell !== 'undefined'"))
        rows=[{'id':1,'type':'folder','title':'Work','parentId':0,'order':0},
              {'id':2,'type':'url','title':'Example','url':'https://example.com','parentId':1,'order':0},
              {'id':3,'type':'url','title':'Move me','url':'https://move.test','parentId':0,'order':1}]
        assert len(s.evaluate(shell,'window.browserShell.replaceBookmarks('+json.dumps(rows)+')'))==3
        saved=Path(isolated)/'Soulu'/'User Data'/'bookmarks.json'
        assert json.loads(saved.read_text(encoding='utf-8'))[1]['parentId']==1
        assert s.evaluate(shell,"window.browserShell.replaceBookmarks([{id:3,type:'folder',parentId:3}]).then(()=>false,()=>true)") is True
        assert len(s.evaluate(shell,'window.browserShell.getBookmarks()'))==3
        s.evaluate(shell,'window.browserShell.setSettings('+json.dumps({'startPageMode':'custom','startPageUrl':home_url,'bookmarksBarMode':'home'})+')')
        content=s.page_socket()
        s.navigate(content,home_url+'/')
        wait(lambda:any(t['url']==home_url+'/' for t in s.evaluate(shell,'window.browserShell.getState()')['tabs']))
        assert s.evaluate(shell,'window.browserShell.getState()')['bookmarksBarVisible'], 'Canonical home URL not recognised'
        s.navigate(content,home_url+'/away')
        wait(lambda:not s.evaluate(shell,'window.browserShell.getState()')['bookmarksBarVisible'])
        s.evaluate(shell,"window.browserShell.setSettings({bookmarksBarMode:'newTab'})")
        s.navigate(content,home_url+'/')
        wait(lambda:any(t['url']==home_url+'/' for t in s.evaluate(shell,'window.browserShell.getState()')['tabs']))
        assert not s.evaluate(shell,'window.browserShell.getState()')['bookmarksBarVisible'], 'New-tab mode also showed on home page'
        s.navigate(content,'about:blank')
        wait(lambda:s.evaluate(shell,'window.browserShell.getState()')['bookmarksBarVisible'])
        content.close()
        for layout in ('classic','compact'):
            for position in ('above','below'):
                s.evaluate(shell,'window.browserShell.setSettings('+json.dumps({'layout':layout,'bookmarksBarMode':'always','bookmarksBarPosition':position})+')')
                wait(lambda:s.evaluate(shell,"document.body.dataset.bookmarksBar === 'true'"))
                assert s.evaluate(shell,"document.querySelector('.bookmarks-bar').getBoundingClientRect().height")==28
        s.evaluate(shell,"window.browserShell.setSettings({bookmarksBarPosition:'above'})")
        s.evaluate(shell,"document.getElementById('compactSidebarButton').click()")
        wait(lambda:s.evaluate(shell,"!document.querySelector('.bookmarks-menu').hidden && document.querySelector('.bookmarks-menu').getBoundingClientRect().height > 300"))
        assert s.evaluate(shell,'window.browserShell.getState()')['sidebarVisible']
        assert s.evaluate(shell,"document.querySelector('.bookmarks-menu').getBoundingClientRect().left")==0
        assert s.evaluate(shell,"document.querySelector('.bookmarks-menu').getBoundingClientRect().bottom")==s.evaluate(shell,'innerHeight')
        assert s.evaluate(shell,"document.querySelector('.bookmark-actions').getBoundingClientRect().bottom < innerHeight"), 'Popover actions clipped to toolbar viewport'
        s.evaluate(shell,"document.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}))")
        positions=s.evaluate(shell,"(()=>{const p=id=>{const r=document.querySelector('.bookmarks-bar [data-bookmark-id=\"'+id+'\"]').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}};return {from:p(3),to:p(1)}})()")
        s.command(shell,'Input.dispatchMouseEvent',{'type':'mouseMoved',**positions['from']})
        s.command(shell,'Input.dispatchMouseEvent',{'type':'mousePressed','button':'left','clickCount':1,**positions['from']})
        for step in range(1,9):
            point={k:positions['from'][k]+(positions['to'][k]-positions['from'][k])*step/8 for k in ('x','y')}
            s.command(shell,'Input.dispatchMouseEvent',{'type':'mouseMoved','buttons':1,**point})
        s.command(shell,'Input.dispatchMouseEvent',{'type':'mouseReleased','button':'left','clickCount':1,**positions['to']})
        wait(lambda:any(r['id']==3 and r.get('parentId')==1 for r in s.evaluate(shell,'window.browserShell.getBookmarks()')))
        s.evaluate(shell,'window.browserShell.newTab()')
        wait(lambda:len(s.evaluate(shell,'window.browserShell.getState()')['tabs'])==2)
        before=windows(process.pid)
        assert s.evaluate(shell,"document.querySelector('.classic-toolbar .navigation-toolbar-button').parentElement.classList.contains('window-controls')")
        s.evaluate(shell,"document.querySelector('.compact-toolbar .navigation-toolbar-button').click()")
        wait(lambda:s.evaluate(shell,"!document.querySelector('.tab-overview').hidden"))
        assert not s.evaluate(shell,"[...document.querySelectorAll('.overview-head button')].some(b=>b.textContent==='Готово')")
        s.evaluate(shell,"document.querySelector('.compact-toolbar .navigation-toolbar-button').click()")
        wait(lambda:not s.evaluate(shell,'window.browserShell.getState()')['overviewVisible'])
        s.evaluate(shell,"document.querySelector('.compact-toolbar .navigation-toolbar-button').click()")
        wait(lambda:s.evaluate(shell,'window.browserShell.getState()')['overviewVisible'])
        assert s.evaluate(shell,"getComputedStyle(document.querySelector('.overview-grid')).display")=='grid'
        assert s.evaluate(shell,"document.querySelectorAll('.overview-card').length")==2
        assert windows(process.pid)==before, 'Overview created a top-level HWND'
        wait(lambda:any(t.get('active') and t.get('thumbnail','').startswith('data:image/jpeg;base64,') for t in s.evaluate(shell,'window.browserShell.getState()')['tabs']))
        state=s.evaluate(shell,'window.browserShell.getState()')
        s.evaluate(shell,'window.browserShell.closeTab('+str(state['tabs'][0]['id'])+')')
        wait(lambda:len(s.evaluate(shell,'window.browserShell.getState()')['tabs'])==1)
        assert s.evaluate(shell,'window.browserShell.getState()')['overviewVisible']
        s.evaluate(shell,"document.querySelector('.overview-preview').click()")
        wait(lambda:not s.evaluate(shell,'window.browserShell.getState()')['overviewVisible'])
        # Import the same Chromium file twice through the actual UI file input.
        imported=Path(isolated)/'Bookmarks.json'
        imported.write_text(json.dumps({'roots':{'bookmark_bar':{'type':'folder','name':'Imported','children':[{'type':'url','name':'Site','url':'https://import.test'}]}}}),encoding='utf-8')
        s.evaluate(shell,"window.confirm=()=>true;window.alert=()=>{}")
        for _ in range(2):
            s.evaluate(shell,"window.souluNavigation.openBookmarks()")
            wait(lambda:s.evaluate(shell,"!document.querySelector('.bookmarks-menu').hidden"))
            s.evaluate(shell,"[...document.querySelectorAll('.bookmark-actions button')].find(b=>b.textContent.startsWith('Импортировать')).click()")
            assert s.evaluate(shell,"(async()=>{const input=document.querySelector('.bookmarks-menu input[type=file]');window.__souluEmit('state',await window.browserShell.getState());return input===document.querySelector('.bookmarks-menu input[type=file]')})()"), 'State update destroyed the active import input'
            doc=s.command(shell,'DOM.getDocument')
            node=s.command(shell,'DOM.querySelector',{'nodeId':doc['root']['nodeId'],'selector':'.bookmarks-menu input[type=file]'})
            s.command(shell,'DOM.setFileInputFiles',{'nodeId':node['nodeId'],'files':[str(imported)]})
            wait(lambda:any(r.get('url')=='https://import.test' for r in s.evaluate(shell,'window.browserShell.getBookmarks()')))
            time.sleep(.3)
        final=s.evaluate(shell,'window.browserShell.getBookmarks()')
        assert sum(r.get('url')=='https://import.test' for r in final)==1
        shell.close();s.close_normally(process)
        process=s.launch(sys.argv[1]);shell=shell_socket()
        wait(lambda:s.evaluate(shell,"typeof window.browserShell !== 'undefined'"))
        restored=s.evaluate(shell,'window.browserShell.getBookmarks()')
        assert [(r['id'],r.get('parentId',0),r.get('url')) for r in restored]==[(r['id'],r.get('parentId',0),r.get('url')) for r in final]
        shell.close();s.close_normally(process)
        print('PASS: bookmarks/folders persist; invalid tree rejected; repeated import deduplicated; compact/classic bar; grid close/switch; JPEG cache; no top-level HWND.')
    finally:
        if process.poll() is None: process.kill()
        home.shutdown()
        home.server_close()
