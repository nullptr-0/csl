package com.nullptr0.csl

import com.intellij.ide.BrowserUtil
import com.intellij.openapi.actionSystem.ActionUpdateThread
import com.intellij.openapi.actionSystem.AnAction
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.CommonDataKeys
import com.intellij.openapi.actionSystem.LangDataKeys
import com.intellij.openapi.diagnostic.Logger
import com.intellij.openapi.fileChooser.FileChooser
import com.intellij.openapi.fileChooser.FileChooserDescriptor
import com.intellij.openapi.progress.ProgressManager
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.ui.Messages
import com.intellij.openapi.vfs.VirtualFile
import org.wso2.lsp4intellij.IntellijLanguageClient
import org.wso2.lsp4intellij.client.languageserver.wrapper.LanguageServerWrapper
import org.wso2.lsp4intellij.utils.FileUtils
import com.nullptr0.csl.ext.CslExtendedServer
import com.nullptr0.csl.ext.CslGenerateHtmlDocParams
import java.io.File
import java.nio.charset.StandardCharsets
import java.util.concurrent.TimeUnit

class GenerateHtmlDocAction : AnAction(), DumbAware {
    override fun getActionUpdateThread(): ActionUpdateThread = ActionUpdateThread.BGT

    private val log = Logger.getInstance(GenerateHtmlDocAction::class.java)

    override fun update(e: AnActionEvent) {
        val file = getTargetFile(e)
        e.presentation.isEnabledAndVisible = file?.extension?.lowercase() == "csl"
    }

    override fun actionPerformed(e: AnActionEvent) {
        val project = e.project ?: return
        val input = getTargetFile(e) ?: return

        val descriptor = FileChooserDescriptor(false, true, false, false, false, false)
            .withTitle("Select Output Folder for CSL HTML Docs")
        val outputDir = FileChooser.chooseFile(descriptor, project, null) ?: return
        val outputPath = outputDir.path

        val uri = FileUtils.pathToUri(input.path)
        val text = try {
            String(input.contentsToByteArray(), StandardCharsets.UTF_8)
        } catch (_: Throwable) { "" }

        var error: Throwable? = null

        ProgressManager.getInstance().runProcessWithProgressSynchronously({
            try {
                val wrapper = findWrapper(project)
                val server = wrapper?.server ?: error("Language server is not available")
                val extServer = server as? CslExtendedServer ?: error("Extended server interface is not available")

                val params = CslGenerateHtmlDocParams().apply {
                    textDocument = CslGenerateHtmlDocParams.TextDocument().apply {
                        this.uri = uri
                        this.text = text
                    }
                }

                val files = extServer.generateHtmlDoc(params)?.get(60, TimeUnit.SECONDS).orEmpty()

                for ((name, content) in files) {
                    val safeName = name ?: continue
                    val out = File(outputPath, safeName)
                    out.parentFile?.mkdirs()
                    out.writeText(content.orEmpty(), Charsets.UTF_8)
                }
            } catch (t: Throwable) {
                error = t
            }
        }, "Generating CSL HTML Docs", false, project)

        val t = error
        if (t != null) {
            log.warn("Failed to generate HTML docs", t)
            Messages.showErrorDialog(project, t.message ?: "Failed to generate HTML docs", "CSL")
            return
        }

        Messages.showInfoMessage(project, "Generated HTML docs in: $outputPath", "CSL")

        val indexIo = File(outputPath, "index.html")
        if (indexIo.exists()) {
            BrowserUtil.browse(indexIo)
        } else {
            Messages.showWarningDialog(project, "index.html not found in: $outputPath", "CSL")
        }
    }

    private fun getTargetFile(e: AnActionEvent): VirtualFile? {
        val vFile = e.getData(CommonDataKeys.VIRTUAL_FILE)
        if (vFile != null) return vFile
        e.getData(CommonDataKeys.EDITOR)
        val psiFile = e.getData(LangDataKeys.PSI_FILE)
        return psiFile?.virtualFile
    }

    private fun findWrapper(project: com.intellij.openapi.project.Project): LanguageServerWrapper? {
        val projectUri: String? = FileUtils.projectToUri(project)
        val map = IntellijLanguageClient.getProjectToLanguageWrappers()
        val set = map[projectUri] ?: return null
        return set.firstOrNull()
    }
}
