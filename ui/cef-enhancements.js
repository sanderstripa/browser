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


(() => {
 const tip=document.createElement('div');tip.className='soulu-tooltip';tip.hidden=true;document.body.append(tip);
 let timer=0,owner=null;
 const hide=()=>{clearTimeout(timer);owner=null;if(!tip.hidden){tip.hidden=true;if(!document.querySelector('.suggestions.visible'))window.browserShell.setSuggestionsHeight(0);}};
 document.addEventListener('pointerover',e=>{const node=e.target.closest('.compact-tab[data-tooltip],.classic-tab[data-tooltip]');if(!node||node===owner)return;hide();owner=node;timer=setTimeout(()=>{if(!node.isConnected)return;tip.textContent=node.dataset.tooltip;const r=node.getBoundingClientRect();tip.style.left=Math.min(window.innerWidth-310,Math.max(8,r.left))+'px';tip.style.top=(r.bottom+10)+'px';tip.hidden=false;window.browserShell.setSuggestionsHeight(r.bottom+78);},420);});
 document.addEventListener('pointerout',e=>{if(owner&&!owner.contains(e.relatedTarget))hide();});
 document.addEventListener('pointerdown',hide);window.addEventListener('blur',hide);
})();
