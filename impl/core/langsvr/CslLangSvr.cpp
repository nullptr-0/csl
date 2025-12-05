#include <string>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <tuple>
#include <functional>
#include <iostream>
#include <fstream>
#include <regex>
#include <stack>
#include "../shared/JsonIO.hpp"
#include "../shared/Components.h"
#include "../shared/CslRepr2Csl.h"
#include "TextEdit.h"
#include "FindPairs.h"
#include "../shared/Token.h"
#include "../shared/FilePosition.h"

using ReprPtr = std::variant<std::shared_ptr<CSL::ConfigSchema>, std::shared_ptr<CSL::TableType::KeyDefinition>>;
using json = jsonio::Value;

enum LineEndType {
    LF,
    CRLF,
    UNKNOWN
};

inline LineEndType lineEndType = LineEndType::UNKNOWN;

std::string readLSPContent(std::istream& stream) {
    std::string line;
    size_t contentLength = 0;

    // Read header part
    while (true) {
        char ch = stream.get();
        if (ch == std::char_traits<char>::eof()) {
            throw std::runtime_error("unexpected EOF reached when reading LSP header");
        }

        line += ch;

        // Header fields are terminated by "\r\n"
        if (!line.empty() && line[line.size() - 1] == '\n') {
            if (line == "\n" || line == "\r\n") {
                if (lineEndType == LineEndType::UNKNOWN) {
                    lineEndType = line.size() == 1 ? LF : CRLF;
                }
                if (contentLength) {
                    break;  // End of headers
                }
            }

            if (line.find("Content-Length:") == 0) {
                contentLength = std::stoi(line.substr(15));  // Extract content length
            }

            line.clear();
        }
    }
    // Read content part
    std::string content;
    content.reserve(contentLength);
    for (size_t i = 0; i < contentLength; ++i) {
        char ch = stream.get();
        if (ch == std::char_traits<char>::eof()) {
            throw std::runtime_error("unexpected EOF reached when reading LSP content");
        }
#ifdef EMSCRIPTEN
        if (ch == -3) {
            ch = '?';
        }
#endif
        content += ch;
    }

    return content;
}

void writeLSPContent(std::ostream& stream, const std::string& content) {
    // Calculate the content length
    size_t contentLength = content.size();

    // Create the header
    std::string header = "Content-Length: " + std::to_string(contentLength
#ifdef EMSCRIPTEN
        + (lineEndType == LF ? 1 : 2)
#endif
    ) + (lineEndType == LF ? "\n\n" : "\r\n\r\n");

    // Write the header and content to the file
    stream.write(header.c_str(), header.size());
    stream.write((content
#ifdef EMSCRIPTEN
        + (lineEndType == LF ? "\n" : "\r\n")
#endif
        ).c_str(), content.size()
#ifdef EMSCRIPTEN
        + (lineEndType == LF ? 1 : 2)
#endif
    );

    // Flush to ensure data is written
    stream.flush();
}

class LanguageServer {
public:
    LanguageServer(
        std::istream& inChannel, std::ostream& outChannel,
        const CslLexerFunctionWithStringInput& cslLexer,
        const CslParserFunction& cslParser) :
        cslLexer(cslLexer), cslParser(cslParser), jsonId(0),
        inChannel(inChannel), outChannel(outChannel) {
    }

    int run() {
        size_t jsonId = 0;
        int serverExitCode = -1;
        std::string input = readLSPContent(inChannel);

        while (!input.empty() && serverExitCode == -1) {
            json request;
            try {
                request = jsonio::parseText(input);
            }
            catch (const std::exception& e) {
                std::cerr << "Error parsing JSON: " << e.what() << "\n";
            }
            try {
                if (isResponse(request)) {
                    try {
                        if (jsonio::hasKey(request, "id")) {
                            auto it = responseCallbacks.find(idToString(request["id"]));
                            if (it != responseCallbacks.end()) {
                                auto callback = it->second;
                                callback(request);
                                responseCallbacks.erase(it);
                            }
                        }
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Error processing response: " << e.what() << "\n";
                    }
                }
                else {
                    auto response = handleRequest(request);
                    if ((response.isObject() ? !response.empty() : !response.isNull())) {
                        sendResponse(response);
                    }
                }
            }
            catch (const std::exception& e) {
                json error = json::Object({
                    json::to_keypair("jsonrpc", "2.0"),
                    json::to_keypair("id", json::Number(jsonId)),
                    json::to_keypair("error", json::Object({
                        json::to_keypair("error", json::Number(-32700)),
                        json::to_keypair("message", e.what())
                    }))
                });
                ++jsonId;
                sendResponse(error);
            }
            serverExitCode = getServerExitCode();
            input = readLSPContent(inChannel);
        }
        return serverExitCode;
    }

protected:
    std::istream& inChannel;
    std::ostream& outChannel;
    size_t jsonId;
    bool isServerInitialized = false;
    bool isClientInitialized = false;
    bool isServerShutdown = false;
    bool isServerExited = false;
    bool clientSupportsMultilineToken = false;
    std::string traceValue;
    CslLexerFunctionWithStringInput cslLexer;
    CslParserFunction cslParser;
    struct DocumentData {
        std::string text;
        Token::TokenList<std::string, std::unique_ptr<Type::Type>> tokensNoComment;
        Token::TokenList<std::string, std::unique_ptr<Type::Type>> tokensWithComment;
        std::vector<std::shared_ptr<CSL::ConfigSchema>> schemas;
        std::unordered_map<size_t, ReprPtr> tokenCslReprMapping;
        std::vector<std::tuple<std::string, FilePosition::Region>> lexErrors;
        std::vector<std::tuple<std::string, FilePosition::Region>> lexWarnings;
        std::vector<std::tuple<std::string, FilePosition::Region>> parseErrors;
        std::vector<std::tuple<std::string, FilePosition::Region>> parseWarnings;
    };
    std::unordered_map<std::string, DocumentData> documentCache;
    std::vector<std::shared_ptr<CSL::ConfigSchema>> cslSchemas;
    std::string currentCslSchema;
    std::unordered_map<std::string, std::function<void(const json&)>> responseCallbacks;

    std::string idToString(const json& id) {
        try {
            return id.get<std::string>();
        } catch (...) {
            try {
                return std::to_string(id.get<size_t>());
            } catch (...) {
                return std::string();
            }
        }
    }

    void sendRequest(const json& request, std::function<void(const json&)> callback = [](const json&) {}) {
        if (!jsonio::hasKey(request, "jsonrpc") ||
            request["jsonrpc"].get<std::string>() != "2.0" ||
            !jsonio::hasKey(request, "id") ||
            !jsonio::hasKey(request, "method")) {
            return;
        }
        writeLSPContent(outChannel, jsonio::dump(request));
        responseCallbacks[idToString(request["id"])] = callback;
    }

