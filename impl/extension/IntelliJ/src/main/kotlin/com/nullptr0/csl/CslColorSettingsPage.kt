package com.nullptr0.csl

import com.intellij.openapi.options.colors.AttributesDescriptor
import com.intellij.openapi.fileTypes.SyntaxHighlighter
import com.intellij.openapi.options.colors.ColorDescriptor
import com.intellij.openapi.options.colors.ColorSettingsPage
import javax.swing.Icon
import com.intellij.openapi.editor.colors.TextAttributesKey

class CslColorSettingsPage : ColorSettingsPage {
    private val descriptors = arrayOf(
        AttributesDescriptor("Keyword", CslSyntaxHighlighter.KEYWORD),
        AttributesDescriptor("Type", CslSyntaxHighlighter.TYPE),
        AttributesDescriptor("Identifier", CslSyntaxHighlighter.IDENTIFIER),
        AttributesDescriptor("String", CslSyntaxHighlighter.STRING),
        AttributesDescriptor("Number", CslSyntaxHighlighter.NUMBER),
        AttributesDescriptor("Operator", CslSyntaxHighlighter.OPERATOR),
        AttributesDescriptor("Braces", CslSyntaxHighlighter.BRACES),
        AttributesDescriptor("Brackets", CslSyntaxHighlighter.BRACKETS),
        AttributesDescriptor("Parentheses", CslSyntaxHighlighter.PARENTHESES),
        AttributesDescriptor("Comma", CslSyntaxHighlighter.COMMA),
        AttributesDescriptor("Dot/Punctuator", CslSyntaxHighlighter.DOT),
        AttributesDescriptor("Comment", CslSyntaxHighlighter.COMMENT),
        AttributesDescriptor("Unknown/Bad", CslSyntaxHighlighter.BAD_CHARACTER)
    )

    override fun getDisplayName(): String = "CSL"

    override fun getIcon(): Icon = CslIcons.FILE

    override fun getHighlighter(): SyntaxHighlighter = CslSyntaxHighlighter()

    override fun getAdditionalHighlightingTagToDescriptorMap(): MutableMap<String, TextAttributesKey>? = null

    override fun getAttributeDescriptors(): Array<AttributesDescriptor> = descriptors

    override fun getColorDescriptors(): Array<ColorDescriptor> = ColorDescriptor.EMPTY_ARRAY

    override fun getDemoText(): String = """
        // CSL demo
        config DemoConfig {
            name: string;
            options: {
                enabled: boolean;
            };
        }
    """.trimIndent()
}
