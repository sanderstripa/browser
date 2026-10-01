(() => {
  'use strict';
  const allowed = new Set('p h2 h3 h4 h5 h6 ul ol li blockquote pre code em strong b i u s del br hr a img figure figcaption table thead tbody tfoot tr th td sup sub span div'.split(' '));
  const forbidden = new Set('script style iframe frame object embed form input button select textarea svg math template noscript audio video source link meta base'.split(' '));
  function webURL(value, base) {
    try { const u = new URL(value, base); return /^https?:$/.test(u.protocol) && !u.username && !u.password ? u.href : ''; }
    catch { return ''; }
  }
  function content(html, base) {
    // Parsing in a template is inert: no scripts or resource loads, including
    // resources that will subsequently be rejected. Never adopt source nodes.
    const template = document.createElement('template'); template.innerHTML = String(html).slice(0, 1000000);
    const fragment = document.createDocumentFragment(); let count = 0;
    function copy(source, target, depth) {
      if (++count > 50000 || depth > 100) return;
      if (source.nodeType === Node.TEXT_NODE) { target.append(document.createTextNode(source.textContent)); return; }
      if (source.nodeType !== Node.ELEMENT_NODE) return;
      const tag = source.localName.toLowerCase();
      if (forbidden.has(tag)) return;
      const node = allowed.has(tag) ? document.createElement(tag) : document.createDocumentFragment();
      if (tag === 'a') {
        const href = webURL(source.getAttribute('href') || '', base);
        if (href) node.setAttribute('href', href);
        node.setAttribute('rel', 'noopener noreferrer');
      }
      if (tag === 'img') {
        const src = webURL(source.getAttribute('src') || '', base) || webURL(source.getAttribute('data-src') || '', base);
        if (!src) return;
        node.dataset.readerSrc = src; node.setAttribute('alt', (source.getAttribute('alt') || '').slice(0, 1000));
        node.setAttribute('loading', 'lazy'); node.setAttribute('referrerpolicy', 'no-referrer');
      }
      for (const child of source.childNodes) copy(child, node, depth + 1);
      target.append(node);
    }
    for (const child of template.content.childNodes) copy(child, fragment, 0);
    return fragment;
  }
  window.souluReaderSafe = Object.freeze({content, webURL});
})();