    bool isResponse(const json& response) {
        return jsonio::hasKey(response, "jsonrpc")
            && response["jsonrpc"].get<std::string>() == "2.0"
            && (jsonio::hasKey(response, "result")
            || jsonio::hasKey(response, "error"));
    }

    void sendResponse(const json& response) {
        if (!isResponse(response)) {
            return;
        }
        writeLSPContent(outChannel, jsonio::dump(response));
    }

    void sendNotification(const json& notification) {
        if (!jsonio::hasKey(notification, "jsonrpc") ||
            notification["jsonrpc"].get<std::string>() != "2.0" ||
            !jsonio::hasKey(notification, "method")) {
			return;
		}
        writeLSPContent(outChannel, jsonio::dump(notification));
    }

    std::string normalizeUri(const std::string& uri) {
        auto isHex = [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
        };
        auto isAllowed = [](char c) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return true;
            switch (c) {
                case '-': case '.': case '_': case '~':
                case '/': case '?': case '#': case '[': case ']': case '@':
                case '!': case '$': case '&': case '\'': case '(': case ')': case '*': case '+': case ',': case ';': case '=':
                    return true;
                default:
                    return false;
            }
        };
        static const char hexDigits[] = "0123456789abcdef";

        std::string pathOut;
        pathOut.reserve(uri.size() > 7 ? uri.size() - 7 : 0);

        size_t i = 7;
        bool hasLeadingSlash = (i < uri.size() && uri[i] == '/');
        size_t driveIdx = hasLeadingSlash ? i + 1 : i;
        auto isAlpha = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
        bool drivePattern = false;
        if (driveIdx + 1 < uri.size() && isAlpha(uri[driveIdx])) {
            if (uri[driveIdx + 1] == ':') {
                drivePattern = true;
            } else if (uri[driveIdx + 1] == '%' && driveIdx + 3 < uri.size() && uri[driveIdx + 2] == '3' && (uri[driveIdx + 3] == 'A' || uri[driveIdx + 3] == 'a')) {
                drivePattern = true;
            }
        }

        if (!hasLeadingSlash && drivePattern) {
            pathOut.push_back('/');
        }

