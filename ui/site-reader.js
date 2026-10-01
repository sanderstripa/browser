(() => {
  'use strict';
  const api = window.browserShell, safe = window.souluReaderSafe;
  if (!api?.getCurrentSite || !safe) return;
  let state = {}, site = null, key = '', revision = 0, anchor = null, articleKey = '', saving = Promise.resolve(), probing = false;
  const el = (tag, cls, text) => { const n = document.createElement(tag); n.className = cls || ''; if (text !== undefined) n.textContent = text; return n; };
  const error = e => { status.textContent = e.message || String(e); status.hidden = false; if (!reader.hidden) { readerNotice.textContent = status.textContent; readerNotice.hidden = false; } };
  const button = (text, fn, cls = '') => { const n = el('button', cls, text); n.type = 'button'; n.onclick = () => Promise.resolve().then(fn).catch(error); return n; };
  const shield = el('div', 'site-shield'), menu = el('section', 'site-popover'), reader = el('section', 'reader-view');
  shield.hidden = menu.hidden = reader.hidden = true;
  menu.setAttribute('role', 'dialog'); menu.setAttribute('aria-label', 'Функции текущего сайта');
  const status = el('p', 'site-status'); status.setAttribute('role', 'status'); status.hidden = true;
  const content = el('div', 'site-menu-content'); menu.append(content, status);
  document.body.append(reader, shield, menu);
  const readerBar = el('div', 'reader-toolbar'), article = el('article', 'reader-article'), settings = el('section', 'reader-settings');
  const readerNotice = el('p', 'reader-notice'); readerNotice.hidden = true; readerNotice.setAttribute('role', 'status');
  settings.hidden = true; settings.setAttribute('aria-label', 'Оформление режима чтения');
  const exit = button('‹ К странице', () => act('reader.exit'));
  const style = button('Оформление · Aa', () => { settings.hidden = !settings.hidden; style.setAttribute('aria-expanded', String(!settings.hidden)); });
  readerBar.append(exit, el('span', '', 'Режим чтения'), style); reader.append(readerBar, readerNotice, settings, article);
  let linkMenu = null;
  const closeLink = () => { linkMenu?.remove(); linkMenu = null; };
  function updateSurface() { api.setPopover(!menu.hidden || !findBox.hidden || Boolean(linkMenu)); }
  function close(focus = false) {
    menu.hidden = shield.hidden = true; closeLink(); updateSurface();
    anchor?.setAttribute('aria-expanded', 'false'); if (focus && anchor?.isConnected) anchor.focus();
  }
  function token(snapshot = site) { return {tabId: snapshot.tabId, url: snapshot.url, generation: snapshot.generation}; }
  async function act(action, values = {}, snapshot = site) {
    if (!snapshot) return;
    const result = await api.siteAction(action, {...token(snapshot), ...values});
    if (result?.tabId && key === `${result.tabId}|${result.url}|${result.generation}`) {
      site = result; renderReader(); if (!menu.hidden) renderMenu();
    }
    return result;
  }
  function position() {
    const rect = anchor?.isConnected ? anchor.getBoundingClientRect() : {left: 12, bottom: state.settings?.layout === 'classic' ? 82 : 48};
    menu.style.left = `${Math.max(8, Math.min(rect.left, innerWidth - 344))}px`;
    menu.style.top = `${Math.min(rect.bottom + 8, innerHeight - 80)}px`;
    menu.style.maxHeight = `${Math.max(50, innerHeight - rect.bottom - 20)}px`;
  }
  async function open() {
    closeLink(); settings.hidden = true;
    anchor = document.querySelector('body[data-layout=classic] #classicPageMenu') || document.querySelector('[data-page-menu]') || document.querySelector('#classicPageMenu');
    const request = ++revision; const snapshot = await api.getCurrentSite();
    if (request !== revision) return;
    site = snapshot; probing = Boolean(site.origin && !site.mainLoading && !site.readerActive); menu.hidden = shield.hidden = false; status.hidden = true;
    anchor?.setAttribute('aria-expanded', 'true'); renderMenu(); position(); await api.setPopover(true);
    menu.querySelector('button:not(:disabled)')?.focus();
    if (site.origin && !site.mainLoading && !site.readerActive) {
      const result = await act('reader.probe', {}, snapshot).catch(e => { if (request === revision && !menu.hidden) error(e); });
      if (request === revision) { probing = false; if (!menu.hidden) renderMenu(); }
    }
  }
  api.pageMenu = () => open().catch(error);
  shield.onpointerdown = () => close(true);
  function select(label, options, value, change) {
    const row = el('label', 'site-control'), control = el('select'); control.setAttribute('aria-label', label);
    for (const [v, name] of options) control.append(new Option(name, String(v)));
    control.value = String(value); control.onchange = () => Promise.resolve(change(control.value)).catch(error);
    row.append(el('span', '', label), control); return row;
  }
  function renderMenu() {
    if (!site) return;
    const expanded = content.querySelector('details')?.open, focused = content.contains(document.activeElement), focusedLabel = document.activeElement?.getAttribute('aria-label');
    content.replaceChildren();
    const head = el('header', 'site-heading'), icon = el('img');
    // Only a web favicon or a local data bitmap is accepted; never markup.
    if (safe.webURL(site.favicon, site.url) || /^data:image\/(png|jpeg|webp|x-icon);base64,/i.test(site.favicon || '')) {
      icon.src = site.favicon; icon.alt = ''; icon.onerror = () => icon.remove(); head.append(icon);
    }
    const info = el('div'); info.append(el('strong', '', site.domain || 'Внутренняя страница'), el('small', '', site.origin || site.url));
    info.append(el('small', '', site.origin?.startsWith('https:') ? (site.secureConnection ? 'HTTPS · защищённое соединение' : 'HTTPS · защита соединения не подтверждена') : site.origin ? 'HTTP · соединение не зашифровано' : 'Настройки сайта недоступны'));
    head.append(info); content.append(head);
    const read = button(site.readerActive ? 'Выйти из режима чтения' : 'Показать режим чтения', async () => { await act(site.readerActive ? 'reader.exit' : 'reader.enter'); close(); });
    read.dataset.readerAction = ''; read.disabled = !site.readerActive && !site.readerAvailable;
    content.append(read);
    if (!site.readerActive && !site.readerAvailable) content.append(el('small', 'site-hint', site.mainLoading ? 'Дождитесь загрузки страницы' : probing ? 'Проверяем, подходит ли страница для чтения…' : 'На этой странице статья не определена.'));
    const zoom = el('div', 'site-zoom');
    zoom.append(el('span', '', 'Масштаб'), button('−', () => act('zoom', {command: 'out'})), button(`${site.zoom || 100}%`, () => act('zoom', {command: 'reset'})), button('+', () => act('zoom', {command: 'in'})));
    for (const b of zoom.querySelectorAll('button')) b.disabled = !site.origin;
    zoom.querySelectorAll('button')[1].title = 'Сбросить к 100%'; content.append(zoom);
    const find = button('Найти на странице · Ctrl+F', async () => { close(); await act('find'); }); find.disabled = !site.origin; content.append(find);
    if (!site.origin || !site.rules) return;
    const rules = site.rules, details = el('details', 'site-permissions'); details.append(el('summary', '', 'Настройки сайта · разрешения'));
    details.open = Boolean(expanded);
    const names = {geolocation:'Геолокация',camera:'Камера',microphone:'Микрофон',notifications:'Уведомления',sound:'Звук',popups:'Всплывающие окна',downloads:'Загрузки'};
    for (const [name, label] of Object.entries(names)) {
      const labels = name === 'sound' ? ['Разрешить', 'Приглушить', 'Блокировать'] : ['Разрешить', 'Спрашивать', 'Запретить'];
      details.append(select(label, [[-1, `По умолчанию: ${labels[rules.defaults[name]]}`], ...labels.map((s, i) => [i, s])], rules.sites?.[site.domain]?.[name] ?? -1, value => act('permission', {permission:name, value:Number(value)})));
    }
    details.append(button('Подробное управление', () => { close(); return api.openSettingsWindow(); })); content.append(details);
    const blocking = rules.blocking, override = blocking.sites[site.domain], checked = override ?? blocking.enabled;
    const toggle = el('label', 'site-control'), check = el('input'); check.type = 'checkbox'; check.checked = Boolean(checked);
    check.onchange = () => act('blocking', {value:check.checked ? 1 : 0}).catch(error);
    toggle.append(el('span', '', 'Блокировка рекламы на этом сайте'), check); content.append(toggle);
    content.append(el('small', 'site-hint', `Глобально: ${blocking.enabled ? 'включена' : 'выключена'} · ${override === undefined ? 'по умолчанию' : 'исключение сайта'}`));
    if (override !== undefined) content.append(button('Для рекламы: по умолчанию', () => act('blocking', {value:2})));
    content.append(button('Данные сайта…', async () => {
      const result = await act('clear'); if (result?.cleared) { status.textContent = 'Хранилища origin очищены, включая sessionStorage. Cookies и HTTP-кэш сохранены.'; status.hidden = false; }
    }), button('Сбросить настройки сайта', () => act('reset')));
    content.append(el('small', 'site-hint', 'Сброс удаляет только исключения разрешений и рекламы для этого домена.'));
    if (focused) { const target = focusedLabel && [...content.querySelectorAll('[aria-label]')].find(n => n.getAttribute('aria-label') === focusedLabel); (target || content.querySelector('button:not(:disabled)'))?.focus(); }
  }
  function preferences(changes) {
    const snapshot = site;
    saving = saving.catch(() => {}).then(() => act('reader.preferences', {preferences:changes}, snapshot));
    return saving;
  }
  function renderReader() {
    const active = Boolean(site?.readerActive && site.article); reader.hidden = !active;
    document.body.dataset.reader = String(active);
    if (!active) { article.replaceChildren(); articleKey = ''; settings.hidden = true; return; }
    const prefs = site.preferences;
    reader.dataset.theme = prefs.theme; reader.dataset.font = prefs.font; reader.dataset.size = String(prefs.size);
    reader.dataset.width = String(prefs.width); reader.dataset.spacing = String(prefs.spacing); reader.dataset.images = String(prefs.images);
    const currentKey = `${site.tabId}|${site.url}|${site.generation}`;
    if (articleKey !== currentKey) {
      articleKey = currentKey; article.replaceChildren(); reader.scrollTop = 0;
      const data = site.article; article.append(el('h1', '', data.title));
      if (data.deck && !data.content.includes(data.deck)) article.append(el('p', 'reader-deck', data.deck));
      const metadata = [data.author, data.date].filter(Boolean).join(' · ');
      if (metadata) article.append(el('p', 'reader-meta', metadata));
      const body = el('div', 'reader-body'); body.append(safe.content(data.content, site.url)); article.append(body);
      const snapshot = site, images = [...body.querySelectorAll('img[data-reader-src]')].slice(0, 64);
      // Keep network activity in the source profile; deliver inert raster data
      // to the shell. Four concurrent requests, bounded by the native loader.
      let index = 0;
      async function loadImages() {
        while (index < images.length && articleKey === currentKey) {
          const image = images[index++];
          try { const result = await api.siteAction('reader.image', {...token(snapshot), target:image.dataset.readerSrc});
            if (image.isConnected && articleKey === currentKey && /^data:image\/(png|jpeg|webp|gif|avif);base64,/.test(result)) image.src = result;
          } catch { if (image.isConnected) image.alt = image.alt || 'Изображение недоступно'; }
        }
      }
      for (let i = 0; i < 4; i++) loadImages();
    }
    const wasFocused = settings.contains(document.activeElement), label = document.activeElement?.getAttribute('aria-label');
    settings.replaceChildren(select('Тема', [['light','Светлая'],['sepia','Сепия'],['dark','Тёмная']], prefs.theme, theme => preferences({theme})),
      select('Шрифт', [['sans','Sans-serif'],['serif','Serif'],['system','Системный']], prefs.font, font => preferences({font})),
      select('Ширина', [[0,'Узкая'],[1,'Средняя'],[2,'Широкая']], prefs.width, width => preferences({width:Number(width)})),
      select('Интервал', [[0,'Обычный'],[1,'Свободный'],[2,'Широкий']], prefs.spacing, spacing => preferences({spacing:Number(spacing)})));
    const size = el('div', 'site-control'); size.append(el('span', '', `Текст · ${prefs.size}`));
    const minus = button('A−', () => preferences({size:Math.max(14, site.preferences.size - 2)})), plus = button('A+', () => preferences({size:Math.min(32, site.preferences.size + 2)}));
    minus.disabled = prefs.size <= 14; plus.disabled = prefs.size >= 32; size.append(minus, plus); settings.append(size);
    const images = el('label', 'site-control'), check = el('input'); check.type = 'checkbox'; check.checked = prefs.images;
    check.setAttribute('aria-label', 'Изображения'); check.onchange = () => preferences({images:check.checked}).catch(error);
    images.append(el('span', '', 'Изображения'), check); settings.append(images);
    if (wasFocused && label) [...settings.querySelectorAll('[aria-label]')].find(n => n.getAttribute('aria-label') === label)?.focus();
  }
  function link(event, mode) {
    const a = event.target.closest('.reader-body a[href]'); if (!a) return;
    event.preventDefault(); const target = safe.webURL(a.getAttribute('href'), site.url); if (target) act('reader.link', {target, mode}).catch(error);
  }
  article.onclick = e => link(e, e.ctrlKey || e.metaKey ? (e.shiftKey ? 'new' : 'background') : 'current');
  article.onauxclick = e => { if (e.button === 1) link(e, 'background'); };
  article.oncontextmenu = e => {
    const a = e.target.closest('.reader-body a[href]'); if (!a) return;
    e.preventDefault(); closeLink(); const snapshot = site, target = safe.webURL(a.getAttribute('href'), snapshot.url);
    linkMenu = el('section', 'reader-link-menu');
    for (const [mode, text] of [['new','Открыть в новой вкладке'],['background','Открыть в фоновой вкладке'],['incognito','Открыть в инкогнито']]) linkMenu.append(button(text, () => { closeLink(); updateSurface(); return act('reader.link', {target, mode}, snapshot); }));
    linkMenu.style.left = `${Math.max(8, Math.min(e.clientX, innerWidth - 280))}px`; linkMenu.style.top = `${Math.min(e.clientY, innerHeight - 140)}px`;
    document.body.append(linkMenu); updateSurface(); linkMenu.querySelector('button')?.focus();
  };
  const findBox = el('section', 'site-find'), findInput = el('input'); findBox.hidden = true;
  findInput.setAttribute('aria-label', 'Найти на странице'); findInput.placeholder = 'Найти на странице';
  const closeFind = () => { findBox.hidden = true; api.find(''); updateSurface(); };
  findBox.append(findInput, button('Найти', () => api.find(findInput.value)), button('×', closeFind)); document.body.append(findBox);
  findInput.oninput = () => api.find(findInput.value); findInput.onkeydown = e => { if (e.key === 'Enter') api.find(findInput.value); };
  api.onRequestFind(() => { close(); findBox.hidden = false; updateSurface(); findInput.focus(); findInput.select(); });
  document.addEventListener('pointerdown', e => {
    if (linkMenu && !linkMenu.contains(e.target)) { closeLink(); updateSurface(); }
    if (!settings.hidden && !settings.contains(e.target) && e.target !== style) settings.hidden = true;
  });
  document.addEventListener('keydown', e => {
    if (e.key === 'Escape') { if (!menu.hidden) { e.preventDefault(); close(true); } else if (!findBox.hidden) closeFind(); else if (!settings.hidden) settings.hidden = true; else closeLink(); }
    if (e.key === 'Tab' && !menu.hidden) {
      const items = [...menu.querySelectorAll('button:not(:disabled),select,summary,input')].filter(n => n.getClientRects().length);
      if (e.shiftKey && document.activeElement === items[0]) { e.preventDefault(); items.at(-1)?.focus(); }
      else if (!e.shiftKey && document.activeElement === items.at(-1)) { e.preventDefault(); items[0]?.focus(); }
    }
  });
  async function sync(next) {
    state = next; reader.style.top = `${(state.settings?.layout === 'classic' ? 82 : 48) + (state.bookmarksBarVisible ? 28 : 0)}px`;
    reader.style.left = state.sidebarVisible ? '276px' : '0';
    const nextKey = `${state.activeTabId}|${state.page?.url || 'about:blank'}|${state.page?.generation}`;
    if (key !== nextKey) { key = nextKey; ++revision; site = null; articleKey = ''; reader.hidden = true; closeFind(); close(); }
    const request = revision; const result = await api.getCurrentSite();
    if (request !== revision) return; site = result; renderReader();
    if (!menu.hidden) { renderMenu(); position(); }
  }
  api.onState(next => sync(next).catch(error)); api.getState().then(sync).catch(error);
  window.addEventListener('resize', position);
})();
