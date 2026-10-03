"""Probe the actual Soulu runtime with an isolated local HTTP fixture."""
import base64
import ctypes
import hashlib
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
import urllib.request


spec = importlib.util.spec_from_file_location('storage', Path(__file__).with_name('test-cef-storage.py'))
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)
BODY = b'Soulu CEF native download fixture\n' * 4096

class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/echo':
            key = self.headers.get('Sec-WebSocket-Key')
            if not key:
                self.send_error(400); return
            accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
            self.send_response(101)
            self.send_header('Upgrade', 'websocket')
            self.send_header('Connection', 'Upgrade')
            self.send_header('Sec-WebSocket-Accept', accept)
            self.end_headers()
            self.wfile.write(b'\x81\x04echo'); self.wfile.flush()
            time.sleep(1); return
        download = self.path.startswith('/download')
        body = BODY if download else b'''<!doctype html><title>CEF platform fixture</title>
<input id=upload type=file><canvas id=c width=128 height=128></canvas><video id=v muted></video>
<script>
window.ticks=0;let ctx=c.getContext('2d');setInterval(()=>{ticks++;ctx.fillStyle=ticks%2?'red':'blue';ctx.fillRect(0,0,128,128)},40);
v.srcObject=c.captureStream(25);
</script>'''
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream' if download else 'text/html')
        if download: self.send_header('Content-Disposition', 'attachment; filename="cef-probe.bin"')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers(); self.wfile.write(body)
    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        self.send_response(200); self.end_headers(); self.wfile.write(body)
    def log_message(self, *_): pass

