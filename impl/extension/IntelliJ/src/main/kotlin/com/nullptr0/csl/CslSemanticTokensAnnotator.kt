package com.nullptr0.csl

import com.intellij.lang.annotation.AnnotationHolder
import com.intellij.lang.annotation.ExternalAnnotator
import com.intellij.lang.annotation.HighlightSeverity
import com.intellij.openapi.editor.Document
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.TextRange
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.PsiFile
import org.eclipse.lsp4j.SemanticTokens
import org.eclipse.lsp4j.SemanticTokensParams
import org.eclipse.lsp4j.TextDocumentIdentifier
import org.wso2.lsp4intellij.IntellijLanguageClient
import org.wso2.lsp4intellij.client.languageserver.wrapper.LanguageServerWrapper
import org.wso2.lsp4intellij.utils.FileUtils
import java.util.concurrent.TimeUnit

class CslSemanticTokensAnnotator : ExternalAnnotator<CslSemanticTokensAnnotator.Input, List<CslSemanticTokensAnnotator.TokenPaint>>() {
    data class Input(val project: Project, val file: PsiFile, val vfile: VirtualFile, val document: Document, val uri: String)
    data class TokenPaint(val range: TextRange, val key: com.intellij.openapi.editor.colors.TextAttributesKey)

    override fun collectInformation(file: PsiFile): Input? {
        val project = file.project
        val vfile = file.virtualFile ?: return null
        val document = com.intellij.openapi.fileEditor.FileDocumentManager.getInstance().getDocument(vfile) ?: return null
        val uri = FileUtils.pathToUri(vfile.path)
        return Input(project, file, vfile, document, uri)
    }

    override fun doAnnotate(collectedInfo: Input?): List<TokenPaint> {
        if (collectedInfo == null) return emptyList()
        val wrapper = findWrapper(collectedInfo.project) ?: return emptyList()
        val server = wrapper.server ?: return emptyList()
        val textDoc = server.textDocumentService
        val params = SemanticTokensParams(TextDocumentIdentifier(collectedInfo.uri))
        val fut = textDoc.semanticTokensFull(params)
        val tokens: SemanticTokens = try {
            fut.get(1500, TimeUnit.MILLISECONDS)
        } catch (_: Throwable) {
            return emptyList()
        }
        return decodeAndMap(tokens, collectedInfo.document)
    }

    override fun apply(file: PsiFile, annotationResult: List<TokenPaint>, holder: AnnotationHolder) {
        for (tp in annotationResult) {
            holder.newAnnotation(HighlightSeverity.INFORMATION, "")
                .range(tp.range)
                .textAttributes(tp.key)
                .create()
        }
    }

    private fun findWrapper(project: Project): LanguageServerWrapper? {
        @Suppress("UNCHECKED_CAST")
        val projectUri: String? = FileUtils.projectToUri(project)
        val map = IntellijLanguageClient.getProjectToLanguageWrappers()
        val set = map[projectUri] ?: return null
        return set.firstOrNull()
    }

    private fun decodeAndMap(tokens: SemanticTokens, document: Document): List<TokenPaint> {
        val data = tokens.data ?: return emptyList()
        var line = 0
        var startChar = 0
        val res = ArrayList<TokenPaint>(data.size / 5)
        var i = 0
        while (i + 4 < data.size) {
            val deltaLine = data[i]
            val deltaStart = data[i + 1]
            val length = data[i + 2]
            val tokenType = data[i + 3]
            // val modifiers = data[i + 4]
            i += 5

            line += deltaLine
            startChar = if (deltaLine == 0) startChar + deltaStart else deltaStart
            val startOffset = safeOffset(document, line, startChar)
            val endOffset = startOffset + length
            val key = mapTokenType(tokenType)
            if (key != null && startOffset >= 0) {
                res.add(TokenPaint(TextRange(startOffset, endOffset), key))
            }
        }
        return res
    }

    private fun safeOffset(document: Document, line: Int, col: Int): Int {
        if (line < 0 || line >= document.lineCount) return -1
        val base = document.getLineStartOffset(line)
        return base + col
    }

    private fun mapTokenType(idx: Int): com.intellij.openapi.editor.colors.TextAttributesKey? {
        return when (idx) {
            0 -> CslSyntaxHighlighter.DATETIME
            1 -> CslSyntaxHighlighter.DURATION
            2 -> CslSyntaxHighlighter.NUMBER
            3 -> CslSyntaxHighlighter.KEYWORD // boolean literal styled as keyword here
            4 -> CslSyntaxHighlighter.KEYWORD
            5 -> CslSyntaxHighlighter.TYPE
            6 -> CslSyntaxHighlighter.IDENTIFIER
            7 -> CslSyntaxHighlighter.DOT
            8 -> CslSyntaxHighlighter.OPERATOR
            9 -> CslSyntaxHighlighter.COMMENT
            10 -> CslSyntaxHighlighter.STRING
            11 -> CslSyntaxHighlighter.BAD_CHARACTER
            else -> null
        }
    }
}
