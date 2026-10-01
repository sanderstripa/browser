"""Native permission state and actual request-filter checks on controlled sites."""
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

def wait(fn,timeout=20):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        value=fn()
        if value:return value
        time.sleep(.1)
    raise AssertionError('Policy state timeout')

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith('/ad.js'):
            body=b'window.adLoaded=true';kind='text/javascript'
        elif self.path.startswith('/site.js'):
            body=b'window.normalLoaded=true';kind='text/javascript'
        else:
            body=(f'<!doctype html><title>Policy smoke</title><script src="/site.js"></script>'
                  f'<script src="http://ads.doubleclick.net:{self.server.server_port}/ad.js"></script>').encode();kind='text/html'
        self.send_response(200);self.send_header('Content-Type',kind);self.send_header('Cache-Control','no-store')
        self.send_header('Access-Control-Allow-Origin','*');self.send_header('Content-Length',str(len(body)))
        self.end_headers();self.wfile.write(body)
    def log_message(self,*args):pass

with tempfile.TemporaryDirectory(prefix='soulu-policy-',ignore_cleanup_errors=True) as root:
    os.environ['LOCALAPPDATA']=root
    server=http.server.ThreadingHTTPServer(('127.0.0.1',s.free_port()),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    origin=f'http://127.0.0.1:{server.server_port}'
    env=dict(os.environ,SOULU_UI_TEST_PORT=str(s.DEBUG_PORT))
    # This changes resolution only in this disposable Soulu test process.
    process=subprocess.Popen([sys.argv[1],'--no-proxy-server',
        '--host-resolver-rules=MAP ads.doubleclick.net 127.0.0.1'],env=env)
    try:
        shell_target=wait(lambda:next((t for t in s.targets() if '/ui/index.html' in t.get('url','')),None))
        shell=s.websocket.create_connection(shell_target['webSocketDebuggerUrl'],timeout=30,origin=s.BASE)
        wait(lambda:s.evaluate(shell,"typeof window.browserShell==='object'"))
        content=s.page_socket()
        def blocking(domain,value):return s.evaluate(shell,'window.browserShell.setContentBlocking('+json.dumps({'domain':domain,'value':value})+')')
        s.navigate(content,origin+'/off');assert s.evaluate(content,'window.adLoaded===true&&window.normalLoaded===true')
        blocking('',1);s.navigate(content,origin+'/on')
        assert s.evaluate(content,'window.adLoaded===undefined&&window.normalLoaded===true'),'Ad script was not filtered'
        blocking('127.0.0.1',0);s.navigate(content,origin+'/allow')
        assert s.evaluate(content,'window.adLoaded===true'),'Per-site allow did not override global block'
        blocking('',0);blocking('127.0.0.1',1);s.navigate(content,origin+'/block')
        assert s.evaluate(content,'window.adLoaded===undefined'),'Per-site block did not override global allow'
        assert s.evaluate(content,f"fetch('http://ads.doubleclick.net:{server.server_port}/ad.js').then(r=>r.ok)"),'XHR was accidentally blocked'
        for name in ('camera','microphone','geolocation','notifications'):
            s.evaluate(shell,'window.browserShell.setSiteRule('+json.dumps({'domain':'','permission':name,'value':2})+')')
            state=s.evaluate(content,"navigator.permissions.query({name:"+json.dumps(name)+"}).then(p=>p.state)")
            assert state=='denied',f'{name} global deny did not reach Chromium'
            s.evaluate(shell,'window.browserShell.setSiteRule('+json.dumps({'domain':'127.0.0.1','permission':name,'value':0})+')')
            state=s.evaluate(content,"navigator.permissions.query({name:"+json.dumps(name)+"}).then(p=>p.state)")
            assert state=='granted',f'{name} domain allow did not override default'
        s.navigate(content,f'http://ads.doubleclick.net:{server.server_port}/document')
        assert s.evaluate(content,'document.title')=='Policy smoke','Top-level navigation was filtered'
        content.close();shell.close();s.close_normally(process)
        print(json.dumps({'native_permission_defaults_and_overrides':True,'request_filter_actual':True,
                          'global_per_site_blocking':True,'xhr_and_document_exemptions':True}))
    finally:
        if process.poll() is None:process.kill();process.wait()
        server.shutdown()
