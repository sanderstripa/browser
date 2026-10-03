(() => {
  'use strict';
  const preference = matchMedia('(prefers-reduced-motion: reduce)');
  const style = getComputedStyle(document.documentElement);
  const ms = name => parseFloat(style.getPropertyValue('--motion-'+name));
  const tokens = Object.freeze({micro: ms('micro'), surface: ms('surface'), structural: ms('structural'),
    sidebarOpen: ms('sidebar-open'), sidebarClose: ms('sidebar-close'), reduced: ms('reduced'),
    enter: style.getPropertyValue('--motion-enter').trim(), exit: style.getPropertyValue('--motion-exit').trim()});
  const active = new Set();
  function cancel() { for (const finish of [...active]) finish(); }
  function transaction() {
    const animations = [], cleanups = [];
    let done = false;
    const finish = () => {
      if (done) return;
      done = true;
      for (const a of animations) a.cancel();
      for (const cleanup of cleanups) cleanup();
      active.delete(finish);
    };
    active.add(finish);
    return {
      cleanup(fn) { cleanups.push(fn); },
      animate(node, frames, duration = tokens.structural, options = {}) {
        const a = node.animate(frames, {duration: preference.matches ? tokens.reduced : duration,
          easing: tokens.enter, fill: 'both', ...options});
        animations.push(a);
        return a;
      },
      play() { Promise.all(animations.map(a => a.finished.catch(() => {}))).then(finish); },
      finish
    };
  }
  preference.addEventListener('change', cancel);
  addEventListener('resize', cancel);
  addEventListener('pagehide', cancel);
  window.souluMotion = Object.freeze({tokens, transaction, cancel,
    get reduced() { return preference.matches; }, get activeCount() { return active.size; }});
})();