def wait(fn, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = fn()
        if value: return value
        time.sleep(.1)
    raise AssertionError('Native probe timeout')

def dialog_controls(pid):
    u = ctypes.windll.user32
    u.GetWindowTextW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
    u.GetClassNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
    u.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
    u.IsWindowVisible.argtypes = [ctypes.c_void_p]
    u.GetDlgCtrlID.argtypes = [ctypes.c_void_p]
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    results = []
    @callback_type
    def child(hwnd, _):
        text = ctypes.create_unicode_buffer(2048); cls = ctypes.create_unicode_buffer(256)
        u.GetWindowTextW(hwnd, text, len(text)); u.GetClassNameW(hwnd, cls, len(cls))
        results.append((hwnd, cls.value, text.value, u.GetDlgCtrlID(hwnd)))
        return True
    @callback_type
    def window(hwnd, _):
        process_id = ctypes.c_ulong(); cls = ctypes.create_unicode_buffer(256)
        u.GetWindowThreadProcessId(hwnd, ctypes.byref(process_id)); u.GetClassNameW(hwnd, cls, len(cls))
        if process_id.value == pid and cls.value == '#32770' and u.IsWindowVisible(hwnd):
            u.EnumChildWindows(hwnd, child, 0)
        return True
    u.EnumWindows(window, 0)
    return results

def main():
    os.environ['SOULU_REGRESSION_SKIP_FIRST_RUN'] = '1'
    urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
    executable = str(Path(sys.argv[1]).resolve())
    output = Path(sys.argv[2]).resolve(); output.parent.mkdir(parents=True, exist_ok=True)
    report = {'checks': [], 'limitations': []}
    def check(value, label):
        assert value, label
        report['checks'].append(label); print('PASS:', label, flush=True)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f'http://127.0.0.1:{server.server_port}'
    with tempfile.TemporaryDirectory(prefix='soulu-cef-platform-', dir=output.parent, ignore_cleanup_errors=True) as root:
        env = dict(os.environ, LOCALAPPDATA=root, APPDATA=root, SOULU_UI_TEST_PORT=str(s.DEBUG_PORT), SOULU_REGRESSION_SKIP_FIRST_RUN='1')
        process = subprocess.Popen([executable, '--no-proxy-server'], env=env, cwd=Path(executable).parent)
        connections = []
        try:
            page = s.page_socket(); connections.append(page)
            shell_target = next(t for t in s.targets() if '/ui/index.html' in t.get('url', ''))
            shell = s.websocket.create_connection(shell_target['webSocketDebuggerUrl'], timeout=30, origin=s.BASE); connections.append(shell)
            s.navigate(page, origin + '/fixture')
            report['userAgent'] = s.evaluate(page, 'navigator.userAgent')
            report['browserVersion'] = s.command(page, 'Browser.getVersion')
            check(report['browserVersion']['product'].endswith('/154.0.8037.94'), 'DevTools reports the selected Chromium runtime')
            check('Chrome/154.' in report['userAgent'], 'Runtime User-Agent has the selected Chromium major version')
            check(s.evaluate(page, "fetch('/fixture').then(r=>r.ok)"), 'HTTP fetch succeeds')
            check(s.evaluate(page, "new Promise(r=>{let x=new XMLHttpRequest();x.open('GET','/fixture');x.onload=()=>r(x.status===200);x.send()})"), 'XHR succeeds')
            check(s.evaluate(page, "new Promise(r=>{let w=new WebSocket('ws://'+location.host+'/echo');w.onmessage=e=>{r(e.data==='echo');w.close()};w.onerror=()=>r(false)})"), 'Native WebSocket receives a server frame')
            check(s.evaluate(page, "sessionStorage.setItem('probe','ok');sessionStorage.getItem('probe')==='ok'"), 'Session storage round trip')
            report['webgl'] = s.evaluate(page, "(()=>{let gl=document.createElement('canvas').getContext('webgl2');if(!gl)return null;let ext=gl.getExtension('WEBGL_debug_renderer_info');return {version:gl.getParameter(gl.VERSION),renderer:ext?gl.getParameter(ext.UNMASKED_RENDERER_WEBGL):gl.getParameter(gl.RENDERER)}})()")
            check(report['webgl'] is not None, 'WebGL2 context initializes')
            s.command(page, 'Runtime.evaluate', {'expression': 'v.play()', 'awaitPromise': True, 'userGesture': True})
            start = s.evaluate(page, 'v.currentTime'); time.sleep(1)
            check(s.evaluate(page, 'v.currentTime') > start + .3, 'Video frames and playback time advance')
            s.evaluate(page, 'v.pause()'); paused = s.evaluate(page, 'v.currentTime'); time.sleep(.4)
            check(abs(s.evaluate(page, 'v.currentTime') - paused) < .15, 'Video pauses')
            upload = Path(root) / 'upload.txt'; upload.write_text('Soulu upload fixture', encoding='utf-8')
            document = s.command(page, 'DOM.getDocument')['root']['nodeId']
            node = s.command(page, 'DOM.querySelector', {'nodeId': document, 'selector': '#upload'})['nodeId']
            s.command(page, 'DOM.setFileInputFiles', {'nodeId': node, 'files': [str(upload)]})
            check(s.evaluate(page, "upload.files[0].text().then(t=>fetch('/upload',{method:'POST',body:t})).then(r=>r.text()).then(t=>t==='Soulu upload fixture')"), 'Selected file uploads through native fetch')
            s.evaluate(shell, "browserShell.setSiteRule({domain:'',permission:'downloads',value:0})")
            saved = Path(root) / 'saved-download.bin'
            s.command(page, 'Page.navigate', {'url': origin + '/download'})
            edit = wait(lambda: next((c for c in dialog_controls(process.pid) if c[1] == 'Edit' and c[3] == 1001), None))
            controls = dialog_controls(process.pid)
            save = next(c for c in controls if c[1] == 'Button' and c[3] == 1)
            u = ctypes.windll.user32
            u.SetWindowTextW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p]
            u.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
            u.GetAncestor.argtypes = [ctypes.c_void_p, ctypes.c_uint]
            u.GetAncestor.restype = ctypes.c_void_p
            u.SetForegroundWindow.argtypes = [ctypes.c_void_p]
            dialog = u.GetAncestor(save[0], 2)
            u.SetForegroundWindow(dialog)
            from pywinauto.controls.win32_controls import EditWrapper, ButtonWrapper
            EditWrapper(edit[0]).set_edit_text(str(saved.resolve()))
            check(EditWrapper(edit[0]).window_text() == str(saved.resolve()), 'Native Save As accepts the test path')
            ButtonWrapper(save[0]).click()
            wait(lambda: saved.exists() and saved.stat().st_size == len(BODY))
            check(saved.read_bytes() == BODY, 'Native Save As downloads exact fixture bytes to selected path')
            downloads = wait(lambda: (rows if any(r['state'] == 'completed' for r in rows) else None) if (rows := s.evaluate(shell, 'browserShell.getDownloads()')) else None)
            check(any(r['receivedBytes'] == len(BODY) for r in downloads), 'Download completion callback reaches Soulu UI')
            s.command(page, 'Page.navigate', {'url': origin + '/download-again'})
            controls = wait(lambda: dialog_controls(process.pid))
            cancel = next(c for c in controls if c[1] == 'Button' and c[3] == 2)
            u.SetForegroundWindow(u.GetAncestor(cancel[0], 2))
            report['downloadsBeforeCancel'] = s.evaluate(shell, 'browserShell.getDownloads()')
            ButtonWrapper(cancel[0]).click_input()
            check(wait(lambda: not dialog_controls(process.pid)), 'Native Cancel closes the repeated Save As dialog')
            check(wait(lambda: any(r['state'] == 'cancelled' for r in s.evaluate(shell, 'browserShell.getDownloads()'))), 'Repeated download can be cancelled from Save As')
            s.close_normally(process)
            check(process.returncode == 0, 'Normal native shutdown succeeds')
            report['limitations'] += ['Canvas captureStream media fixture does not verify YouTube codecs, audible playback, seek or fullscreen.', 'WebGL renderer is recorded; software rendering is not proof of hardware acceleration.', 'Clipboard, drag/drop, VPN credentials and real authenticated sessions need separate checks.']
        finally:
            for ws in connections:
                try: ws.close()
                except Exception: pass
            if process.poll() is None: process.kill()
            output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    server.shutdown()

if __name__ == '__main__': main()
