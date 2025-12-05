package com.nullptr0.csl

import com.intellij.lexer.Lexer
import com.intellij.openapi.editor.DefaultLanguageHighlighterColors
import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.openapi.fileTypes.SyntaxHighlighter
import com.intellij.psi.TokenType
import com.intellij.psi.tree.IElementType

class CslSyntaxHighlighter : SyntaxHighlighter {
    override fun getHighlightingLexer(): Lexer = CslLexer()

    override fun getTokenHighlights(tokenType: IElementType): Array<TextAttributesKey> = when (tokenType) {
        TokenType.WHITE_SPACE -> emptyArray<TextAttributesKey>()

        CslTokenTypes.COMMENT -> pack(COMMENT)
        CslTokenTypes.STRING -> pack(STRING)
        CslTokenTypes.NUMBER -> pack(NUMBER)
        CslTokenTypes.BOOLEAN -> pack(KEYWORD)
        CslTokenTypes.DATETIME -> pack(STRING)
        CslTokenTypes.DURATION -> pack(STRING)

        CslTokenTypes.KEYWORD -> pack(KEYWORD)
        CslTokenTypes.TYPE -> pack(TYPE)
        CslTokenTypes.IDENTIFIER -> pack(IDENTIFIER)

        CslTokenTypes.OPERATOR -> pack(OPERATOR)
        CslTokenTypes.LBRACE, CslTokenTypes.RBRACE -> pack(BRACES)
        CslTokenTypes.LBRACKET, CslTokenTypes.RBRACKET -> pack(BRACKETS)
        CslTokenTypes.LPAREN, CslTokenTypes.RPAREN -> pack(PARENTHESES)
        CslTokenTypes.COMMA -> pack(COMMA)
        CslTokenTypes.COLON -> pack(DOT) // closest default style
        CslTokenTypes.DOT -> pack(DOT)
        CslTokenTypes.PUNCTUATOR -> pack(DOT)

        CslTokenTypes.UNKNOWN -> pack(BAD_CHARACTER)
        else -> emptyArray<TextAttributesKey>()
    }

    companion object Keys {
        val KEYWORD: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_KEYWORD", DefaultLanguageHighlighterColors.KEYWORD
        )
        val TYPE: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_TYPE", DefaultLanguageHighlighterColors.CLASS_NAME
        )
        val IDENTIFIER: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_IDENTIFIER", DefaultLanguageHighlighterColors.IDENTIFIER
        )
        val STRING: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_STRING", DefaultLanguageHighlighterColors.STRING
        )
        val NUMBER: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_NUMBER", DefaultLanguageHighlighterColors.NUMBER
        )
        val DATETIME: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_DATETIME", DefaultLanguageHighlighterColors.STRING
        )
        val DURATION: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_DURATION", DefaultLanguageHighlighterColors.STRING
        )
        val OPERATOR: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_OPERATOR", DefaultLanguageHighlighterColors.OPERATION_SIGN
        )
        val BRACES: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_BRACES", DefaultLanguageHighlighterColors.BRACES
        )
        val BRACKETS: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_BRACKETS", DefaultLanguageHighlighterColors.BRACKETS
        )
        val PARENTHESES: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_PARENTHESES", DefaultLanguageHighlighterColors.PARENTHESES
        )
        val COMMA: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_COMMA", DefaultLanguageHighlighterColors.COMMA
        )
        val DOT: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_DOT", DefaultLanguageHighlighterColors.DOT
        )
        val COMMENT: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_COMMENT", DefaultLanguageHighlighterColors.LINE_COMMENT
        )
        val BAD_CHARACTER: TextAttributesKey = TextAttributesKey.createTextAttributesKey(
            "CSL_BAD_CHARACTER", DefaultLanguageHighlighterColors.INVALID_STRING_ESCAPE
        )

        private fun pack(vararg keys: TextAttributesKey): Array<TextAttributesKey> = arrayOf(*keys)
    }
}
