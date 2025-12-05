package com.nullptr0.csl

import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.event.DocumentListener
import org.eclipse.lsp4j.ServerCapabilities
import org.eclipse.lsp4j.services.LanguageClient
import org.eclipse.lsp4j.services.LanguageServer
import org.wso2.lsp4intellij.client.ClientContext
import org.wso2.lsp4intellij.client.DefaultLanguageClient
import org.wso2.lsp4intellij.client.languageserver.ServerOptions
import org.wso2.lsp4intellij.client.languageserver.requestmanager.DefaultRequestManager
import org.wso2.lsp4intellij.client.languageserver.requestmanager.RequestManager
import org.wso2.lsp4intellij.client.languageserver.wrapper.LanguageServerWrapper
import org.wso2.lsp4intellij.editor.EditorEventManager
import org.wso2.lsp4intellij.extensions.LSPExtensionManager
import org.wso2.lsp4intellij.listeners.EditorMouseListenerImpl
import org.wso2.lsp4intellij.listeners.EditorMouseMotionListenerImpl
import org.wso2.lsp4intellij.listeners.LSPCaretListenerImpl
import com.nullptr0.csl.ext.CslExtendedServer

class CslLspExtensionManager : LSPExtensionManager {
    override fun <T : DefaultRequestManager> getExtendedRequestManagerFor(
        wrapper: LanguageServerWrapper,
        server: LanguageServer,
        client: LanguageClient,
        serverCapabilities: ServerCapabilities
    ): T {
        @Suppress("UNCHECKED_CAST")
        return DefaultRequestManager(wrapper, server, client, serverCapabilities) as T
    }

    override fun <T : EditorEventManager> getExtendedEditorEventManagerFor(
        editor: Editor,
        documentListener: DocumentListener,
        mouseListener: EditorMouseListenerImpl,
        mouseMotionListener: EditorMouseMotionListenerImpl,
        caretListener: LSPCaretListenerImpl,
        requestManager: RequestManager,
        serverOptions: ServerOptions,
        wrapper: LanguageServerWrapper
    ): T {
        @Suppress("UNCHECKED_CAST")
        return EditorEventManager(editor, documentListener, mouseListener, mouseMotionListener, caretListener, requestManager, serverOptions, wrapper) as T
    }

    override fun getExtendedServerInterface(): Class<out LanguageServer> {
        return CslExtendedServer::class.java
    }

    override fun getExtendedClientFor(context: ClientContext): LanguageClient {
        return DefaultLanguageClient(context)
    }

}
