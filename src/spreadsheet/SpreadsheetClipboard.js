import {
  DefaultDOMElement, platform
} from 'substance'

export default class SpreadsheetClipboard {

  constructor(editorSession) {
    this.editorSession = editorSession
  }

  onCopy(e) {
    e.preventDefault()
    e.stopPropagation()
    if (e.clipboardData) {
      let data = this._copy()
      if (data) {
        // store as plain text and html
        e.clipboardData.setData('text/plain', data.text)
        try {
          e.clipboardData.setData('text/html', data.html)
        } catch(err) {
          // fails under some browsers
        }
      }
    }
  }

  onCut(e) {
    this.onCopy(e)
    this._cut()
  }

  onPaste(event) {
    let clipboardData = event.clipboardData
    let types = {}
    for (let i = 0; i < clipboardData.types.length; i++) {
      types[clipboardData.types[i]] = true
    }
    event.preventDefault()
    event.stopPropagation()
    let plainText
    let html
    if (types['text/plain']) {
      plainText = clipboardData.getData('text/plain')
    }
    if (types['text/html']) {
      html = clipboardData.getData('text/html')
    }
    // WORKAROUND: FF does not provide HTML coming in from other applications
    // so fall back to pasting plain text
    if (platform.isFF && !html) {
      this._pastePlainText(plainText)
      return
    }
    // if we have content given as HTML we let the importer assess the quality first
    // and fallback to plain text import if it's bad
    if (html) {
      this._pasteHtml(html, plainText)
    } else {
      this._pastePlainText(plainText)
    }
  }

  _pasteHtml(html, plainText) {
    let vals = this._htmlToVals(html)
    if (vals && vals.length > 0) {
      let n = vals.length
      let m = vals[0].length
      let range = this._getRange()
      this.editorSession.transaction((tx) => {
        let sheet = tx.getDocument()
        sheet.setValues(range.startRow, range.startCol, vals)
        tx.setSelection({
          type: 'custom',
          customType: 'sheet',
          data: {
            type: 'range',
            anchorRow: range.startRow,
            anchorCol: range.startCol,
            focusRow: range.startRow+n-1,
            focusCol: range.startCol+m-1
          }
        })
      })
    } else {
      this._pastePlainText(plainText)
    }
  }

  _pastePlainText(plainText) {
    let sel = this._getSelection()
    if (!sel) return
    const rowIdx = sel.anchorRow
    const colIdx = sel.anchorCol
    this.editorSession.transaction((tx) => {
      let sheet = tx.getDocument()
      let cell = sheet.getCell(rowIdx, colIdx)
      cell.textContent = plainText
      tx.setSelection({
        type: 'custom',
        customType: 'sheet',
        data: {
          type: 'range',
          anchorRow: rowIdx,
          anchorCol: colIdx,
          focusRow: rowIdx,
          focusCol: colIdx
        }
      })
    })
  }

  _getSelection() {
    let sel = this.editorSession.getSelection()
    if (sel.isCustomSelection() && sel.customType === 'sheet') {
      return sel.data
    } else {
      return null
    }
  }

  _getRange() {
    const sel = this._getSelection()
    if (!sel) return null
    const sheet = this.editorSession.getDocument()
    let startRow = Math.min(sel.anchorRow, sel.focusRow)
    let endRow = Math.max(sel.anchorRow, sel.focusRow)
    let startCol = Math.min(sel.anchorCol, sel.focusCol)
    let endCol = Math.max(sel.anchorCol, sel.focusCol)
    if (sel.type === 'columns') {
      startRow = 0
      endRow = sheet.getRowCount()
    } else if (sel.type === 'rows') {
      startCol = 0
      endRow = sheet.getColumnCount()
    }
    return {
      startRow, endRow, startCol, endCol
    }
  }

  _copy() {
    const sheet = this.editorSession.getDocument()
    const range = this._getRange()
    if (!range) return null
    let vals = sheet.getValues(range.startRow, range.startCol, range.endRow, range.endCol)
    let text = this._valsToPlainText(vals)
    let html = this._valsToHTML(vals)
    return { text, html }
  }

  _cut() {
    const range = this._getRange()
    if (!range) return
    this.editorSession.transaction((tx) => {
      let sheet = tx.getDocument()
      sheet.clearRange(range.startRow, range.startCol, range.endRow, range.endCol)
    })
  }

  _valsToHTML(vals) {
    let bodyHTML = vals.map((rowVals) => {
      const rowHTML = rowVals.map((val) => {
        return `<td>${val}</td>`
      }).join('')
      return `<tr>${rowHTML}</tr>`
    }).join('\n')
    return `<table>${bodyHTML}</table>`
  }

  _valsToPlainText(vals) {
    return vals.map((rowVals) => {
      return rowVals.join(' ')
    }).join('\n')
  }

  _htmlToVals(html) {
    let doc = DefaultDOMElement.parseHTML(html)
    let table = doc.find('table')
    if (table) {
      let rowEls = table.findAll('tr')
      let vals = rowEls.map((rowEl) => {
        return rowEl.children.map((cell) => {
          return cell.textContent
        })
      })
      return vals
    }
  }
}