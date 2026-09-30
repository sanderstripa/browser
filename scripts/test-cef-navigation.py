"""Small release smoke check for native CEF popup ownership and tab activation."""
import importlib.util
import ctypes
import json
import pathlib
import sys
import time

spec = importlib.util.spec_from_file_location('storage', pathlib.Path(__file__).with_name('test-cef-storage.py'))
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)


def wait_for(predicate):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.1)
    raise AssertionError('Tab state did not reach expected condition')


def native_windows(process):
    """Include owned popups too: none may escape Soulu's child tab host."""
    windows = []
    user32 = ctypes.windll.user32
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    user32.GetWindow.argtypes = [ctypes.c_void_p, ctypes.c_uint]
    user32.GetWindow.restype = ctypes.c_void_p

    @callback_type
    def collect(hwnd, _):
        pid = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == process.pid and user32.IsWindowVisible(hwnd):
            windows.append(hwnd)
        return True

    user32.EnumWindows(collect, 0)
    return sorted(windows)


process = s.launch(sys.argv[1])
try:
    shell_target = next(t for t in s.targets() if '/ui/index.html' in t.get('url', ''))
    shell = s.websocket.create_connection(shell_target['webSocketDebuggerUrl'], timeout=30,
                                         origin=s.BASE)
    content = s.page_socket()

    def state():
        return s.evaluate(shell, 'window.browserShell.getState()')

    wait_for(lambda: s.evaluate(shell, 'typeof window.browserShell !== "undefined"'))
    initial = state()
    main_windows = native_windows(process)
    assert len(main_windows) == 1, {'initial_native_windows': main_windows}
    opener_id = initial['activeTabId']
    s.navigate(content, 'data:text/html,<title>Soulu navigation opener</title><a id="link" href="about:blank" target="_blank">popup</a>')
    initial_url = s.evaluate(content, 'location.href')
    assert s.command(content, 'Runtime.evaluate', {
        'expression': "window.open('about:blank', '_blank') !== null",
        'userGesture': True, 'returnByValue': True
    })['result']['value'] is True
    popup_state = wait_for(lambda: (v if len(v['tabs']) == 2 and v['activeTabId'] != opener_id else None)
                           if (v := state()) else None)
    popup_id = popup_state['activeTabId']
    assert native_windows(process) == main_windows, 'window.open created another native window'
    assert any(t['id'] == opener_id for t in popup_state['tabs']), popup_state
    popup_target = next(t for t in s.targets() if t.get('type') == 'page' and t.get('url') == 'about:blank')
    popup = s.websocket.create_connection(popup_target['webSocketDebuggerUrl'], timeout=30, origin=s.BASE)
    assert s.evaluate(popup, 'window.opener !== null') is True
    s.command(popup, 'Runtime.evaluate', {'expression': 'window.close()', 'userGesture': True})
    closed = wait_for(lambda: (v if len(v['tabs']) == 1 and v['activeTabId'] == opener_id else None)
                     if (v := state()) else None)
    assert s.evaluate(content, 'location.href') == initial_url

    # An ordinary target=_blank click must use the same ownership path.
    s.command(content, 'Runtime.evaluate', {'expression': "document.getElementById('link').click()", 'userGesture': True})
    blank = wait_for(lambda: (v if len(v['tabs']) == 2 and v['activeTabId'] != opener_id else None)
                     if (v := state()) else None)
    assert native_windows(process) == main_windows, 'target=_blank created another native window'
    s.evaluate(shell, f"window.browserShell.closeTab({blank['activeTabId']})")
    wait_for(lambda: len(state()['tabs']) == 1)

    # Chromium's modifier-click path exercises the background disposition.
    s.command(content, 'Runtime.evaluate', {'expression': "document.getElementById('link').removeAttribute('target')"})
    box = s.evaluate(content, "(()=>{const r=document.getElementById('link').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()")
    for event in ['mousePressed', 'mouseReleased']:
        s.command(content, 'Input.dispatchMouseEvent', {
            'type': event, 'x': box['x'], 'y': box['y'],
            'button': 'left', 'clickCount': 1, 'modifiers': 2
        })
    background = wait_for(lambda: (v if len(v['tabs']) == 2 else None) if (v := state()) else None)
    assert background['activeTabId'] == opener_id, background
    assert native_windows(process) == main_windows, 'background tab created another native window'
    assert s.evaluate(content, 'location.href') == initial_url
    assert not any(t.get('url', '').startswith('chrome://omnibox-popup') for t in s.targets())
    print(json.dumps({'window_open': 'internal tab', 'opener': 'preserved',
                      'popup_close': 'opener retained', 'background_tab': 'active tab retained',
                      'top_level_windows': len(main_windows)}))
    content.close()
    shell.close()
    s.close_normally(process)
finally:
    if process.poll() is None:
        process.kill()
