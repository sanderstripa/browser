"""Verify Windows handler command-line links in the real app, including relaunch."""
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
os.environ.pop('SOULU_REGRESSION_SKIP_FIRST_RUN',None)
os.environ['NO_PROXY']='localhost,127.0.0.1,::1'
checks=[]
def wait(fn):
    end=time.monotonic()+30
    while time.monotonic()<end:
        value=fn()
        if value:return value
        time.sleep(.1)
    raise AssertionError('External link timeout')
def check(value,label):
    assert value,label
    checks.append(label);print('PASS:',label,flush=True)
class Site(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'<title>Soulu external-link fixture</title>'
        self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers()
        try:self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError):pass
    def log_message(self,*args):pass
def main():
    exe=str(Path(sys.argv[1]).resolve());server=http.server.ThreadingHTTPServer(('127.0.0.1',s.free_port()),Site)
    threading.Thread(target=server.serve_forever,daemon=True).start();origin=f'http://127.0.0.1:{server.server_port}'
    with tempfile.TemporaryDirectory(prefix='soulu-default-links-',ignore_cleanup_errors=True) as root:
        env=dict(os.environ,LOCALAPPDATA=root,APPDATA=root,SOULU_UI_TEST_PORT=str(s.DEBUG_PORT))
        process=subprocess.Popen([exe,origin+'/initial'],env=env);sockets=[]
        def socket(fragment):
            target=wait(lambda:next((t for t in s.targets() if fragment in t.get('url','')),None))
            ws=s.websocket.create_connection(target['webSocketDebuggerUrl'],timeout=30,origin=s.BASE);sockets.append(ws);return ws
        try:
            shell=socket('/ui/index.html');wait(lambda:s.evaluate(shell,"typeof window.browserShell?.getState==='function'"))
            page=socket('/ui/onboarding.html');wait(lambda:s.evaluate(page,"document.querySelector('main')?.getAttribute('aria-busy')==='false'"))
            check(len(s.evaluate(shell,'browserShell.getState()')['tabs'])==1,'External cold launch preserves new-profile onboarding')
            s.command(page,'Runtime.evaluate',{'expression':"cefQuery({request:JSON.stringify({action:'onboarding.finish',payload:{skip:true}}),onSuccess:()=>{},onFailure:()=>{}})"})
            wait(lambda:any(t.get('url')==origin+'/initial' and t.get('active') for t in s.evaluate(shell,'browserShell.getState()')['tabs']))
            check(len(s.evaluate(shell,'browserShell.getState()')['tabs'])==1,'First-run completion opens pending external URL in the same tab')
            second=subprocess.Popen([exe,origin+'/relaunch'],env=env);second.wait(timeout=30)
            wait(lambda:any(t.get('url')==origin+'/relaunch' and t.get('active') for t in s.evaluate(shell,'browserShell.getState()')['tabs']))
            check(len(s.evaluate(shell,'browserShell.getState()')['tabs'])==2,'Running-app relaunch opens the HTTP link in one foreground tab')
            second=subprocess.Popen([exe,'file:///C:/Windows/win.ini'],env=env);second.wait(timeout=30);time.sleep(.2)
            check(len(s.evaluate(shell,'browserShell.getState()')['tabs'])==2,'External handler rejects file URLs')
            s.evaluate(shell,'browserShell.newIncognito()');wait(lambda:s.evaluate(shell,'browserShell.getState()')['incognito'])
            second=subprocess.Popen([exe,origin+'/normal'],env=env);second.wait(timeout=30)
            wait(lambda:any(t.get('url')==origin+'/normal' and t.get('active') for t in s.evaluate(shell,'browserShell.getState()')['tabs']))
            check(not s.evaluate(shell,'browserShell.getState()')['incognito'],'External handler returns to a normal profile from incognito')
        finally:
            for ws in sockets:
                try:ws.close()
                except Exception:pass
            s.close_normally(process);server.shutdown()
    if len(sys.argv)>2:
        output=Path(sys.argv[2]);output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps({'passed':True,'checks':checks},indent=2))
if __name__=='__main__':main()
