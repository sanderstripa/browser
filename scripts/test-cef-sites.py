"""Record real CEF document errors, redirects and TLS for the reported sites."""
import importlib.util
import json
import pathlib
import sys
import time

spec = importlib.util.spec_from_file_location('storage', pathlib.Path(__file__).with_name('test-cef-storage.py'))
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)
process = s.launch(sys.argv[1])
rows = []
try:
    shell_target = next(t for t in s.targets() if '/ui/index.html' in t.get('url', ''))
    shell = s.websocket.create_connection(shell_target['webSocketDebuggerUrl'], timeout=30, origin=s.BASE)
    # This network fixture must not block on interactive permission prompts.
    for permission in ['geolocation', 'camera', 'microphone', 'notifications', 'popups']:
        s.evaluate(shell, 'browserShell.setSiteRule(' + json.dumps({
            'domain': '', 'permission': permission, 'value': 2}) + ')')
    shell.close()
    ws = s.page_socket()
    ws.settimeout(1)
    s.command(ws, 'Network.enable')
    s.command(ws, 'Page.enable')
    for host in ['youtube.com', 'facebook.com', 'ozon.ru', 'google.com',
                 'sanderstripa.com', 'apps.sanderstripa.com']:
        s.sequence += 1
        ident = s.sequence
        ws.send(json.dumps({'id': ident, 'method': 'Page.navigate', 'params': {'url': 'https://' + host}}))
        row = {'host': host, 'events': []}
        document_requests = set()
        deadline = time.monotonic() + 60
        settled = None
        while time.monotonic() < deadline:
            try:
                message = json.loads(ws.recv())
            except s.websocket.WebSocketTimeoutException:
                if settled and time.monotonic() > settled + 5:
                    break
                continue
            if message.get('id') == ident:
                row['navigation'] = message.get('result', message.get('error'))
            method = message.get('method')
            params = message.get('params', {})
            request_id = params.get('requestId')
            if params.get('type') == 'Document':
                document_requests.add(request_id)
            if request_id not in document_requests:
                continue
            event = {'event': method, 'requestId': request_id}
            if method == 'Network.requestWillBeSent':
                event['url'] = params['request']['url']
                if 'redirectResponse' in params:
                    event['redirect'] = {key: params['redirectResponse'].get(key) for key in ['url', 'status', 'statusText']}
            elif method == 'Network.responseReceived':
                response = params['response']
                event['response'] = {key: response.get(key) for key in ['url', 'status', 'remoteIPAddress', 'protocol', 'securityState']}
                event['tls'] = {key: response.get('securityDetails', {}).get(key) for key in ['protocol', 'issuer', 'subjectName']}
            elif method == 'Network.loadingFailed':
                event['errorText'] = params.get('errorText')
                event['canceled'] = params.get('canceled')
                settled = time.monotonic()
            elif method == 'Network.loadingFinished':
                settled = time.monotonic()
            else:
                continue
            row['events'].append(event)
            if settled and time.monotonic() > settled + 5:
                break
        ws.settimeout(30)
        row['page'] = s.evaluate(ws, '({url:location.href,title:document.title,ready:document.readyState})')
        row['opened'] = (not row['page']['url'].startswith('chrome-error:')
                         and any(e.get('response', {}).get('status') == 200 for e in row['events'])
                         and row['page']['ready'] == 'complete')
        # A server rejection is recorded separately from a browser/network error.
        # Preview 48 already receives this Ozon challenge from CI runner egress.
        row['externalServiceRejection'] = (
            host == 'ozon.ru' and row['page']['url'].startswith('https://ozon.ru/')
            and row['page']['ready'] == 'complete'
            and any(e.get('response', {}).get('status') in (403, 429) for e in row['events']))
        rows.append(row)
        if len(sys.argv) > 2:
            pathlib.Path(sys.argv[2]).write_text(json.dumps(rows, indent=2), encoding='utf-8')
        print(json.dumps(row), flush=True)
        ws.settimeout(1)
    if len(sys.argv) > 2:
        pathlib.Path(sys.argv[2]).write_text(json.dumps(rows, indent=2), encoding='utf-8')
    ws.close()
    s.close_normally(process)
    assert all(row['opened'] or row['externalServiceRejection'] for row in rows), 'A reported site did not load; inspect recorded errors'
finally:
    if process.poll() is None:
        process.kill()
