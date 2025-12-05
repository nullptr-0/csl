package com.nullptr0.csl

import com.intellij.lexer.LexerBase
import com.intellij.psi.TokenType
import com.intellij.psi.tree.IElementType
import java.util.regex.Pattern

class CslLexer : LexerBase() {
    private var buffer: CharSequence = ""
    private var startOffset: Int = 0
    private var endOffset: Int = 0
    private var tokenStart: Int = 0
    private var tokenEnd: Int = 0
    private var tokenType: IElementType? = null

    override fun start(buffer: CharSequence, startOffset: Int, endOffset: Int, initialState: Int) {
        this.buffer = buffer
        this.startOffset = startOffset
        this.endOffset = endOffset
        this.tokenStart = startOffset
        this.tokenEnd = startOffset
        this.tokenType = null
        advance()
    }

    override fun getState(): Int = 0

    override fun getTokenType(): IElementType? = tokenType

    override fun getTokenStart(): Int = tokenStart

    override fun getTokenEnd(): Int = tokenEnd

    override fun advance() {
        if (tokenEnd >= endOffset) {
            tokenType = null
            return
        }
        tokenStart = if (tokenEnd == 0) startOffset else tokenEnd
        var i = tokenStart
        if (i >= endOffset) {
            tokenType = null
            return
        }

        fun isWhitespace(c: Char): Boolean = c == ' ' || c == '\t' || c == '\n' || c == '\r'
        fun isIdentStart(c: Char): Boolean = c == '_' || c.isLetter()
        fun isIdentPart(c: Char): Boolean = c == '_' || c.isLetterOrDigit()

        // Whitespace
        if (isWhitespace(buffer[i])) {
            i++
            while (i < endOffset && isWhitespace(buffer[i])) i++
            tokenType = TokenType.WHITE_SPACE
            tokenEnd = i
            return
        }

        val ch = buffer[i]
        // Line comment
        if (ch == '/' && i + 1 < endOffset && buffer[i + 1] == '/') {
            i += 2
            while (i < endOffset && buffer[i] != '\n' && buffer[i] != '\r') i++
            tokenType = CslTokenTypes.COMMENT
            tokenEnd = i
            return
        }

        // Braces/Brackets/Parens/Punctuators
        when (ch) {
            '{' -> { tokenType = CslTokenTypes.LBRACE; tokenEnd = i + 1; return }
            '}' -> { tokenType = CslTokenTypes.RBRACE; tokenEnd = i + 1; return }
            '[' -> { tokenType = CslTokenTypes.LBRACKET; tokenEnd = i + 1; return }
            ']' -> { tokenType = CslTokenTypes.RBRACKET; tokenEnd = i + 1; return }
            '(' -> { tokenType = CslTokenTypes.LPAREN; tokenEnd = i + 1; return }
            ')' -> { tokenType = CslTokenTypes.RPAREN; tokenEnd = i + 1; return }
            ',' -> { tokenType = CslTokenTypes.COMMA; tokenEnd = i + 1; return }
            ':' -> { tokenType = CslTokenTypes.COLON; tokenEnd = i + 1; return }
            '.' -> { tokenType = CslTokenTypes.DOT; tokenEnd = i + 1; return }
            ';' -> { tokenType = CslTokenTypes.PUNCTUATOR; tokenEnd = i + 1; return }
        }

        // Multi-char punctuators
        if (matchExact(i, "=>")) { tokenType = CslTokenTypes.PUNCTUATOR; tokenEnd = i + 2; return }

        // Raw quoted identifier: R`tag(content)tag`
        if (ch == 'R' && i + 1 < endOffset && buffer[i + 1] == '`') {
            var j = i + 2
            val tagStart = j
            var validTag = true
            var tagLen = 0
            while (j < endOffset) {
                val c = buffer[j]
                if (c == '(') break
                if (c == ')' || c == '\\' || c == '\n' || c == '\r' || tagLen > 16) { validTag = false; break }
                tagLen++
                j++
            }
            if (validTag && j < endOffset && buffer[j] == '(') {
                val tag = buffer.subSequence(tagStart, j).toString()
                j++
                // consume until ')'
                while (j < endOffset && buffer[j] != ')') j++
                if (j < endOffset && buffer[j] == ')') {
                    j++
                    // expect tag then backtick
                    val endNeeded = tag.length + 1
                    if (j + endNeeded <= endOffset && buffer.subSequence(j, j + tag.length).toString() == tag && buffer[j + tag.length] == '`') {
                        j += endNeeded
                        tokenType = CslTokenTypes.IDENTIFIER
                        tokenEnd = j
                        return
                    }
                }
            }
            // fall through if not valid raw quoted identifier
        }

        if (ch == 'R' && i + 1 < endOffset && buffer[i + 1] == '"') {
            var j = i + 2
            val tagStart = j
            var validTag = true
            var tagLen = 0
            while (j < endOffset) {
                val c = buffer[j]
                if (c == '(') break
                val allowed = (
                    c.isLetterOrDigit() ||
                    c == '!' || c == '"' || c == '#' || c == '%' || c == '&' || c == '\'' ||
                    c == '*' || c == '+' || c == ',' || c == '-' || c == '.' || c == '/' ||
                    c == ':' || c == ';' || c == '<' || c == '=' || c == '>' || c == '?' ||
                    c == '[' || c == ']' || c == '^' || c == '_' || c == '{' || c == '|' || c == '}' || c == '~'
                )
                if (!allowed || c == '\\' || c == ')' || c == '\n' || c == '\r' || tagLen > 16) { validTag = false; break }
                tagLen++
                j++
            }
            if (validTag && j < endOffset && buffer[j] == '(') {
                val tag = buffer.subSequence(tagStart, j).toString()
                j++
                while (j < endOffset) {
                    if (buffer[j] == ')') {
                        val endNeeded = tag.length + 1
                        if (j + endNeeded + 1 <= endOffset &&
                            buffer.subSequence(j + 1, j + 1 + tag.length).toString() == tag &&
                            buffer[j + 1 + tag.length] == '"') {
                            j += endNeeded + 1
                            tokenType = CslTokenTypes.STRING
                            tokenEnd = j
                            return
                        }
                    }
                    j++
                }
            }
        }

        if (ch == '"') {
            i++
            while (i < endOffset) {
                val c = buffer[i]
                if (c == '\\' && i + 1 < endOffset) { i += 2; continue }
                if (c == '"') { i++; break }
                i++
            }
            tokenType = CslTokenTypes.STRING
            tokenEnd = i
            return
        }

        // Boolean
        if (matchKeyword(i, "true") || matchKeyword(i, "false")) {
            val kw = if (matchKeyword(i, "true")) "true" else "false"
            tokenType = CslTokenTypes.BOOLEAN
            tokenEnd = i + kw.length
            return
        }

        // Type keywords
        val typeKeywords = arrayOf("string", "number", "boolean", "datetime", "duration")
        for (kw in typeKeywords) {
            if (matchKeyword(i, kw)) {
                tokenType = CslTokenTypes.TYPE
                tokenEnd = i + kw.length
                return
            }
        }
        if (matchKeyword(i, "any{}")) { tokenType = CslTokenTypes.TYPE; tokenEnd = i + 5; return }
        if (matchKeyword(i, "any[]")) { tokenType = CslTokenTypes.TYPE; tokenEnd = i + 5; return }

        // Keywords (including functions)
        val keywords = arrayOf(
            "config", "constraints", "requires", "conflicts", "with", "validate",
            "exists", "count_keys", "all_keys", "wildcard_keys", "subset"
        )
        for (kw in keywords) {
            if (matchKeyword(i, kw)) {
                tokenType = CslTokenTypes.KEYWORD
                tokenEnd = i + kw.length
                return
            }
        }
        if (i < endOffset && buffer[i] == '*') {
            tokenType = CslTokenTypes.KEYWORD
            tokenEnd = i + 1
            return
        }

        // Numbers (integer or decimal)
        if (ch.isDigit()) {
            i++
            while (i < endOffset && buffer[i].isDigit()) i++
            if (i < endOffset && buffer[i] == '.') {
                i++
                while (i < endOffset && buffer[i].isDigit()) i++
            }
            tokenType = CslTokenTypes.NUMBER
            tokenEnd = i
            return
        }

        // ISO 8601 Duration starting with 'P'
        if (ch == 'P') {
            val durPattern = Pattern.compile("P(?:\\d+Y)?(?:\\d+M)?(?:\\d+W)?(?:\\d+D)?(?:T(?:\\d+H)?(?:\\d+M)?(?:\\d+S)?)?")
            val m = durPattern.matcher(buffer.subSequence(i, endOffset))
            if (m.lookingAt()) {
                tokenType = CslTokenTypes.DURATION
                tokenEnd = i + m.end()
                return
            }
        }

        // Datetime ISO 8601 like 2024-12-03 or with time
        run {
            val dtPattern = Pattern.compile("\\d{4}-\\d{2}-\\d{2}(?:[ T]\\d{2}:\\d{2}(?::\\d{2})?(?:Z|[+-]\\d{2}:\\d{2})?)?")
            val m = dtPattern.matcher(buffer.subSequence(i, endOffset))
            if (m.lookingAt()) {
                tokenType = CslTokenTypes.DATETIME
                tokenEnd = i + m.end()
                return
            }
        }

        // Identifier (bare or backtick quoted)
        if (isIdentStart(ch)) {
            i++
            while (i < endOffset && isIdentPart(buffer[i])) i++
            tokenType = CslTokenTypes.IDENTIFIER
            tokenEnd = i
            return
        }
        if (ch == '`') {
            i++
            while (i < endOffset) {
                val c = buffer[i]
                if (c == '\\' && i + 1 < endOffset) { i += 2; continue }
                if (c == '`') { i++; break }
                i++
            }
            tokenType = CslTokenTypes.IDENTIFIER
            tokenEnd = i
            return
        }

        // Operators
        val twoCharOps = arrayOf("==", "!=", "<=", ">=", "&&", "||", "<<", ">>")
        for (op in twoCharOps) {
            if (matchExact(i, op)) {
                tokenType = CslTokenTypes.OPERATOR
                tokenEnd = i + op.length
                return
            }
        }
        val oneCharOps = charArrayOf('=', '!', '<', '>', '+', '-', '*', '/', '?', ':', '~', '&', '|', '^', '%', '@')
        if (oneCharOps.contains(ch)) {
            tokenType = CslTokenTypes.OPERATOR
            tokenEnd = i + 1
            return
        }

        // Punctuator fallback
        val punct = charArrayOf('{','}','[',']','(',')',',',':','.')
        if (punct.contains(ch)) {
            tokenType = CslTokenTypes.PUNCTUATOR
            tokenEnd = i + 1
            return
        }

        // Unknown: consume until whitespace or known punct
        i++
        while (i < endOffset) {
            val c = buffer[i]
            if (isWhitespace(c)) break
            if (charArrayOf('{','}','[',']','(',')',',',':','.').contains(c)) break
            i++
        }
        tokenType = CslTokenTypes.UNKNOWN
        tokenEnd = i
    }

    private fun matchExact(i: Int, s: String): Boolean {
        if (i + s.length > endOffset) return false
        for (k in s.indices) {
            if (buffer[i + k] != s[k]) return false
        }
        return true
    }

    private fun isWordBoundary(pos: Int): Boolean {
        if (pos !in (startOffset + 1)..<endOffset) return true
        val c = buffer[pos]
        return !(c.isLetterOrDigit() || c == '_')
    }

    private fun matchKeyword(i: Int, kw: String): Boolean {
        if (!matchExact(i, kw)) return false
        val before = i - 1
        val after = i + kw.length
        val beforeBoundary = before < startOffset || isWordBoundary(before)
        val afterBoundary = after >= endOffset || isWordBoundary(after)
        return beforeBoundary && afterBoundary
    }

    override fun getBufferSequence(): CharSequence = buffer

    override fun getBufferEnd(): Int = endOffset
}
