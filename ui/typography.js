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
