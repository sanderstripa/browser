// Only explicitly linked Soulu pages run this; never inject into web content.
// Hold the first visible frame until all real faces are available, including Cyrillic.
window.souluTypographyReady = Promise.all([400,500,600].map(weight =>
  document.fonts.load(`${weight} 14px Onest`, 'Aa Ёё Йй Жж Щщ Ыы Дд Лл 0123456789 @/:;()[]—+%')
)).then(faces => {
  if (faces.some(face => !face.length)) throw new Error('Bundled Onest face unavailable');
  document.documentElement.classList.add('typography-ready');
  return true;
}).catch(error => {
  // A broken package must not quietly present a different UI font.
  console.error('Soulu typography initialization failed', error);
  document.documentElement.dataset.typographyError = String(error);
  throw error;
});

// Native title bubbles cannot select a page's private @font-face. Own pages
// use one semantic tooltip; keep title/ARIA attributes for their callers.
(() => {
  const tip=document.createElement('div');tip.className='soulu-type-tooltip';
  tip.id='soulu-type-tooltip';tip.setAttribute('role','tooltip');tip.hidden=true;
  document.body.append(tip);
  let owner=null,timer=0,previousDescription=null;
  const hide=()=>{
    clearTimeout(timer);tip.hidden=true;
    if(owner){if(previousDescription===null)owner.removeAttribute('aria-describedby');else owner.setAttribute('aria-describedby',previousDescription);}
    owner=null;
  };
  const show=(node)=>{
    if(!node||node===owner||node.closest('.reader-article'))return;
    hide();owner=node;previousDescription=node.getAttribute('aria-describedby');
    timer=setTimeout(()=>{
      if(!node.isConnected||!node.title)return hide();
      tip.textContent=node.title;tip.hidden=false;
      const r=node.getBoundingClientRect(),size=tip.getBoundingClientRect();
      tip.style.left=Math.max(8,Math.min(r.left,innerWidth-size.width-8))+'px';
      const below=r.bottom+8;
      tip.style.top=Math.max(8,Math.min(below,innerHeight-size.height-8))+'px';
      node.setAttribute('aria-describedby',[previousDescription,tip.id].filter(Boolean).join(' '));
    },420);
  };
  document.addEventListener('pointerover',event=>show(event.target.closest('[title]')));
  document.addEventListener('pointerout',event=>{if(owner&&!owner.contains(event.relatedTarget))hide();});
  document.addEventListener('focusin',event=>show(event.target.closest('[title]')));
  document.addEventListener('focusout',hide);
  document.addEventListener('pointerdown',hide);
  document.addEventListener('keydown',hide);
  document.addEventListener('scroll',hide,true);
  window.addEventListener('resize',hide);
  window.addEventListener('blur',hide);
})();
