(() => {
  if (!window.browserShell) return;

  const apply = state => {
    const settings = state?.settings || state || {};
    const set = (name, value, fallback = true) => {
      document.body.dataset[name] = String(value ?? fallback);
    };
    set("showSidebar", settings.showSidebar);
    set("showBack", settings.showBack);
    set("showFavorites", settings.showFavorites);
    set("showNewTab", settings.showNewTab);
    set("showDownloads", settings.showDownloads);
    set("showVpn", settings.vpnToolbarVisible);
    document.body.dataset.incognito = String(Boolean(state?.incognito));
  };

  window.browserShell.onState(apply);
  window.browserShell.getState().then(apply).catch(() => {});
})();
