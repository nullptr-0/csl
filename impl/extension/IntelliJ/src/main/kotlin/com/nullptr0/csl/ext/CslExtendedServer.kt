package com.nullptr0.csl.ext

import org.eclipse.lsp4j.jsonrpc.services.JsonRequest
import org.eclipse.lsp4j.services.LanguageServer
import java.util.concurrent.CompletableFuture

interface CslExtendedServer : LanguageServer {
    @JsonRequest("csl/generateHtmlDoc")
    fun generateHtmlDoc(params: CslGenerateHtmlDocParams?): CompletableFuture<MutableMap<String?, String?>?>?
}

