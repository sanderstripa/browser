"""Exercise real right-click menus and their existing Soulu tab routing."""
import ctypes
import http.server
import importlib.util
import json
import pathlib
import sys
import threading
import time

spec = importlib.util.spec_from_file_location('storage', pathlib.Path(__file__).with_name('test-cef-storage.py'))
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)
u = ctypes.windll.user32
u.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
u.SendMessageW.restype = ctypes.c_ssize_t
u.GetMenuItemCount.argtypes = [ctypes.c_void_p]
u.GetMenuStringW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_wchar_p, ctypes.c_int, ctypes.c_uint]
u.SetForegroundWindow.argtypes = [ctypes.c_void_p]
server = http.server.ThreadingHTTPServer(('127.0.0.1',s.free_port()),s.SiteHandler)
threading.Thread(target=server.serve_forever,daemon=True).start()
origin = f'http://127.0.0.1:{server.server_port}'
process = s.launch(sys.argv[1])


def wait(predicate, timeout=15):
    deadline = time.monotonic()+timeout
    while time.monotonic()<deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.1)
    raise AssertionError('Expected menu/tab state did not appear')


def windows(menu=False):
    found=[]
    @ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)
    def collect(hwnd,_):
        pid=ctypes.c_ulong()
        u.GetWindowThreadProcessId(hwnd,ctypes.byref(pid))
        cls=ctypes.create_unicode_buffer(80)
        u.GetClassNameW(ctypes.c_void_p(hwnd),cls,80)
        if pid.value==process.pid and u.IsWindowVisible(ctypes.c_void_p(hwnd)) and ((cls.value=='#32768')==menu):
            found.append(hwnd)
        return True
    u.EnumWindows(collect,0)
    return found


try:
    content=s.page_socket()
    target=next(t for t in s.targets() if '/ui/index.html' in t.get('url',''))
    shell=s.websocket.create_connection(target['webSocketDebuggerUrl'],timeout=30,origin=s.BASE)
    wait(lambda:s.evaluate(shell,'typeof window.browserShell !== "undefined"'))
    def state(): return s.evaluate(shell,'window.browserShell.getState()')
    opener=state()['activeTabId']
    main=windows()
    assert len(main)==1,main
    s.navigate(content,origin+'/opener')
    s.evaluate(content,f"document.body.innerHTML='<a id=link href={origin}/link>test link</a>'")
    s.evaluate(content,"document.cookie='menu_profile=regular; Path=/'")
    history=s.command(content,'Page.getNavigationHistory')
    def choose(index):
        u.SetForegroundWindow(main[0])
        box=s.evaluate(content,"(()=>{const r=document.getElementById('link').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()")
        for kind in ['mousePressed','mouseReleased']:
            s.sequence+=1
            content.send(json.dumps({'id':s.sequence,'method':'Input.dispatchMouseEvent','params':{
                'type':kind,'x':box['x'],'y':box['y'],'button':'right','clickCount':1}}))
        hwnd=wait(lambda:next(iter(windows(True)),None))
        menu=u.SendMessageW(hwnd,0x01E1,0,0) # MN_GETHMENU
        labels=[]
        for i in range(u.GetMenuItemCount(menu)):
            buf=ctypes.create_unicode_buffer(200)
            u.GetMenuStringW(menu,i,buf,200,0x400)
            labels.append(buf.value)
        assert len(labels)==5,labels
        # Drive the actual Windows menu, rather than invoke a bridge action.
        for key in [0x24]+[0x28]*index+[0x0D]:
            u.keybd_event(key,0,0,0)
            u.keybd_event(key,0,2,0)
            time.sleep(.1)
        wait(lambda:not windows(True))
        return labels
    labels=choose(0)
    fg=wait(lambda:(v if len(v['tabs'])==2 and v['activeTabId']!=opener else None) if (v:=state()) else None)
    assert windows()==main
    s.evaluate(shell,f"window.browserShell.closeTab({fg['activeTabId']})")
    wait(lambda:len(state()['tabs'])==1)
    choose(1)
    bg=wait(lambda:(v if len(v['tabs'])==2 else None) if (v:=state()) else None)
    assert bg['activeTabId']==opener and windows()==main,bg
    bgid=next(t['id'] for t in bg['tabs'] if t['id']!=opener)
    s.evaluate(shell,f'window.browserShell.closeTab({bgid})')
    wait(lambda:len(state()['tabs'])==1)
    choose(2)
    private=wait(lambda:(v if any(t.get('incognito') for t in v['tabs']) else None) if (v:=state()) else None)
    incog=next(t for t in private['tabs'] if t.get('incognito'))
    assert windows()==main
    # Inspect the isolated browser target; no regular cookies may cross over.
    it=wait(lambda:next((t for t in s.targets() if t.get('url')==origin+'/link'),None))
    iw=s.websocket.create_connection(it['webSocketDebuggerUrl'],timeout=30,origin=s.BASE)
    assert 'menu_profile=regular' not in s.evaluate(iw,'document.cookie')
    iw.close()
    s.evaluate(shell,f"window.browserShell.closeTab({incog['id']})")
    wait(lambda:len(state()['tabs'])==1)
    choose(4)
    u.OpenClipboard.argtypes=[ctypes.c_void_p]
    u.GetClipboardData.restype=ctypes.c_void_p
    k=ctypes.windll.kernel32
    k.GlobalLock.argtypes=[ctypes.c_void_p]
    k.GlobalLock.restype=ctypes.c_void_p
    k.GlobalUnlock.argtypes=[ctypes.c_void_p]
    assert u.OpenClipboard(None)
    try:
        handle=u.GetClipboardData(13)
        ptr=k.GlobalLock(handle)
        copied=ctypes.wstring_at(ptr)
        k.GlobalUnlock(handle)
    finally:u.CloseClipboard()
    assert copied==origin+'/link',copied
    assert s.command(content,'Page.getNavigationHistory')==history
    print(json.dumps({'menu':labels,'foreground':'passed','background':'passed',
        'incognito_cookie_isolation':'passed','copy_link':'passed','top_level_windows':1,
        'save_link':'manual download dialog verification required'},ensure_ascii=False),flush=True)
    content.close();shell.close()
    s.close_normally(process)
finally:
    if process.poll() is None:process.kill()
    server.shutdown()
