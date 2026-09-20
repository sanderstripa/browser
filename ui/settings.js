(() => {
  const $ = s => document.querySelector(s);
  const $$ = s => [...document.querySelectorAll(s)];
  let settings = {};
  let browserState = { profiles: [], update: null };
  const copy = {
    ru: {
      settings:"Настройки",appearance:"Оформление",toolbar:"Панель",search:"Поиск",startup:"Запуск",downloads:"Загрузки",accounts:"Аккаунты",passwords:"Пароли",
      panelMode:"Режим панели",mainMode:"Основная",classicMode:"Классическая",mainDefault:"Основная — компактная панель Soulu и режим по умолчанию.",
      theme:"Тема",themeHint:"Оформление интерфейса",system:"Системная",light:"Светлая",dark:"Тёмная",matte:"Матовая прозрачность",matteHint:"Размытие и мягкий градиент панели",language:"Язык",languageHint:"Язык меню и настроек",
      visibleWidgets:"Элементы панели",widgetsHint:"Отключённые элементы исчезают сразу. Вернуть их можно здесь.",sidebar:"Боковая панель",back:"Назад",favorites:"Избранное",newTab:"Новая вкладка",
      addressPosition:"Положение адресной строки",center:"По центру",left:"Слева",extensionsPosition:"Положение расширений",leftOfAddress:"Слева от адреса",rightOfAddress:"Справа от адреса",downloadIcon:"Иконка загрузок",dynamic:"Динамически",always:"Всегда",
      searchEngine:"Поисковая система",addressBehavior:"Открывать адрес",currentTab:"В текущей вкладке",newIfOccupied:"В новой, если текущая занята",startPage:"Стартовая страница",blank:"Пустая",custom:"Свой адрес",startUrl:"Адрес стартовой страницы",
      askLocation:"Спрашивать место сохранения",downloadPath:"Папка загрузок",vpnHint:"VPN выключен по умолчанию. Кнопку на панели можно скрыть в разделе «Панель».",vpnButton:"Показывать кнопку VPN",
      accountsHint:"Вход в Google выполняется в общей сессии браузера. Синхронизация Chrome пока не включена.",passwordsHint:"Хранилище паролей будет подключено к нативной CEF-реализации на следующем этапе."
    },
    en: {
      settings:"Settings",appearance:"Appearance",toolbar:"Toolbar",search:"Search",startup:"Startup",downloads:"Downloads",accounts:"Accounts",passwords:"Passwords",
      panelMode:"Toolbar mode",mainMode:"Main",classicMode:"Classic",mainDefault:"Main is Soulu's compact toolbar and the default mode.",
      theme:"Theme",themeHint:"Interface appearance",system:"System",light:"Light",dark:"Dark",matte:"Matte transparency",matteHint:"Blur and a soft toolbar gradient",language:"Language",languageHint:"Language for menus and settings",
      visibleWidgets:"Toolbar items",widgetsHint:"Disabled items disappear immediately. You can restore them here.",sidebar:"Sidebar",back:"Back",favorites:"Favorites",newTab:"New tab",
      addressPosition:"Address position",center:"Centered",left:"Left",extensionsPosition:"Extensions position",leftOfAddress:"Left of address",rightOfAddress:"Right of address",downloadIcon:"Downloads icon",dynamic:"Dynamic",always:"Always",
      searchEngine:"Search engine",addressBehavior:"Open address",currentTab:"In current tab",newIfOccupied:"In a new tab when occupied",startPage:"Start page",blank:"Blank",custom:"Custom address",startUrl:"Start page address",
      askLocation:"Ask where to save",downloadPath:"Downloads folder",vpnHint:"VPN is off by default. Its toolbar button can be hidden in Toolbar.",vpnButton:"Show VPN button",
      accountsHint:"Google sign-in uses the shared browser session. Chrome Sync is not enabled.",passwordsHint:"Password storage will be connected to the native CEF implementation in a later step."
    }
  };
  Object.assign(copy.ru, {profiles:"Профили",updates:"Обновления",profilesHint:"У каждого профиля отдельные cookies, кэш, сессии сайтов и хранилища.",profileName:"Название профиля",createProfile:"Создать профиль",incognito:"Режим инкогнито",incognitoHint:"Данные вкладок инкогнито не записываются на диск.",openIncognito:"Открыть вкладку",souluUpdates:"Обновления Soulu",recommended:"Рекомендуемая",automaticUpdates:"Автоматически проверять обновления",checkUpdates:"Проверить",updateRestart:"Обновить и перезапустить"});
  Object.assign(copy.en, {profiles:"Profiles",updates:"Updates",profilesHint:"Every profile has separate cookies, cache, site sessions and storage.",profileName:"Profile name",createProfile:"Create profile",incognito:"Incognito mode",incognitoHint:"Incognito tab data is never written to disk.",openIncognito:"Open tab",souluUpdates:"Soulu updates",recommended:"Recommended",automaticUpdates:"Automatically check for updates",checkUpdates:"Check",updateRestart:"Update and restart"});

  const sectionHints = {
    ru:{appearance:"Настройте вид браузера и основной панели.",toolbar:"Выберите состав и расположение элементов.",search:"Поиск и поведение адресной строки.",startup:"Что Soulu открывает при запуске.",downloads:"Папка и поведение загрузок.",vpn:"Параметры встроенного VPN.",profiles:"Независимые пространства для личной и рабочей жизни.",accounts:"Учётные записи и веб-сессии.",passwords:"Сохранённые данные входа.",updates:"Версии Soulu, CEF и Chromium."},
    en:{appearance:"Customize the browser and main toolbar.",toolbar:"Choose toolbar items and placement.",search:"Search and address bar behavior.",startup:"What Soulu opens on launch.",downloads:"Download folder and behavior.",vpn:"Built-in VPN preferences.",profiles:"Independent spaces for personal and work use.",accounts:"Accounts and web sessions.",passwords:"Saved sign-in details.",updates:"Soulu, CEF and Chromium versions."}
  };
  const titles = {appearance:"appearance",toolbar:"toolbar",search:"search",startup:"startup",downloads:"downloads",vpn:"VPN",profiles:"profiles",accounts:"accounts",passwords:"passwords",updates:"updates"};

  function translate() {
    const lang = settings.language === "en" ? "en" : "ru";
    document.documentElement.lang = lang;
    document.title = lang === "en" ? "Soulu Settings" : "Настройки Soulu";
    $("[data-t]").forEach(el => { const v=copy[lang][el.dataset.t]; if(v) el.textContent=v; });
    $("[data-t-placeholder]").forEach(el => { const v=copy[lang][el.dataset.tPlaceholder]; if(v) el.placeholder=v; });
    const active=$(".nav-item.active")?.dataset.section || "appearance";
    $("#sectionTitle").textContent = titles[active] === "VPN" ? "VPN" : copy[lang][titles[active]];
    $("#sectionHint").textContent = sectionHints[lang][active];
  }

  function fill() {
    $$('[name="layout"]').forEach(x => x.checked = x.value === (settings.layout || "compact"));
    const values={theme:"system",language:"ru",addressPosition:"center",extensionsPosition:"left",downloadsMode:"dynamic",searchEngine:"google",addressOpenMode:"current",startPageMode:"blank",startPageUrl:"",downloadPath:""};
    for(const [id,fallback] of Object.entries(values)){const el=$("#"+id);if(el)el.value=settings[id] ?? fallback;}
    $("#mattePanel").checked = settings.mattePanel !== false;
    $("#askDownloadLocation").checked = settings.askDownloadLocation !== false;
    $("[data-setting]").forEach(el => el.checked = settings[el.dataset.setting] !== false);
    $("#automaticUpdates").checked = settings.automaticUpdates !== false;
    renderProfiles();
    renderUpdate(browserState.update);
    translate();
  }

  function renderProfiles() {
    const box = $("#profileList");
    if (!box) return;
    box.innerHTML = (browserState.profiles || []).map(profile =>
      `<div class="profile-row${profile.active ? " active" : ""}"><span><b>${profile.name}</b><small>${profile.id}</small></span><button data-profile-id="${profile.id}" ${profile.active ? "disabled" : ""}>${profile.active ? "✓" : "Открыть"}</button></div>`
    ).join("");
    box.querySelectorAll("[data-profile-id]").forEach(button =>
      button.onclick = async () => {
        browserState = await window.browserShell.switchProfile(button.dataset.profileId);
        settings = browserState.settings || settings;
        fill();
      });
  }

  function renderUpdate(update) {
    if (!update) return;
    $("#souluVersion").textContent = update.soulu || "—";
    $("#recommendedVersion").textContent = update.recommended || "—";
    $("#cefVersion").textContent = update.cef || "—";
    $("#chromiumVersion").textContent = update.chromium || "—";
    const lang = settings.language === "en" ? "en" : "ru";
    $("#updateStatus").textContent = update.available
      ? (update.security ? (lang === "en" ? "Critical security update" : "Критическое обновление безопасности") : (lang === "en" ? "Update available" : "Доступно обновление"))
      : (lang === "en" ? "Soulu is up to date" : "Установлена актуальная версия");
    $("#updatePill").textContent = update.available ? (lang === "en" ? "Update" : "Обновление") : "OK";
    $("#updatePill").classList.toggle("security", Boolean(update.security));
    $("#updateDot").classList.toggle("visible", Boolean(update.available));
    $("#installUpdate").disabled = !update.available;
  }

  async function patch(value) {
    settings = await window.browserShell.setSettings(value);
    fill();
  }

  $$(".nav-item").forEach(button => button.onclick = () => {
    $$(".nav-item").forEach(x => x.classList.toggle("active", x === button));
    $$(".settings-section").forEach(x => x.classList.toggle("active", x.dataset.page === button.dataset.section));
    translate();
  });
  $$('[name="layout"]').forEach(el => el.onchange=()=>patch({layout:el.value}));
  for(const id of ["theme","language","addressPosition","extensionsPosition","downloadsMode","searchEngine","addressOpenMode","startPageMode","startPageUrl","downloadPath"]){
    const el=$("#"+id); if(el) el.onchange=()=>patch({[id]:el.value});
  }
  $("#mattePanel").onchange=e=>patch({mattePanel:e.target.checked});
  $("#askDownloadLocation").onchange=e=>patch({askDownloadLocation:e.target.checked});
  $("#automaticUpdates").onchange=e=>patch({automaticUpdates:e.target.checked});
  $("[data-setting]").forEach(el=>el.onchange=e=>patch({[el.dataset.setting]:e.target.checked}));
  $("#createProfile").onclick=async()=>{
    const name=$("#profileName").value.trim();
    if(!name)return;
    browserState=await window.browserShell.createProfile(name);
    settings=browserState.settings||settings;
    $("#profileName").value="";
    fill();
  };
  $("#openIncognito").onclick=()=>window.browserShell.newIncognito();
  $("#checkUpdates").onclick=async()=>{browserState.update=await window.browserShell.checkUpdates();renderUpdate(browserState.update);};

  window.browserShell.onState(state => {browserState=state;settings=state.settings||settings;fill();});
  Promise.all([window.browserShell.getSettings(),window.browserShell.getState()]).then(([value,state])=>{settings=value||{};browserState=state||browserState;fill();});
})();
