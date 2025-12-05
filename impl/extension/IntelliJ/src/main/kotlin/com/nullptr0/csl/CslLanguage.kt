// Copyright 2025 nullptr-0.
package com.nullptr0.csl

import com.intellij.lang.Language

object CslLanguage : Language("CSL") {
    @Suppress("unused")
    private fun readResolve(): Any = CslLanguage
}