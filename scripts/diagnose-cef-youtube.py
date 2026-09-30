"""Observe YouTube subrequests/console/guide timing without changing browser flags."""
import importlib.util
import json
import pathlib
import sys
import os
import subprocess
import time
import urllib.parse

spec = importlib.util.spec_from_file_location('storage', pathlib.Path(__file__).with_name('test-cef-storage.py'))
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)
chrome = len(sys.argv) > 3 and sys.argv[3] == '--chrome'
if chrome:
    profile = pathlib.Path(os.environ['LOCALAPPDATA']) / 'chrome-comparison'
    process = subprocess.Popen([sys.argv[1], '--user-data-dir='+str(profile),
        '--remote-debugging-port='+str(s.DEBUG_PORT), '--remote-allow-origins='+s.BASE,
        '--no-first-run', 'about:blank'])
else:
    process = s.launch(sys.argv[1])
requests = {}
console = []
samples = []
workers = []
pending_commands = {}
started = time.monotonic()


def safe_url(url):
    # Keep request paths, never auth-bearing query strings or fragments.
    parsed = urllib.parse.urlsplit(url)
    return urllib.parse.urlunsplit((parsed.scheme, parsed.netloc, parsed.path, '', ''))


def send(ws, method, params=None, label=None):
    s.sequence += 1
    pending_commands[s.sequence] = label or method
    ws.send(json.dumps({'id': s.sequence, 'method': method, 'params': params or {}}))


try:
    ws = s.page_socket()
    for domain in ['Network', 'Runtime', 'Log', 'Page', 'ServiceWorker']:
        s.command(ws, domain + '.enable')
    started = time.monotonic()
    send(ws, 'Page.navigate', {'url': 'https://www.youtube.com/'})
    ws.settimeout(.25)
    next_sample = 0
    while time.monotonic() - started < 65:
        elapsed = time.monotonic() - started
        if elapsed >= next_sample:
            send(ws, 'Runtime.evaluate', {'expression': """(() => {
              const guide = document.querySelector('ytd-guide-renderer,yt-guide-renderer');
              const rect = guide && guide.getBoundingClientRect();
              return {url:location.origin+location.pathname,ready:document.readyState,
                guidePresent:!!guide,guideItems:guide?guide.querySelectorAll('ytd-guide-entry-renderer').length:0,
                guideVisible:!!rect&&rect.width>0&&rect.height>0,
                viewport:{width:innerWidth,height:innerHeight},
                miniGuide:!!document.querySelector('ytd-mini-guide-renderer'),
                guideElements:[...document.querySelectorAll('*')].filter(e=>e.tagName.toLowerCase().includes('guide')).map(e=>({tag:e.tagName,hidden:e.hidden,display:getComputedStyle(e).display,width:e.getBoundingClientRect().width})).slice(0,15),
                navigation:performance.getEntriesByType('navigation').map(n=>({
                  responseStart:n.responseStart,domInteractive:n.domInteractive,
                  domContentLoaded:n.domContentLoadedEventEnd,load:n.loadEventEnd})),
                resources:performance.getEntriesByType('resource').length};
            })()""", 'returnByValue': True}, f'sample:{elapsed:.1f}')
            next_sample += 5
        try:
            msg = json.loads(ws.recv())
        except s.websocket.WebSocketTimeoutException:
            continue
        method = msg.get('method', '')
        p = msg.get('params', {})
        ident = p.get('requestId')
        if method == 'Network.requestWillBeSent':
            r = p['request']
            requests[ident] = {'url':safe_url(r['url']),'type':p.get('type'),
                'start':elapsed,'timestamp':p['timestamp'],'initiator':p.get('initiator',{}).get('type')}
        elif ident in requests:
            r = requests[ident]
            if method == 'Network.responseReceived':
                response = p['response']
                r.update(status=response['status'],protocol=response.get('protocol'),
                    fromServiceWorker=response.get('fromServiceWorker',False),
                    fromDiskCache=response.get('fromDiskCache',False),timing=response.get('timing'))
            elif method in ['Network.loadingFinished','Network.loadingFailed']:
                r['duration'] = p['timestamp'] - r['timestamp']
                r['end'] = elapsed
                if method.endswith('Failed'):
                    r['error'] = p.get('errorText')
                    r['canceled'] = p.get('canceled')
        if method == 'Runtime.exceptionThrown':
            detail = p['exceptionDetails']
            console.append({'at':elapsed,'exception':detail.get('exception',{}).get('description',detail.get('text'))})
        elif method == 'Log.entryAdded':
            entry = p['entry']
            console.append({'at':elapsed,'level':entry.get('level'),'text':entry.get('text'),'url':safe_url(entry.get('url',''))})
        elif method.startswith('ServiceWorker.'):
            workers.append({'at':elapsed,'event':method,'params':p})
        if msg.get('id') in pending_commands:
            label = pending_commands.pop(msg['id'])
            if label.startswith('sample:'):
                samples.append({'at':float(label.split(':')[1]),'page':msg.get('result',{}).get('result',{}).get('value'), 'error':msg.get('error')})
    ws.settimeout(30)
    rows = list(requests.values())
    for r in rows:
        r.pop('timestamp', None)
        if 'end' not in r:
            r['pendingFor'] = time.monotonic() - started - r['start']
    report = {'source':'live CEF DevTools events, no scrolling or clicks',
        'samples':samples,'failed':[r for r in rows if 'error' in r],
        'pending':[r for r in rows if 'end' not in r],
        'slow':[r for r in rows if r.get('duration',0)>2],
        'requests':rows,'console':console,'serviceWorkers':workers}
    pathlib.Path(sys.argv[2]).write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    print(json.dumps({'requests':len(rows),'failed':len(report['failed']),
        'pending':len(report['pending']),'slow':len(report['slow']),'samples':samples}),flush=True)
    if chrome:
        s.command(ws,'Browser.close')
        process.wait(timeout=30)
    else:
        s.close_normally(process)
    ws.close()
finally:
    if process.poll() is None:
        process.kill()
