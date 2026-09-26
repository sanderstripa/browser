"""Exercise real CEF settings-page events and inspect the separate shell browser."""
import json
import os
import time
import urllib.request
import websocket

PORT = int(os.environ.get("SOULU_UI_TEST_PORT", "9223"))
BASE = f"http://127.0.0.1:{PORT}"
seq = 0

def targets():
    with urllib.request.urlopen(BASE + "/json/list", timeout=3) as response:
        return json.load(response)

def find_target(fragment, timeout=45):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            for target in targets():
                if fragment in target.get("url", "") and target.get("type") == "page":
                    return websocket.create_connection(
                        target["webSocketDebuggerUrl"], timeout=8,
                        origin=f"http://127.0.0.1:{PORT}")
        except (OSError, KeyError):
            pass
        time.sleep(.25)
    raise AssertionError(f"CEF target {fragment!r} did not appear")

def evaluate(ws, expression):
    global seq
    seq += 1
    ident = seq
    ws.send(json.dumps({"id": ident, "method": "Runtime.evaluate",
                        "params": {"expression": expression,
                                   "returnByValue": True, "awaitPromise": True}}))
    while True:
        response = json.loads(ws.recv())
        if response.get("id") != ident:
            continue
        if "error" in response:
            raise AssertionError(response["error"])
        result = response["result"]
        if "exceptionDetails" in result:
            raise AssertionError(result["exceptionDetails"])
        return result["result"].get("value")

def wait_for(predicate, label, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except (OSError, KeyError):
            pass
        time.sleep(.25)
    raise AssertionError(f"UI state not reached: {label}")

shell = find_target("/ui/index.html")
settings = None
try:
    assert wait_for(lambda: evaluate(shell, "Boolean(window.browserShell && document.querySelector('.browser-toolbar'))"), "shell ready")
    evaluate(shell, "window.browserShell.openSettingsWindow()")
    settings = find_target("/ui/settings.html")
    assert wait_for(lambda: evaluate(settings, "document.body.classList.contains('ready')"), "settings ready")
    # These are real change/click handlers from the settings page, not a
    # direct write to settings.json or a synthetic state injected into shell.
    evaluate(settings, """(() => {
      const theme = document.querySelector('#theme');
      theme.value = 'dark';
      theme.dispatchEvent(new Event('change', {bubbles:true}));
      return true;
    })()""")
    wait_for(lambda: evaluate(shell, "document.body.dataset.theme === 'dark'"), "dark shell theme")
    evaluate(settings, """(() => {
      const checkbox = document.querySelector('#mattePanel');
      if (!checkbox.checked) checkbox.click();
      return true;
    })()""")
    wait_for(lambda: evaluate(shell, "document.body.dataset.matte === 'true'"), "matte shell")
    for key in ("vpnToolbarVisible", "showSidebar", "showBack",
                "showFavorites", "showNewTab", "showDownloads"):
        evaluate(settings, f"""(() => {{
          const control = document.querySelector('[data-setting="{key}"]');
          if (!control) throw new Error('Missing setting {key}');
          if (control.checked) control.click();
          return true;
        }})()""")
        wait_for(lambda key=key: evaluate(shell, f"document.body.dataset.{ 'showVpn' if key == 'vpnToolbarVisible' else key } === 'false'"), f"{key} hidden")
    styles = evaluate(shell, """(() => {
      const pick = selector => {
        const element = document.querySelector(selector);
        return element ? getComputedStyle(element).display : 'absent';
      };
      return {
        theme: document.body.dataset.theme,
        matte: document.body.dataset.matte,
        background: getComputedStyle(document.querySelector('.compact-toolbar')).backgroundImage,
        vpn: pick('#compactVpnButton'), sidebar: pick('#compactSidebarButton'),
        back: pick('#compactBackButton'), favorite: pick('.compact-active-tab [data-favorites]'),
        newTab: pick('#compactNewTabButton'), downloads: pick('#compactDownloadsButton')
      };
    })()""")
    print("CEF shell after settings-page interaction:", json.dumps(styles, ensure_ascii=False))
    assert styles["theme"] == "dark" and styles["matte"] == "true"
    assert "gradient" in styles["background"].lower()
    for key in ("vpn", "sidebar", "back", "favorite", "newTab", "downloads"):
        assert styles[key] == "none", f"{key} did not hide: {styles[key]}"
    # Check the reverse direction too: controls must reappear, not merely hide.
    for key, selector in (("vpnToolbarVisible", "#compactVpnButton"),
                          ("showSidebar", "#compactSidebarButton"),
                          ("showBack", "#compactBackButton"),
                          ("showNewTab", "#compactNewTabButton")):
        evaluate(settings, f"""(() => {{
          const control = document.querySelector('[data-setting="{key}"]');
          if (!control.checked) control.click();
          return true;
        }})()""")
        wait_for(lambda selector=selector: evaluate(shell, f"getComputedStyle(document.querySelector('{selector}')).display !== 'none'"), f"{key} restored")
    # Verify light and dark are distinct on the toolbar, not only in Settings.
    evaluate(settings, """(() => {
      const theme = document.querySelector('#theme');
      theme.value = 'light';
      theme.dispatchEvent(new Event('change', {bubbles:true}));
      return true;
    })()""")
    wait_for(lambda: evaluate(shell, "document.body.dataset.theme === 'light'"), "light shell theme")
    light = evaluate(shell, "getComputedStyle(document.querySelector('.compact-toolbar')).backgroundImage")
    assert light != styles["background"], "Toolbar gradient did not change with theme"
    evaluate(settings, """(() => {
      const theme = document.querySelector('#theme');
      theme.value = 'dark';
      theme.dispatchEvent(new Event('change', {bubbles:true}));
      return true;
    })()""")
    wait_for(lambda: evaluate(shell, "document.body.dataset.theme === 'dark'"), "dark restored")
    persisted = evaluate(shell, """(async () => await window.browserShell.getSettings())()""")
    assert persisted["theme"] == "dark" and persisted["mattePanel"] is True
    assert persisted["vpnToolbarVisible"] is True
    print("CEF settings and shell UI test passed.")
finally:
    if settings: settings.close()
    shell.close()
