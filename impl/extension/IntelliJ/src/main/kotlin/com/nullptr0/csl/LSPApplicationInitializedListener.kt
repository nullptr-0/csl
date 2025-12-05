// Copyright 2025 nullptr-0.
package com.nullptr0.csl

import com.intellij.ide.ApplicationInitializedListener
import com.intellij.ide.plugins.PluginManagerCore
import com.intellij.openapi.util.SystemInfo
import kotlin.io.path.exists
import org.wso2.lsp4intellij.IntellijLanguageClient
import org.wso2.lsp4intellij.client.languageserver.serverdefinition.RawCommandServerDefinition
import com.intellij.openapi.extensions.PluginId
import kotlinx.coroutines.CoroutineScope
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption

class LSPApplicationInitializedListener : ApplicationInitializedListener {
    override suspend fun execute() {
        val plugin = PluginManagerCore.getPlugin(PluginId.getId("com.nullptr-0.csl"))
        val pluginPath = plugin?.pluginPath ?: return

        val executableName = if (SystemInfo.isWindows) "csl.exe" else "csl"
        val targetPath: Path = pluginPath.resolve(executableName)

        if (!targetPath.exists()) {
            val resourcePath = 
                if (SystemInfo.isWindows) 
                    "/bin/windows/$executableName" 
                else if (SystemInfo.isLinux) 
                    "/bin/linux/$executableName" 
                else if (SystemInfo.isMac) 
                    "/bin/macos/$executableName" 
                else null

            if (resourcePath != null) {
                val input = LSPApplicationInitializedListener::class.java.getResourceAsStream(resourcePath)
                if (input != null) {
                    Files.copy(input, targetPath, StandardCopyOption.REPLACE_EXISTING)
                    try { targetPath.toFile().setExecutable(true) } catch (_: Throwable) {}
                }
            }
        }

        if (targetPath.exists()) {
            IntellijLanguageClient.addExtensionManager("csl", CslLspExtensionManager())
            val command = arrayOf(
                targetPath.toString(), "--langsvr", "--stdio"
            )
            IntellijLanguageClient.addServerDefinition(
                RawCommandServerDefinition("csl", command)
            )
        }
    }
}