        for (; i < uri.size(); ++i) {
            char c = uri[i];
            if (c == '%' && i + 2 < uri.size() && isHex(uri[i + 1]) && isHex(uri[i + 2])) {
                pathOut.push_back('%');
                pathOut.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(uri[i + 1]))));
                pathOut.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(uri[i + 2]))));
                i += 2;
            } else if (isAllowed(c)) {
                pathOut.push_back(c);
            } else {
                unsigned char uc = static_cast<unsigned char>(c);
                pathOut.push_back('%');
                pathOut.push_back(hexDigits[(uc >> 4) & 0xF]);
                pathOut.push_back(hexDigits[uc & 0xF]);
            }
        }

        bool lowerCaseWindowsPath = false;
        if (pathOut.size() >= 5 && pathOut[0] == '/' && isAlpha(pathOut[1]) && pathOut[2] == '%' && pathOut[3] == '3' && (pathOut[4] == 'A' || pathOut[4] == 'a')) {
            lowerCaseWindowsPath = true;
        }

        if (lowerCaseWindowsPath) {
            for (size_t j = 0; j < pathOut.size(); ++j) {
                if (pathOut[j] == '%' && j + 2 < pathOut.size()) {
                    j += 2;
                    continue;
                }
                pathOut[j] = static_cast<char>(std::tolower(static_cast<unsigned char>(pathOut[j])));
            }
        }

        std::string out = "file://";
        out += pathOut;
        return out;
    }

    json handleRequest(const json& request) {
        json requestId;
        if (jsonio::hasKey(request, "id")) {
            requestId = request["id"];
        }
        try {
            if (request["method"].get<std::string>() == "initialize") {
                return handleInitialize(request);
            }
            else {
                if (!isServerInitialized) {
                    throw std::runtime_error("Server not initialized");
                }
                if (request["method"].get<std::string>() == "initialized") {
                    return handleInitialized(request);
                }
                else {
                    if (!isClientInitialized) {
                        throw std::runtime_error("Client not initialized");
                    }
                    if (isServerShutdown && request["method"].get<std::string>() != "exit") {
                        throw std::runtime_error("Server already shutdown");
                    }
                    else {
						std::string method = request["method"].get<std::string>();
                        if (method == "exit") {
                            return handleExit(request);
                        }
                        else if (method == "shutdown") {
                            return handleShutdown(request);
                        }
                        else if (method == "textDocument/didOpen") {
                            return handleDidOpen(request);
                        }
                        else if (method == "textDocument/didChange") {
                            return handleDidChange(request);
                        }
                        else if (method == "textDocument/didClose") {
                            return handleDidClose(request);
                        }
                        else if (method == "$/setTrace") {
                            return handleSetTrace(request);
                        }
                        else if (method == "textDocument/references") {
                            return handleReferences(request);
                        }
                        else if (method == "textDocument/rename") {
                            return handleRename(request);
                        }
                        else if (method == "textDocument/foldingRange") {
                            return handleFoldingRange(request);
                        }
                        else if (method == "textDocument/semanticTokens/full") {
                            return handleSemanticTokens(request);
                        }
                        else if (method == "textDocument/formatting") {
                            return handleFormatting(request);
                        }
                        else if (method == "textDocument/definition") {
                            return handleDefinition(request);
                        }
                        else if (method == "textDocument/completion") {
                            return handleCompletion(request);
                        }
                        else if (method == "textDocument/hover") {
                            return handleHover(request);
                        }
                        else if (method == "textDocument/diagnostic") {
                            return handlePullDiagnostic(request);
                        }
                        else if (method == "csl/generateHtmlDoc") {
                            return handleGenerateHtmlDoc(request);
                        }
                    }
                    json error = json::Object({
                        json::to_keypair("jsonrpc", "2.0"),
                        json::to_keypair("id", requestId),
                        json::to_keypair("error", json::Object({
                            json::to_keypair("error", json::Number(-32601)),
                            json::to_keypair("message", "Method not found")
                        }))
                    });
                    return error;
                }
            }
        }
        catch (const std::exception& e) {
            json error = json::Object({
                json::to_keypair("jsonrpc", "2.0"),
                json::to_keypair("id", requestId),
                json::to_keypair("error", json::Object({
                    json::to_keypair("error", json::Number(-32603)),
                    json::to_keypair("message", e.what())
                }))
            });
            return error;
        }
    }

    int getServerExitCode() {
        return isServerExited ? isServerShutdown ? 0 : 1 : -1;
    }

    json genRequest(const std::string& method, const json& params) {
        json request = json::Object({
            json::to_keypair("jsonrpc", "2.0"),
            json::to_keypair("id", json::Number(jsonId)),
            json::to_keypair("method", method),
            json::to_keypair("params", params)
        });
        ++jsonId;
        return request;
    }

    json genResponse(const json& id, const json& result, const json& error) {
        if (error.isNull()) {
            return json::Object({
                json::to_keypair("jsonrpc", "2.0"),
                json::to_keypair("id", id),
                json::to_keypair("result", result)
            });
        } else {
            return json::Object({
                json::to_keypair("jsonrpc", "2.0"),
                json::to_keypair("id", id),
                json::to_keypair("error", error)
            });
        }
    }

    json genNotification(const std::string& method, const json& params) {
        return json::Object({
            json::to_keypair("jsonrpc", "2.0"),
            json::to_keypair("method", method),
            json::to_keypair("params", params)
        });
    }

    json handleInitialize(const json& request) {
        if (isServerInitialized) {
            throw std::runtime_error("Initialize request may only be sent once");
        }
        isServerInitialized = true;
        traceValue = jsonio::hasKey(request["params"], "trace") ? request["params"]["trace"].get<std::string>() : "";
        clientSupportsMultilineToken =
            jsonio::hasKey(request["params"], "textDocument") &&
            jsonio::hasKey(request["params"], "semanticTokens") &&
            jsonio::hasKey(request["params"], "multilineTokenSupport") &&
            request["params"]["capabilities"]["textDocument"]["semanticTokens"]["multilineTokenSupport"].get<bool>();
        return genResponse(
            request["id"],
            jsonio::parseText(R"({
                   "capabilities": {
                       "textDocumentSync": 1,
                       "referencesProvider": true,
                       "renameProvider": true,
                       "foldingRangeProvider": true,
                       "semanticTokensProvider": {
                           "legend": {
                               "tokenTypes": [
                                   "datetime", "duration", "number", "boolean", "keyword", "type", "identifier",
                                   "punctuator", "operator", "comment", "string", "unknown"
                               ],
                               "tokenModifiers": []
                           },
                           "full": true
                       },
                       "documentFormattingProvider": true,
                       "definitionProvider": true,
                       "completionProvider": {
                           "triggerCharacters": [".", "-", "c", "s", "n", "b", "d", "a", "w", "r", "v", "e"],
                           "allCommitCharacters": [".", "=", " ", "\"", "'", "]", "}"]
                        },
                       "hoverProvider": true,
                       "diagnosticProvider": {
                           "interFileDependencies": true,
                           "workspaceDiagnostics": false
                       }
                   }
               })"),
            json()
        );
    }

    json handleInitialized(const json& request) {
        if (isClientInitialized) {
            throw std::runtime_error("Initialized request may only be sent once");
        }
        isClientInitialized = true;
        return json();
    }

    void recomputeDocument(const std::string& uri, const std::string& text) {
        auto nuri = normalizeUri(uri);
        DocumentData data;
        data.text = text;
        auto [tokensNC, lexErrorsNC, lexWarningsNC] = cslLexer(text, false, clientSupportsMultilineToken);
        auto [schemas, parseErrors, parseWarnings, tokenCslReprMapping] = cslParser(tokensNC);
        auto [tokensWC, lexErrorsWC, lexWarningsWC] = cslLexer(text, true, clientSupportsMultilineToken);
        data.tokensNoComment = std::move(tokensNC);
        data.tokensWithComment = std::move(tokensWC);
        data.schemas = std::move(schemas);
        data.tokenCslReprMapping = std::move(tokenCslReprMapping);
        data.lexErrors = std::move(lexErrorsNC);
        data.lexWarnings = std::move(lexWarningsNC);
        data.parseErrors = std::move(parseErrors);
        data.parseWarnings = std::move(parseWarnings);
        documentCache[nuri] = std::move(data);
    }

    json handleShutdown(const json& request) {
        isServerShutdown = true;
        return genResponse(
            request["id"],
            json(),
            json()
        );
    }

    json handleExit(const json& request) {
        isServerExited = true;
        isServerInitialized = false;
        return json();
    }

    json handleDidOpen(const json& request) {
        const auto& text = request["params"]["textDocument"]["text"].get<std::string>();
        const auto& uri = request["params"]["textDocument"]["uri"].get<std::string>();
        recomputeDocument(uri, text);
        sendNotification(genPublishDiagnosticsNotification(uri));
        return json();
    }

    json handleDidChange(const json& request) {
        const auto& changes = request["params"]["contentChanges"];
        const auto& uri = request["params"]["textDocument"]["uri"].get<std::string>();
        if (!changes.empty()) {
            const auto& text = changes[changes.size() - 1]["text"].get<std::string>();
            recomputeDocument(uri, text);
            sendNotification(genPublishDiagnosticsNotification(uri));
        }
        return json();
    }

    json handleDidClose(const json& request) {
        const auto& uri = request["params"]["textDocument"]["uri"].get<std::string>();
        documentCache.erase(normalizeUri(uri));
        return json();
    }

    json handleSetTrace(const json& request) {
        traceValue = request["params"]["value"].get<std::string>();
        return json();
    }

    json genDiagnosticsFromErrorWarningList(const std::vector<std::tuple<std::string, FilePosition::Region>>& errors, const std::vector<std::tuple<std::string, FilePosition::Region>>& warnings) {
        json diagnostics = json::Array({});
        for (const auto& error : errors) {
            auto [message, region] = error;
            json start = json::Object({
                json::to_keypair("line", json::Number(region.start.line.getValue())),
                json::to_keypair("character", json::Number(region.start.column.getValue()))
            });
            json end = json::Object({
                json::to_keypair("line", json::Number(region.end.line.getValue())),
                json::to_keypair("character", json::Number(region.end.column.getValue()))
            });
            json range = json::Object({
                json::to_keypair("start", start),
                json::to_keypair("end", end)
            });
            json diag = json::Object({
                json::to_keypair("range", range),
                json::to_keypair("message", message),
                json::to_keypair("severity", json::Number(1))
            });
            diagnostics.push_back(diag);
        }
        for (const auto& warning : warnings) {
            auto [message, region] = warning;
            json start = json::Object({
                json::to_keypair("line", json::Number(region.start.line.getValue())),
                json::to_keypair("character", json::Number(region.start.column.getValue()))
            });
            json end = json::Object({
                json::to_keypair("line", json::Number(region.end.line.getValue())),
                json::to_keypair("character", json::Number(region.end.column.getValue()))
            });
            json range = json::Object({
                json::to_keypair("start", start),
                json::to_keypair("end", end)
            });
            json diag = json::Object({
                json::to_keypair("range", range),
                json::to_keypair("message", message),
                json::to_keypair("severity", json::Number(2))
            });
            diagnostics.push_back(diag);
        }
        return diagnostics;
    }

    json genDiagnosticsForCslFile(const std::string& uri) {
        auto it = documentCache.find(normalizeUri(uri));
        if (it == documentCache.end()) {
            throw std::runtime_error("Document not found");
        }
        std::vector<std::tuple<std::string, FilePosition::Region>> errors;
        std::vector<std::tuple<std::string, FilePosition::Region>> warnings;
        errors.insert(errors.end(), it->second.lexErrors.begin(), it->second.lexErrors.end());
        errors.insert(errors.end(), it->second.parseErrors.begin(), it->second.parseErrors.end());
        warnings.insert(warnings.end(), it->second.lexWarnings.begin(), it->second.lexWarnings.end());
        warnings.insert(warnings.end(), it->second.parseWarnings.begin(), it->second.parseWarnings.end());
        json diagnostics = genDiagnosticsFromErrorWarningList(errors, warnings);
        return diagnostics;
    }

    json genPublishDiagnosticsNotification(const std::string& uri, json diag = json::Array({})) {
        json params = json::Object({
            json::to_keypair("uri", uri),
            json::to_keypair("diagnostics", diag.size() ? diag : genDiagnosticsForCslFile(uri))
        });
        return genNotification("textDocument/publishDiagnostics", params);
    }

    json handlePullDiagnostic(const json& request) {
        const auto& uri = request["params"]["textDocument"]["uri"].get<std::string>();
        auto it = documentCache.find(normalizeUri(uri));
        if (it == documentCache.end()) {
            throw std::runtime_error("Document not found");
        }

        json diagnostics = genDiagnosticsForCslFile(uri);

        json result = json::Object({
            json::to_keypair("kind", "full"),
            json::to_keypair("items", diagnostics)
        });

        return genResponse(
            request["id"],
            result,
            json()
        );
    }

    json handleSemanticTokens(const json& request) {
        const auto& uri = request["params"]["textDocument"]["uri"].get<std::string>();
        auto it = documentCache.find(normalizeUri(uri));
        if (it == documentCache.end()) {
            throw std::runtime_error("Document not found");
        }

        auto& tokenList = it->second.tokensWithComment;
        auto& tokens = tokenList.GetTokenList();

        std::vector<size_t> data;
        size_t prevLine = 0;
        size_t prevChar = 0;

        for (const auto& token : tokens) {
            // Calculate delta positions
            size_t deltaLine = token.range.start.line.getValue() - prevLine;
            size_t deltaChar = (deltaLine == 0) ? token.range.start.column.getValue() - prevChar : token.range.start.column.getValue();

            size_t length = token.range.end.line.getValue() - token.range.start.line.getValue() ? token.value.length() : token.range.end.column.getValue() - token.range.start.column.getValue();
            size_t tokenType = getTokenTypeIndex(token.type, token.prop);

            data.insert(data.end(), { deltaLine, deltaChar, length, tokenType, 0 });

            prevLine = token.range.start.line.getValue();
            prevChar = token.range.start.column.getValue();
        }

        json result = json::Object({
            json::to_keypair("data", json::Array(data))
        });

        return genResponse(
            request["id"],
            result,
            json()
        );
    }

    size_t getTokenTypeIndex(const std::string& type, const std::unique_ptr<Type::Type>& prop) {
        static const std::vector<std::string> types = {
            "datetime", "duration", "number", "boolean", "keyword", "type", "identifier",
            "punctuator", "operator", "comment", "string", "unknown"
        };

        auto it = std::find(types.begin(), types.end(), type);
        return it != types.end() ? std::distance(types.begin(), it) : 8;
    }

    json handleFormatting(const json& request) {
        const auto& uri = request["params"]["textDocument"]["uri"].get<std::string>();
        auto it = documentCache.find(normalizeUri(uri));
        if (it == documentCache.end()) {
            throw std::runtime_error("Document not found");
        }
        auto newCsl = CSL::toCsl(it->second.schemas);
        auto edits = computeEdits(it->second.text, newCsl);

        json result;
        if (edits.empty()) {
            result = json::Object({});
        }
        else {
            result = json::Array({});
            for (auto& edit : edits) {
                json start = json::Object({
                    json::to_keypair("line", json::Number(edit.range.start.line)),
                    json::to_keypair("character", json::Number(edit.range.start.character))
                });
                json end = json::Object({
                    json::to_keypair("line", json::Number(edit.range.end.line)),
                    json::to_keypair("character", json::Number(edit.range.end.character))
                });
                json range = json::Object({
                    json::to_keypair("start", start),
                    json::to_keypair("end", end)
                });
                json change = json::Object({
                    json::to_keypair("range", range),
                    json::to_keypair("newText", edit.newText)
                });
                result.push_back(change);
            }
        }

        return genResponse(
            request["id"],
            result,
            json()
        );
    }

    json handleGenerateHtmlDoc(const json& request) {
        const auto& td = request["params"]["textDocument"];
        const auto& uri = td["uri"].get<std::string>();
        auto it = documentCache.find(normalizeUri(uri));
        std::unordered_map<std::string, std::string> files;
        bool reuseExisting = !jsonio::hasKey(td, "reuseExisting");
        if (!reuseExisting) {
            std::string text = td["text"].get<std::string>();
            reuseExisting = it != documentCache.end() && it->second.text == text;
            if (!reuseExisting) {
                auto [tokensNC, lexErrorsNC, lexWarningsNC] = cslLexer(text, false, clientSupportsMultilineToken);
                auto [schemas, parseErrors, parseWarnings, tokenCslReprMapping] = cslParser(tokensNC);
                files = CSL::toHtmlDoc(schemas);
            }
        }
        if (reuseExisting) {
            if (it == documentCache.end()) {
                throw std::runtime_error("Document not found");
            }
            files = CSL::toHtmlDoc(it->second.schemas);
        }
        json result = json::Object({});
        for (const auto& kv : files) {
            result.push_back(json::KeyPair{ kv.first, json::String(kv.second) });
        }
        return genResponse(
            request["id"],
            result,
            json()
        );
    }

    json handleDefinition(const json& request) {
        const auto& uri = request["params"]["textDocument"]["uri"].get<std::string>();
        auto it = documentCache.find(normalizeUri(uri));
        if (it == documentCache.end()) {
            throw std::runtime_error("Document not found");
        }
        FilePosition::Position position = { { request["params"]["position"]["line"].get<size_t>(), false }, { request["params"]["position"]["character"].get<size_t>(), false } };
        auto& tokenList = it->second.tokensNoComment;
        auto& tokenCslReprMapping = it->second.tokenCslReprMapping;
		json definition = json::Object({});
        for (auto tokenListIterator = tokenList.begin(); tokenListIterator != tokenList.end(); ++tokenListIterator) {
            const auto& token = *tokenListIterator;
            if (token.range.contains(position)) {
                auto tokenIndex = std::distance(tokenList.begin(), tokenListIterator);
                if (tokenCslReprMapping.find(tokenIndex) == tokenCslReprMapping.end()) {
                    continue;
                }
                auto targetKey = tokenCslReprMapping[tokenIndex];
                if (std::holds_alternative<std::shared_ptr<CSL::ConfigSchema>>(targetKey)) {
                    auto schema = std::get<std::shared_ptr<CSL::ConfigSchema>>(targetKey);
                    auto region = schema->getNameRegion();
                    json start = json::Object({
                        json::to_keypair("line", json::Number(region.start.line.getValue())),
                        json::to_keypair("character", json::Number(region.start.column.getValue()))
                    });
                    json end = json::Object({
                        json::to_keypair("line", json::Number(region.end.line.getValue())),
                        json::to_keypair("character", json::Number(region.end.column.getValue()))
                    });
                    json range = json::Object({
                        json::to_keypair("start", start),
                        json::to_keypair("end", end)
                    });
                    definition = json::Object({
                        json::to_keypair("uri", uri),
                        json::to_keypair("range", range)
                    });
                }
                else if (std::holds_alternative<std::shared_ptr<CSL::TableType::KeyDefinition>>(targetKey)) {
                    auto keyDef = std::get<std::shared_ptr<CSL::TableType::KeyDefinition>>(targetKey);
                    auto region = keyDef->getNameRegion();
                    json start = json::Object({
                        json::to_keypair("line", json::Number(region.start.line.getValue())),
                        json::to_keypair("character", json::Number(region.start.column.getValue()))
                    });
                    json end = json::Object({
                        json::to_keypair("line", json::Number(region.end.line.getValue())),
                        json::to_keypair("character", json::Number(region.end.column.getValue()))
                    });
                    json range = json::Object({
                        json::to_keypair("start", start),
                        json::to_keypair("end", end)
                    });
                    definition = json::Object({
                        json::to_keypair("uri", uri),
                        json::to_keypair("range", range)
                    });
                }
            }
        }
        return genResponse(
            request["id"],
            definition,
            json()
        );
    }
    
    std::string backtickIfNeeded(const std::string& s) {
        static const std::regex ident(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
        return std::regex_match(s, ident) ? s : ("`" + s + "`");
    }

    std::shared_ptr<CSL::TableType> findDeepestTableTypeAtPosition(
        const std::vector<std::shared_ptr<CSL::ConfigSchema>>& schemas,
        const FilePosition::Position& position) {
        std::function<std::shared_ptr<CSL::TableType>(const std::shared_ptr<CSL::CSLType>&)> descend;
        descend = [&](const std::shared_ptr<CSL::CSLType>& type) -> std::shared_ptr<CSL::TableType> {
            if (!type) return nullptr;
            if (!type->getRegion().contains(position)) return nullptr;
            switch (type->getKind()) {
                case CSL::CSLType::Kind::Table: {
                    auto table = std::static_pointer_cast<CSL::TableType>(type);
                    auto deepest = table;
                    for (const auto& k : table->getExplicitKeys()) {
                        const auto& t = k->getType();
                        auto cand = descend(t);
                        if (cand) deepest = cand;
                    }
                    if (table->getWildcardKey()) {
                        auto cand = descend(table->getWildcardKey()->getType());
                        if (cand) deepest = cand;
                    }
                    return deepest;
                }
                case CSL::CSLType::Kind::Array: {
                    auto arr = std::static_pointer_cast<CSL::ArrayType>(type);
                    return descend(arr->getElementType());
                }
                case CSL::CSLType::Kind::Union: {
                    auto uni = std::static_pointer_cast<CSL::UnionType>(type);
                    std::shared_ptr<CSL::TableType> deepest;
                    for (const auto& mt : uni->getMemberTypes()) {
                        auto cand = descend(mt);
                        if (cand) deepest = cand;
                    }
                    return deepest;
                }
                default:
                    return nullptr;
            }
        };
        std::shared_ptr<CSL::TableType> best;
        for (const auto& schema : schemas) {
            if (!schema) continue;
            const auto& root = schema->getRootTable();
            if (!root) continue;
            auto cand = descend(root);
            if (cand) {
                if (!best) {
                    best = cand;
                } else {
                    auto bSpan = best->getRegion().lineSpan();
                    auto cSpan = cand->getRegion().lineSpan();
                    if (cSpan < bSpan || (cSpan == bSpan && cand->getRegion().colSpan() < best->getRegion().colSpan())) {
                        best = cand;
                    }
                }
            }
        }
        return best;
    }

    json handleCompletion(const json& request) {
        const auto& uri = request["params"]["textDocument"]["uri"].get<std::string>();
        auto it = documentCache.find(normalizeUri(uri));
        if (it == documentCache.end()) {
            throw std::runtime_error("Document not found");
        }
        FilePosition::Position position = { { request["params"]["position"]["line"].get<size_t>(), false }, { request["params"]["position"]["character"].get<size_t>(), false } };
        auto& tokenList = it->second.tokensNoComment;
        auto& schemas = it->second.schemas;
        auto& tokenCslReprMapping = it->second.tokenCslReprMapping;
        auto completions = json::Array({});
        auto addCompletion = [&](const std::string& label, int kind, const std::string& detail, const std::string& insertText) {
            json completionItem = json::Object({
                json::to_keypair("label", label),
                json::to_keypair("kind", json::Number(kind)),
                json::to_keypair("detail", detail),
                json::to_keypair("insertText", insertText)
            });
            completions.push_back(completionItem);
        };
        auto addKeyCompletion = [&](const std::string& keyLabel, const std::shared_ptr<CSL::TableType::KeyDefinition>& keyDef) {
            addCompletion(
                keyLabel,
                6,
                std::string(keyDef->getIsOptional() ? "Optional" : "Mandatory") + " key in schema",
                backtickIfNeeded(keyLabel)
            );
        };
        auto buildKeywordTypePairs = [&](const std::string& input) {
            std::unordered_map<std::string, std::tuple<int, std::string, std::string>> map;
            // Keywords
            std::vector<std::string> keywords = {
                "config", "constraints", "requires", "conflicts", "with", "validate",
                "exists", "count_keys", "all_keys", "wildcard_keys", "subset", "*"
            };
            for (const auto& k : keywords) {
                map[k] = std::make_tuple(14, std::string("Keyword"), k);
            }
            // Built-in types
            std::vector<std::string> types = { "any{}", "any[]", "string", "number", "boolean", "datetime", "duration" };
            for (const auto& t : types) {
                map[t] = std::make_tuple(25, std::string("Built-in type"), t);
            }
            return findPairs(map, input);
        };
        std::unordered_set<std::string> seenLabels;
        for (auto tokenListIterator = tokenList.begin(); tokenListIterator != tokenList.end(); ++tokenListIterator) {
            const auto& token = *tokenListIterator;
            if (token.range.contains(position)) {
                auto tokenIndex = std::distance(tokenList.begin(), tokenListIterator);
                std::vector<std::pair<std::string, std::shared_ptr<CSL::TableType::KeyDefinition>>> completionKeyPairs;
                if (tokenListIterator != tokenList.begin() && token.value == ".") {
                    auto targetKey = tokenCslReprMapping[std::distance(tokenList.begin(), std::prev(tokenListIterator))];
                    if (std::holds_alternative<std::shared_ptr<CSL::TableType::KeyDefinition>>(targetKey)) {
                        auto keyType = std::get<std::shared_ptr<CSL::TableType::KeyDefinition>>(targetKey)->getType();
                        if (keyType->getKind() == CSL::CSLType::Kind::Table) {
                            auto explicitKeys = std::dynamic_pointer_cast<CSL::TableType>(keyType)->getExplicitKeys();
                            for (const auto& keyDef : explicitKeys) {
                                completionKeyPairs.push_back({ keyDef->getName(), keyDef });
                            }
                        }
                    }
                }
                else {
                    auto tableType = findDeepestTableTypeAtPosition(schemas, position);
                    if (tableType) {
                        auto explicitKeys = tableType->getExplicitKeys();
                        std::unordered_map<std::string, std::shared_ptr<CSL::TableType::KeyDefinition>> keyNameKeyDefMapping;
                        for (const auto& keyDef : explicitKeys) {
                            keyNameKeyDefMapping[keyDef->getName()] = keyDef;
                        }
                        auto foundKeyPairs = findPairs(keyNameKeyDefMapping, token.value);
                        completionKeyPairs.insert(completionKeyPairs.end(), foundKeyPairs.begin(), foundKeyPairs.end());
                    }
                    // Also suggest keywords and built-in types
                    const auto& keywordTypePairs = buildKeywordTypePairs(token.value);
                    for (const auto& kv : keywordTypePairs) {
                        const auto& lbl = kv.first;
                        const auto& meta = kv.second;
                        if (seenLabels.insert(lbl).second) {
                            addCompletion(lbl, std::get<0>(meta), std::get<1>(meta), std::get<2>(meta));
                        }
                    }
                }
                for (auto& completionKeyPair : completionKeyPairs) {
                    auto completionKeyId = completionKeyPair.first;
                    auto completionKeyValue = completionKeyPair.second;
                    if (seenLabels.insert(completionKeyId).second) {
                        addKeyCompletion(completionKeyId, completionKeyValue);
                    }
                }
            }
            else if (token.range.end < position && (std::next(tokenListIterator) == tokenList.end() || std::next(tokenListIterator)->range.start > position)) {
                auto tableType = findDeepestTableTypeAtPosition(schemas, position);
                if (tableType) {
                    auto explicitKeys = tableType->getExplicitKeys();
                    std::unordered_map<std::string, std::shared_ptr<CSL::TableType::KeyDefinition>> keyNameKeyDefMapping;
                    for (const auto& keyDef : explicitKeys) {
                        keyNameKeyDefMapping[keyDef->getName()] = keyDef;
                    }
                    auto completionKeyPairs = findPairs(keyNameKeyDefMapping, token.value);
                    for (auto& completionKeyPair : completionKeyPairs) {
                        auto completionKeyId = completionKeyPair.first;
                        auto completionKeyValue = completionKeyPair.second;
                        if (seenLabels.insert(completionKeyId).second) {
                            addKeyCompletion(completionKeyId, completionKeyValue);
                        }
                    }
                }
                // Also suggest keywords and built-in types
                for (const auto& kv : buildKeywordTypePairs(token.value)) {
                    const auto& lbl = kv.first;
                    const auto& meta = kv.second;
                    if (seenLabels.insert(lbl).second) {
                        addCompletion(lbl, std::get<0>(meta), std::get<1>(meta), std::get<2>(meta));
                    }
                }
            }
        }
        json result;
        if (completions.size()) {
            result = json::Object({
                json::to_keypair("isIncomplete", false),
                json::to_keypair("items", completions)
            });
        }
        else {
            result = json::Object({});
        }
        return genResponse(
            request["id"],
            result,
            json()
        );
    }

    json handleHover(const json& request) {
        const auto& uri = request["params"]["textDocument"]["uri"].get<std::string>();
        auto it = documentCache.find(normalizeUri(uri));
        if (it == documentCache.end()) {
            throw std::runtime_error("Document not found");
        }
        FilePosition::Position position = { { request["params"]["position"]["line"].get<size_t>(), false }, { request["params"]["position"]["character"].get<size_t>(), false } };
        auto& tokenList = it->second.tokensNoComment;
        auto& tokenCslReprMapping = it->second.tokenCslReprMapping;
        auto hover = json::Object({});
        for (auto tokenListIterator = tokenList.begin(); tokenListIterator != tokenList.end(); ++tokenListIterator) {
            const auto& token = *tokenListIterator;
            const auto tokenRange = token.range;
            if (tokenRange.contains(position)) {
                auto tokenIndex = std::distance(tokenList.begin(), tokenListIterator);
                if (tokenCslReprMapping.find(tokenIndex) == tokenCslReprMapping.end()) {
                    continue;
                }
                auto targetKey = tokenCslReprMapping[tokenIndex];
                if (std::holds_alternative<std::shared_ptr<CSL::ConfigSchema>>(targetKey)) {
                    auto schema = std::get<std::shared_ptr<CSL::ConfigSchema>>(targetKey);
                    std::string markdown = "## **Schema** " + schema->getName() + "\n";
                    markdown += "- **Defined At**: ln " + std::to_string(schema->getRegion().start.line.getValue() + 1) + ", col " + std::to_string(schema->getRegion().start.column.getValue() + 1);
                    json contents = json::Object({
                        json::to_keypair("kind", "markdown"),
                        json::to_keypair("value", markdown)
                    });
                    json start = json::Object({
                        json::to_keypair("line", json::Number(tokenRange.start.line.getValue())),
                        json::to_keypair("character", json::Number(tokenRange.start.column.getValue()))
                    });
                    json end = json::Object({
                        json::to_keypair("line", json::Number(tokenRange.end.line.getValue())),
                        json::to_keypair("character", json::Number(tokenRange.end.column.getValue()))
                    });
                    json range = json::Object({
                        json::to_keypair("start", start),
                        json::to_keypair("end", end)
                    });
                    hover = json::Object({
                        json::to_keypair("contents", contents),
                        json::to_keypair("range", range)
                    });
                }
                else if (std::holds_alternative<std::shared_ptr<CSL::TableType::KeyDefinition>>(targetKey)) {
                    auto keyDef = std::get<std::shared_ptr<CSL::TableType::KeyDefinition>>(targetKey);
                    std::string keyTypeStr;
                    switch (keyDef->getType()->getKind()) {
                        case CSL::CSLType::Kind::Primitive:
                            {
                                auto primType = std::static_pointer_cast<CSL::PrimitiveType>(keyDef->getType());
                                switch (primType->getPrimitive()) {
                                    case CSL::PrimitiveType::Primitive::String:
                                        keyTypeStr = "String";
                                        break;
                                    case CSL::PrimitiveType::Primitive::Number:
                                        keyTypeStr = "Number";
                                        break;
                                    case CSL::PrimitiveType::Primitive::Boolean:
                                        keyTypeStr = "Boolean";
                                        break;
                                    case CSL::PrimitiveType::Primitive::Datetime:
                                        keyTypeStr = "Datetime";
                                        break;
                                    case CSL::PrimitiveType::Primitive::Duration:
                                        keyTypeStr = "Duration";
                                        break;
                                }
                            }
                            break;
                        case CSL::CSLType::Kind::Table:
                            keyTypeStr = "Table";
                            break;
                        case CSL::CSLType::Kind::Array:
                            keyTypeStr = "Array";
                            break;
                        case CSL::CSLType::Kind::Union:
                            keyTypeStr = "Union";
                            break;
                        case CSL::CSLType::Kind::AnyTable:
                            keyTypeStr = "Any Table";
                            break;
                        case CSL::CSLType::Kind::AnyArray:
                            keyTypeStr = "Any Array";
                            break;
                        default:
                            keyTypeStr = "Value";
                            break;
                    }
                    std::string markdown = "## ";
                    if (keyDef->getIsWildcard()){
                        markdown += "Wildcard **" + keyTypeStr + "**\n";
                    }
                    else {
                        markdown += "**" + keyTypeStr + "** " + keyDef->getName() + "\n";
                    }
                    if (keyDef->getIsOptional()) {
                        markdown += "- **Optional** key\n";
                    }
                    markdown += "- **Defined At**: ln " + std::to_string(keyDef->getNameRegion().start.line.getValue() + 1) + ", col " + std::to_string(keyDef->getNameRegion().start.column.getValue() + 1) + "\n";
                    if (keyDef->getDefaultValue().has_value()) {
                        markdown += "- **Default Value**: " + keyDef->getDefaultValue().value().first;
                    }
                    json contents = json::Object({
                        json::to_keypair("kind", "markdown"),
                        json::to_keypair("value", markdown)
                    });
                    json start = json::Object({
                        json::to_keypair("line", json::Number(tokenRange.start.line.getValue())),
                        json::to_keypair("character", json::Number(tokenRange.start.column.getValue()))
                    });
                    json end = json::Object({
                        json::to_keypair("line", json::Number(tokenRange.end.line.getValue())),
                        json::to_keypair("character", json::Number(tokenRange.end.column.getValue()))
                    });
                    json range = json::Object({
                        json::to_keypair("start", start),
                        json::to_keypair("end", end)
                    });
                    hover = json::Object({
                        json::to_keypair("contents", contents),
                        json::to_keypair("range", range)
                    });
                }
            }
        }
        return genResponse(
            request["id"],
            hover,
            json()
        );
    }

    json handleReferences(const json& request) {
        const auto& uri = request["params"]["textDocument"]["uri"].get<std::string>();
        auto it = documentCache.find(normalizeUri(uri));
        if (it == documentCache.end()) {
            throw std::runtime_error("Document not found");
        }
        bool includeDeclaration = request["params"]["context"]["includeDeclaration"].get<bool>();
        FilePosition::Position position = { { request["params"]["position"]["line"].get<size_t>(), false }, { request["params"]["position"]["character"].get<size_t>(), false } };
        auto& tokenList = it->second.tokensNoComment;
        auto& tokenCslReprMapping = it->second.tokenCslReprMapping;
        auto references = json::Array({});
        std::unordered_map<ReprPtr, std::vector<FilePosition::Region>> referencesMap;
        ReprPtr targetKey;
        bool keyFound = false;
        for (auto tokenListIterator = tokenList.begin(); tokenListIterator != tokenList.end(); ++tokenListIterator) {
            const auto& token = *tokenListIterator;
            auto tokenIndex = std::distance(tokenList.begin(), tokenListIterator);
            if (tokenCslReprMapping.find(tokenIndex) == tokenCslReprMapping.end()) {
                continue;
            }
            auto curKey = tokenCslReprMapping[tokenIndex];
            referencesMap[curKey].push_back(token.range);
            if (token.range.contains(position)) {
                targetKey = curKey;
                keyFound = true;
            }
        }
        FilePosition::Region targetKeyDefRegion;
        std::shared_ptr<CSL::ConfigSchema> targetSchema;
        std::shared_ptr<CSL::TableType::KeyDefinition> targetKeyDef;
        if (keyFound) {
            if (std::holds_alternative<std::shared_ptr<CSL::ConfigSchema>>(targetKey)) {
                targetSchema = std::get<std::shared_ptr<CSL::ConfigSchema>>(targetKey);
                targetKeyDefRegion = targetSchema->getRegion();
            }
            else if (std::holds_alternative<std::shared_ptr<CSL::TableType::KeyDefinition>>(targetKey)) {
                targetKeyDef = std::get<std::shared_ptr<CSL::TableType::KeyDefinition>>(targetKey);
                targetKeyDefRegion = targetKeyDef->getNameRegion();
            }
        }
        if (targetSchema || targetKeyDef) {
            for (const auto& keyRef : referencesMap[targetKey]) {
                if (!includeDeclaration && keyRef == targetKeyDefRegion) {
                    continue;
                }
                json start = json::Object({
                    json::to_keypair("line", json::Number(keyRef.start.line.getValue())),
                    json::to_keypair("character", json::Number(keyRef.start.column.getValue()))
                });
                json end = json::Object({
                    json::to_keypair("line", json::Number(keyRef.end.line.getValue())),
                    json::to_keypair("character", json::Number(keyRef.end.column.getValue()))
                });
                json range = json::Object({
                    json::to_keypair("start", start),
                    json::to_keypair("end", end)
                });
                json reference = json::Object({
                    json::to_keypair("uri", uri),
                    json::to_keypair("range", range)
                });
                references.push_back(reference);
            }
        }
        return genResponse(
            request["id"],
            references,
            json()
        );
    }

    json handleRename(const json& request) {
        const auto& uri = request["params"]["textDocument"]["uri"].get<std::string>();
        auto it = documentCache.find(normalizeUri(uri));
        if (it == documentCache.end()) {
            throw std::runtime_error("Document not found");
        }
        auto newName = backtickIfNeeded(request["params"]["newName"].get<std::string>());
        FilePosition::Position position = { { request["params"]["position"]["line"].get<size_t>(), false }, { request["params"]["position"]["character"].get<size_t>(), false } };
        auto& tokenList = it->second.tokensNoComment;
        auto& tokenCslReprMapping = it->second.tokenCslReprMapping;
        std::unordered_map<ReprPtr, std::vector<FilePosition::Region>> referencesMap;
        ReprPtr targetKey;
        bool keyFound = false;
        for (auto tokenListIterator = tokenList.begin(); tokenListIterator != tokenList.end(); ++tokenListIterator) {
            const auto& token = *tokenListIterator;
            auto tokenIndex = std::distance(tokenList.begin(), tokenListIterator);
            if (tokenCslReprMapping.find(tokenIndex) == tokenCslReprMapping.end()) {
                continue;
            }
            auto curKey = tokenCslReprMapping[tokenIndex];
            referencesMap[curKey].push_back(token.range);
            if (token.range.contains(position)) {
                targetKey = curKey;
                keyFound = true;
            }
        }
        json result;
        std::shared_ptr<CSL::ConfigSchema> targetSchema;
        std::shared_ptr<CSL::TableType::KeyDefinition> targetKeyDef;
        if (keyFound) {
            if (std::holds_alternative<std::shared_ptr<CSL::ConfigSchema>>(targetKey)) {
                targetSchema = std::get<std::shared_ptr<CSL::ConfigSchema>>(targetKey);
            }
            else if (std::holds_alternative<std::shared_ptr<CSL::TableType::KeyDefinition>>(targetKey)) {
                targetKeyDef = std::get<std::shared_ptr<CSL::TableType::KeyDefinition>>(targetKey);
            }
        }
        if (targetSchema || targetKeyDef) {
            json changes = json::Array({});
            for (const auto& ref : referencesMap[targetKey]) {
                json start = json::Object({
                    json::to_keypair("line", json::Number(ref.start.line.getValue())),
                    json::to_keypair("character", json::Number(ref.start.column.getValue()))
                });
                json end = json::Object({
                    json::to_keypair("line", json::Number(ref.end.line.getValue())),
                    json::to_keypair("character", json::Number(ref.end.column.getValue()))
                });
                json range = json::Object({
                    json::to_keypair("start", start),
                    json::to_keypair("end", end)
                });
                json change = json::Object({
                    json::to_keypair("range", range),
                    json::to_keypair("newText", newName)
                });
                changes.push_back(change);
            }
            json changesObj = json::Object({ json::KeyPair{ uri, changes } });
            result = json::Object({ json::to_keypair("changes", changesObj) });
        }
        else {
            result = json::Object({});
        }
        return genResponse(
            request["id"],
            result,
            json()
        );
    }

    json handleFoldingRange(const json& request) {
        const auto& uri = request["params"]["textDocument"]["uri"].get<std::string>();
        auto it = documentCache.find(normalizeUri(uri));
        if (it == documentCache.end()) {
            throw std::runtime_error("Document not found");
        }
        auto& tokenList = it->second.tokensNoComment;
        auto ranges = json::Array({});
        std::stack<FilePosition::Position> braceStack;
        for (auto tokenListIterator = tokenList.begin(); tokenListIterator != tokenList.end(); ++tokenListIterator) {
            if (tokenListIterator->value == "{") {
                braceStack.push(tokenListIterator->range.start);
            }
            else if (tokenListIterator->value == "}" && !braceStack.empty()) {
                auto startPosition = braceStack.top();
                braceStack.pop();
                auto endPosition = tokenListIterator->range.end;
                if (startPosition.line == endPosition.line) {
                    continue;
                }
                json range = json::Object({
                    json::to_keypair("startLine", json::Number(startPosition.line.getValue())),
                    json::to_keypair("startCharacter", json::Number(startPosition.column.getValue())),
                    json::to_keypair("endLine", json::Number(endPosition.line.getValue())),
                    json::to_keypair("endCharacter", json::Number(endPosition.column.getValue())),
                    json::to_keypair("kind", "range")
                });
                ranges.push_back(range);
            }
        }
        for (auto tokenListIterator = tokenList.begin(); tokenListIterator != tokenList.end() && std::next(tokenListIterator) != tokenList.end(); ++tokenListIterator) {
            const auto& token = *tokenListIterator;
            if (token.type == "comment") {
                auto startPosition = token.range.start;
                for (; tokenListIterator != tokenList.end(); ++tokenListIterator) {
                    if (std::next(tokenListIterator) == tokenList.end() || std::next(tokenListIterator)->type != "comment") {
                        break;
                    }
                }
                if (tokenListIterator == tokenList.end()) {
                    break;
                }
                auto endPosition = tokenListIterator->range.end;
                if (startPosition.line == endPosition.line) {
                    continue;
                }
                json range = json::Object({
                    json::to_keypair("startLine", json::Number(startPosition.line.getValue())),
                    json::to_keypair("startCharacter", json::Number(startPosition.column.getValue())),
                    json::to_keypair("endLine", json::Number(endPosition.line.getValue())),
                    json::to_keypair("endCharacter", json::Number(endPosition.column.getValue())),
                    json::to_keypair("kind", "comment")
                });
                ranges.push_back(range);
            }
        }
        return genResponse(
            request["id"],
            ranges,
            json()
        );
    }
};

int CslLangSvrMain(
    std::istream& inChannel,
    std::ostream& outChannel,
    const CslLexerFunctionWithStringInput& cslLexer,
    const CslParserFunction& cslParser
) {
    LanguageServer server(inChannel, outChannel, cslLexer, cslParser);
    return server.run();
}
