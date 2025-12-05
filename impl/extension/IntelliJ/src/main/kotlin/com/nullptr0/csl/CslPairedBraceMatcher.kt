package com.nullptr0.csl

import com.intellij.lang.PairedBraceMatcher
import com.intellij.psi.PsiFile
import com.intellij.psi.tree.IElementType
import com.intellij.lang.BracePair

class CslPairedBraceMatcher : PairedBraceMatcher {
    private val pairs = arrayOf(
        BracePair(CslTokenTypes.LBRACE, CslTokenTypes.RBRACE, true),
        BracePair(CslTokenTypes.LBRACKET, CslTokenTypes.RBRACKET, true)
    )

    override fun getPairs(): Array<BracePair> = pairs
    override fun isPairedBracesAllowedBeforeType(lbraceType: IElementType, contextType: IElementType?): Boolean = true
    override fun getCodeConstructStart(file: PsiFile?, openingBraceOffset: Int): Int = openingBraceOffset
}
