// Copyright 2025 nullptr-0.
package com.nullptr0.csl

import com.intellij.openapi.fileTypes.LanguageFileType
import javax.swing.Icon

object CslFileType : LanguageFileType(CslLanguage) {
    override fun getName() = "CSL File"
    override fun getDescription() = "CSL language file"
    override fun getDefaultExtension() = "csl"
    override fun getIcon(): Icon = CslIcons.FILE
}
