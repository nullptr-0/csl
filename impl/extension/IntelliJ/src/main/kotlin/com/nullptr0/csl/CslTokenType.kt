package com.nullptr0.csl

import com.intellij.psi.tree.IElementType

class CslTokenType(debugName: String) : IElementType(debugName, CslLanguage)

object CslTokenTypes {
    val LBRACE = CslTokenType("LBRACE")
    val RBRACE = CslTokenType("RBRACE")
    val LBRACKET = CslTokenType("LBRACKET")
    val RBRACKET = CslTokenType("RBRACKET")

    val LPAREN = CslTokenType("LPAREN")
    val RPAREN = CslTokenType("RPAREN")
    val COMMA = CslTokenType("COMMA")
    val COLON = CslTokenType("COLON")
    val DOT = CslTokenType("DOT")

    val COMMENT = CslTokenType("COMMENT")
    val STRING = CslTokenType("STRING")
    val NUMBER = CslTokenType("NUMBER")
    val BOOLEAN = CslTokenType("BOOLEAN")
    val DATETIME = CslTokenType("DATETIME")
    val DURATION = CslTokenType("DURATION")
    val KEYWORD = CslTokenType("KEYWORD")
    val TYPE = CslTokenType("TYPE")
    val IDENTIFIER = CslTokenType("IDENTIFIER")
    val OPERATOR = CslTokenType("OPERATOR")
    val PUNCTUATOR = CslTokenType("PUNCTUATOR")
    val UNKNOWN = CslTokenType("UNKNOWN")
}
