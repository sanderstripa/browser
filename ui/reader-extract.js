// Runs in an isolated Chromium world, never in the source page's JS world.
(() => {
  if (!/^https?:$/.test(location.protocol) || document.body?.innerText.length < 500) return null;
  const article = new Readability(document.cloneNode(true), {charThreshold: 500, maxElemsToParse: 50000}).parse();
  if (!article || article.length < 500 || !article.content) return null;
  // Return untrusted data only. The trusted view builds a new allowlisted DOM.
  return {url: location.href, title: article.title || document.title,
    deck: article.excerpt || '', author: article.byline || '', date: article.publishedTime || '',
    content: article.content.slice(0, 1000000)};
})()
