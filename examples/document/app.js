/*
  A tiny integration of a Stencila Document editor
  using a stub backend.
*/

import { DocumentPage, MemoryBackend, getQueryStringParam } from 'stencila'

window.addEventListener('load', () => {
  window.documentPage = DocumentPage.mount({
    backend: new MemoryBackend(),
    documentId: getQueryStringParam('documentId') || 'kitchen-sink'
  }, window.document.body)


  function onKeyDown(e) {
    // CTRL+S
    if (e.ctrlKey && e.keyCode === 83) {
      console.info('saving')
      window.documentPage.save()
    }
  }
  document.addEventListener('keydown', onKeyDown, false);

})
