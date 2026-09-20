(() => {
  // Electron exposes this API from preload.js. CEF exposes cefQuery instead.
  if (window.browserShell || typeof window.cefQuery !== "function") return;

  const listeners = new Map();
  const emit = (name, value) => {
    for (const listener of listeners.get(name) || []) listener(value);
  };
  window.__souluEmit = emit;

  const subscribe = (name, callback) => {
    if (!listeners.has(name)) listeners.set(name, new Set());
    listeners.get(name).add(callback);
    return () => listeners.get(name)?.delete(callback);
  };

  const invoke = (action, payload = null) => new Promise((resolve, reject) => {
    window.cefQuery({
      request: JSON.stringify({ action, payload }),
      persistent: false,
      onSuccess: response => {
        if (!response) return resolve(null);
        try { resolve(JSON.parse(response)); }
        catch { resolve(response); }
      },
      onFailure: (_code, message) => reject(new Error(message || `CEF action failed: ${action}`))
    });
  });

  window.browserShell = {
    navigate: value => invoke("browser.navigate", value),
    back: () => invoke("browser.back"),
    reload: () => invoke("browser.reload"),
    newTab: () => invoke("browser.newTab"),
    switchTab: id => invoke("browser.switchTab", id),
    closeTab: id => invoke("browser.closeTab", id),
    toggleSidebar: () => invoke("browser.toggleSidebar"),
    setRightPanel: width => invoke("browser.setRightPanel", width),
    setSuggestionsHeight: height => invoke("browser.setSuggestionsHeight", height),
    pageMenu: () => invoke("browser.pageMenu"),
    shareMenu: () => invoke("browser.shareMenu"),
    find: value => invoke("browser.find", value),
    suggestions: value => invoke("browser.suggestions", value),
    getDownloads: () => invoke("browser.downloads.get"),
    getBookmarks: () => invoke("browser.bookmarks.get"),
    addBookmark: () => invoke("browser.bookmarks.add"),
    removeBookmark: id => invoke("browser.bookmarks.remove", id),
    openBookmark: url => invoke("browser.bookmarks.open", url),
    getSettings: () => invoke("browser.settings.get"),
    setSettings: value => invoke("browser.settings.set", value),
    chooseDownloadFolder: () => invoke("browser.downloads.chooseFolder"),
    getPasswords: () => invoke("browser.passwords.get"),
    addPassword: value => invoke("browser.passwords.add", value),
    removePassword: id => invoke("browser.passwords.remove", id),
    copyPassword: (id, field) => invoke("browser.passwords.copy", { id, field }),
    addGoogleAccount: () => invoke("browser.google.add"),
    manageGoogleAccounts: () => invoke("browser.google.manage"),
    minimize: () => invoke("window.minimize"),
    maximize: () => invoke("window.maximize"),
    close: () => invoke("window.close"),
    getState: () => invoke("browser.state.get"),
    onState: callback => subscribe("state", callback),
    onDownloads: callback => subscribe("downloads", callback),
    onFocusAddress: callback => subscribe("focusAddress", callback),
    onRequestFind: callback => subscribe("requestFind", callback)
  };

  window.vpn = {
    send: (action, payload = {}) => invoke("vpn.send", { action, payload }),
    settingsGet: () => invoke("vpn.settings.get"),
    settingsSet: value => invoke("vpn.settings.set", value),
    onState: callback => subscribe("vpnState", callback)
  };

  document.addEventListener("mousedown", event => {
    if (event.button !== 0) return;
    if (event.target.closest(".window-drag-strip,.compact-drag-space")) {
      event.preventDefault();
      invoke("window.beginDrag");
    }
  });

  document.addEventListener("contextmenu", event => {
    const toolbar = event.target.closest(".toolbar,.compact-toolbar");
    if (!toolbar || event.target.closest("input,button,select,.suggestions")) return;
    event.preventDefault();
    invoke("window.toolbarMenu");
  });
})();
