"""Real native dialog consent, private button faces and Unicode prompt roundtrip."""
import ctypes
from ctypes import wintypes as w
import http.server
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
from PIL import ImageGrab

spec=importlib.util.spec_from_file_location('storage',Path(__file__).with_name('test-cef-storage.py'))
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
os.environ['NO_PROXY']='localhost,127.0.0.1,::1'
os.environ['SOULU_REGRESSION_SKIP_FIRST_RUN']='1'
u=ctypes.windll.user32;g=ctypes.windll.gdi32
u.GetDlgItem.argtypes=[w.HWND,ctypes.c_int];u.GetDlgItem.restype=w.HWND
u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=ctypes.c_ssize_t
u.SetWindowTextW.argtypes=[w.HWND,w.LPCWSTR]
u.SetForegroundWindow.argtypes=[w.HWND]
u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
class LOGFONT(ctypes.Structure):
    _fields_=[(name,w.LONG) for name in ('height','width','escapement','orientation','weight')]+[(name,w.BYTE) for name in ('italic','underline','strikeout','charset','outprecision','clipprecision','quality','pitch')]+[('face',w.WCHAR*32)]
g.GetObjectW.argtypes=[w.HANDLE,ctypes.c_int,ctypes.c_void_p]
def wait(fn):
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        value=fn()
        if value:return value
        time.sleep(.1)
    raise AssertionError('Native typography state timeout')
def dialog_for(pid):
    found=[]
    @ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    def visit(hwnd,_):
        owner=w.DWORD();u.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        kind=ctypes.create_unicode_buffer(80);u.GetClassNameW(hwnd,kind,80)
        if owner.value==pid and kind.value=='#32770' and u.IsWindowVisible(hwnd):found.append(hwnd)
        return True
    u.EnumWindows(visit,0)
    return found[0] if found else None
def main():
    report={'passed':False,'checks':[],'nativeFonts':[]}
    out=Path(sys.argv[2]);out.parent.mkdir(parents=True,exist_ok=True)
    visuals=out.parent/'typography-visuals';visuals.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='soulu-native-type-',ignore_cleanup_errors=True) as root:
        os.environ['LOCALAPPDATA']=root
        server=http.server.ThreadingHTTPServer(('127.0.0.1',s.free_port()),s.SiteHandler)
        threading.Thread(target=server.serve_forever,daemon=True).start()
        process=s.launch(sys.argv[1]);ws=None
        try:
            ws=s.page_socket();s.navigate(ws,f'http://127.0.0.1:{server.server_port}/fixture')
            sample='Ёё Йй Жж Щщ Ыы Дд Лл Aa Gg Ii Ll Oo 0123456789 https://example.com/path?q=test'
            cases=[('confirm accept','confirm('+json.dumps(sample)+')',1,True),('confirm decline','confirm('+json.dumps(sample)+')',2,False),('alert','(alert('+json.dumps(sample)+'),true)',1,True),('prompt accept','prompt('+json.dumps(sample)+',"initial")',1,'Ответ Ёё 0123'),('prompt cancel','prompt("Cancel", "initial")',2,None),('long body','confirm('+json.dumps((sample+'\n')*100)+')',2,False),('prompt enter','prompt("Enter", "initial")',1,'Ответ Ёё 0123'),('confirm escape','confirm("Escape")',2,False)]
            for label,expression,button,expected in cases:
                s.evaluate(ws,'window.dialogFinished=false;setTimeout(()=>{window.dialogValue='+expression+';window.dialogFinished=true},50);true')
                hwnd=wait(lambda:dialog_for(process.pid))
                control=u.GetDlgItem(hwnd,button);assert control,(label,'missing result control')
                handle=u.SendMessageW(control,0x0031,0,0);font=LOGFONT()
                assert g.GetObjectW(handle,ctypes.sizeof(font),ctypes.byref(font))
                assert font.face=='Onest Medium' and font.weight==500,(label,font.face,font.weight)
                report['nativeFonts'].append({'case':label,'face':font.face,'weight':font.weight,'height':font.height})
                edit=u.GetDlgItem(hwnd,1001)
                if label in ('prompt accept','prompt enter'):
                    assert edit,'Prompt input missing'
                    value=ctypes.create_unicode_buffer(expected)
                    assert u.SendMessageW(edit,0x000C,0,ctypes.addressof(value)),'Prompt input update failed'
                if label=='long body':
                    rect=w.RECT();u.GetWindowRect(hwnd,ctypes.byref(rect));assert rect.bottom-rect.top<1500
                rect=w.RECT();u.GetWindowRect(hwnd,ctypes.byref(rect))
                time.sleep(.35)  # Capture after Windows' dialog entrance transition.
                ImageGrab.grab(bbox=(rect.left,rect.top,rect.right,rect.bottom),all_screens=True).save(visuals/('native-'+label.replace(' ','-')+'.png'))
                if label in ('prompt enter','confirm escape'):
                    key=0x0D if label=='prompt enter' else 0x1B
                    # Route normal key messages through the native dialog loop;
                    # another desktop app must not steal the test's keystrokes.
                    u.PostMessageW(edit or control,0x0100,key,1)
                    u.PostMessageW(edit or control,0x0101,key,0xC0000001)
                else:u.SendMessageW(control,0x00F5,0,0)
                wait(lambda:s.evaluate(ws,'window.dialogFinished'))
                actual=s.evaluate(ws,'window.dialogValue')
                assert actual==expected,(label,actual,expected)
                report['checks'].append(label);print('PASS:',label,flush=True)
            s.evaluate(ws,'setTimeout(()=>prompt("Close owner", "initial"),50);true')
            wait(lambda:dialog_for(process.pid))
            s.close_normally(process)
            assert process.returncode==0,'Closing a modal owner failed'
            report['checks'].append('normal shutdown with prompt open')
            print('PASS: normal shutdown with prompt open',flush=True)
            report['passed']=True
        finally:
            if ws:ws.close()
            try:
                if process.poll() is None:s.close_normally(process)
            finally:
                server.shutdown()
                out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
if __name__=='__main__':main()
