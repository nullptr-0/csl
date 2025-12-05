package com.nullptr0.csl

import com.intellij.codeInsight.editorActions.TypedHandlerDelegate
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.fileTypes.FileType
import com.intellij.openapi.util.TextRange
import com.intellij.openapi.project.Project
import com.intellij.psi.PsiFile

class CslQuoteTypedHandler : TypedHandlerDelegate() {
    override fun beforeCharTyped(c: Char, project: Project, editor: Editor, file: PsiFile, fileType: FileType): Result {
        val isCsl = file.fileType === CslFileType || file.language === CslLanguage
        if (!isCsl) return Result.CONTINUE
        if (c != '"' && c != '`') return Result.CONTINUE

        val document = editor.document
        val caret = editor.caretModel.currentCaret
        val selection = editor.selectionModel
        val offset = caret.offset

        WriteCommandAction.runWriteCommandAction(project) {
            if (selection.hasSelection()) {
                val start = selection.selectionStart
                val end = selection.selectionEnd
                val selected = document.getText(TextRange(start, end))
                document.replaceString(start, end, "$c$selected$c")
                caret.moveToOffset(start + 1 + selected.length)
                selection.removeSelection()
            } else {
                val nextChar = if (offset < document.textLength) document.charsSequence[offset] else null
                if (nextChar != null && nextChar == c) {
                    caret.moveToOffset(offset + 1)
                } else {
                    document.insertString(offset, "$c$c")
                    caret.moveToOffset(offset + 1)
                }
            }
        }
        return Result.STOP
    }
}
