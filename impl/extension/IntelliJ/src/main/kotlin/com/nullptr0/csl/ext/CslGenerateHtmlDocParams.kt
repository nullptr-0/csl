package com.nullptr0.csl.ext

import java.util.Objects

class CslGenerateHtmlDocParams {
    var textDocument: TextDocument? = null

    class TextDocument {
        var uri: String? = null
        var text: String? = null
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is CslGenerateHtmlDocParams) return false
        return textDocument?.uri == other.textDocument?.uri
    }

    override fun hashCode(): Int {
        return Objects.hash(textDocument?.uri)
    }
}
