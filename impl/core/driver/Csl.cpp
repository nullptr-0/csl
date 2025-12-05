#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <functional>
#include <memory>
#include "../shared/Components.h"

#ifndef STDIO_ONLY
#include "../shared/UniSock.hpp"
#include "../shared/UniPipe.hpp"
#endif // !STDIO_ONLY

#ifdef EMSCRIPTEN
#include "../shared/BlockingInput.h"
BlockingStdinStream cbin;
#endif

//#define DEBUG

int main(int argc, char* argv[]) {
    std::vector<std::string> argVector(argv, argv + argc);
    auto printInfo = [](std::ostream& stream) {
        stream << "csl: A Config Schema Language Utility\n";
        stream << "Built at: " __TIME__ " " __DATE__ << "\n";
        stream << "Copyright (C) 2023-2025 nullptr-0.\n";
        stream.flush();
    };
    auto printHelp = [&argVector](std::ostream& stream) {
        stream << "Usage:\n"
            << argVector[0] << " --htmldoc <path_file> <path_dir>\n"
            << "    Generate HTML documentation in <path_dir> directory for the config schema file <path_file>.\n"
            << argVector[0] << " --test <path>\n"
            << "    Test the config schema file <path> for correctness.\n"
            << argVector[0] << " --langsvr --stdio\n"
            << "    Start a language server instance on standard IO.\n"
#ifndef STDIO_ONLY
            << argVector[0] << " --langsvr --socket=<port>\n"
            << argVector[0] << " --langsvr --socket <port>\n"
            << argVector[0] << " --langsvr --port=<port>\n"
            << argVector[0] << " --langsvr --port <port>\n"
            << "    Start a language server instance on specified port.\n"
            << argVector[0] << " --langsvr --pipe=<pipe>\n"
            << argVector[0] << " --langsvr --pipe <pipe>\n"
            << "    Start a language server instance on specified named pipe.\n"
#endif // !STDIO_ONLY
            << argVector[0] << " --help\n"
            << argVector[0] << " -h\n"
            << "    Print this help message.\n";
    };
    if (argc >= 3 && argVector[1] == "--langsvr") {
        auto cslStringLexer = [](const std::string& input, bool preserveComment, bool multilineToken) -> std::tuple<Token::TokenList<std::string, std::unique_ptr<Type::Type>>, std::vector<std::tuple<std::string, FilePosition::Region>>, std::vector<std::tuple<std::string, FilePosition::Region>>> {
            std::istringstream stream(input);
            return CslLexerMain(stream, preserveComment, multilineToken);
        };
        int retVal = 1;
        if (argc == 3 && argVector[2] == "--stdio") {
            retVal = CslLangSvrMain(
#ifdef EMSCRIPTEN
                cbin
#else
                std::cin
#endif
                , std::cout, cslStringLexer, CslParserMain);
        }
#ifndef STDIO_ONLY
        else if (argc >= 3 && (argVector[2].substr(0, 6) == "--port" || argVector[2].substr(0, 8) == "--socket")) {
            size_t optionPrefixLength = argVector[2].substr(0, 6) == "--port" ? 6 : 8;
#ifndef DEBUG
            try {
#endif // DEBUG
                std::string port;
                if (argc == 4 && argVector[2].size() == optionPrefixLength) {
                    port = argVector[3];
                }
                else if (argc == 3) {
                    port = argVector[2].substr(optionPrefixLength + 1);
                }
                else {
                    throw std::invalid_argument("invalid arguments");
                }
                socketstream socket("127.0.0.1", atoi(port.c_str()), socketstream::client);
                if (!socket.is_open()) {
                    throw std::runtime_error("unable to open socket on port " + port);
                    return 2;
                }
                retVal = CslLangSvrMain(socket, socket, cslStringLexer, CslParserMain);
#ifndef DEBUG
            }
            catch (const std::exception& e) {
                std::cerr << e.what() << '\n';
                return 1;
            }
#endif // DEBUG
        }
        else if (argc >= 3 && (argVector[2].substr(0, 6) == "--pipe")) {
#ifndef DEBUG
            try {
#endif // DEBUG
                std::string pipeName;
                if (argc == 4 && argVector[2].size() == 6) {
                    pipeName = argVector[3];
                }
                else if (argc == 3) {
                    pipeName = argVector[2].substr(7);
                }
                else {
                    throw std::invalid_argument("invalid arguments");
                }
                pipestream pipe(pipeName, NamedPipeDescriptor::client);
                if (!pipe.is_open()) {
                    throw std::runtime_error("unable to open pipe " + pipeName);
                }
                retVal = CslLangSvrMain(pipe, pipe, cslStringLexer, CslParserMain);
#ifndef DEBUG
            }
            catch (const std::exception& e) {
                std::cerr << e.what() << '\n';
                return 1;
            }
#endif // DEBUG
        }
#endif // !STDIO_ONLY
        else {
            printInfo(std::cerr);
            std::cerr << "invalid arguments:";
            for (const auto& arg : argVector) {
                std::cerr << " " << arg;
            }
            std::cerr << "\n";
            return 2;
        }
        return retVal;
    }
    else if (argc == 4 && argVector[1] == "--htmldoc") {
        int retVal = 0;
        const auto inputPath = argVector[2];
        const auto outputDir = argVector[3];
        printInfo(std::cout);
        std::list<std::unique_ptr<std::fstream>> openedFileStreams;
        auto getStreamForDiskFile = [&openedFileStreams](const std::string& path, std::ios_base::openmode mode) -> std::iostream* {
            if ((mode & std::ios::out) && !std::filesystem::exists(path)) {
                std::ofstream(path, std::ios::out).close();
            }
            auto file = std::make_unique<std::fstream>(path, mode);
            if (!file->is_open()) {
                throw std::runtime_error("unable to open " + path);
            }
            openedFileStreams.push_back(std::move(file));
            return openedFileStreams.back().get();
        };
        if (std::filesystem::is_regular_file(inputPath)) {
#ifndef DEBUG
            try {
#endif // DEBUG
                auto inputStream = getStreamForDiskFile(inputPath, std::ios::in);
                std::vector<std::tuple<std::string, FilePosition::Region>> errors;
                std::vector<std::tuple<std::string, FilePosition::Region>> warnings;
                auto [cslTokenList, cslLexErrors, cslLexWarnings] = CslLexerMain(*inputStream, false);
                auto [schemas, cslParseErrors, cslParseWarnings, tokenCslReprMapping] = CslParserMain(cslTokenList);
                errors.insert(errors.end(), cslLexErrors.begin(), cslLexErrors.end());
                errors.insert(errors.end(), cslParseErrors.begin(), cslParseErrors.end());
                warnings.insert(warnings.end(), cslLexWarnings.begin(), cslLexWarnings.end());
                warnings.insert(warnings.end(), cslParseWarnings.begin(), cslParseWarnings.end());
                if (errors.size()) {
                    std::cerr << "\nErrors in " << inputPath << ":\n";
                    for (const auto& error : errors) {
                        auto errorStart = std::get<1>(error).start;
                        std::cerr << "Error (line " << errorStart.line << ", col " << errorStart.column << "): " << std::get<0>(error) << "\n";
                    }
                    retVal = 1;
                }
                if (warnings.size()) {
                    std::cerr << "\nWarnings in " << inputPath << ":\n";
                    for (const auto& warning : warnings) {
                        auto warningStart = std::get<1>(warning).start;
                        std::cerr << "Warning (line " << warningStart.line << ", col " << warningStart.column << "): " << std::get<0>(warning) << "\n";
                    }
                }
                if (!retVal) {
                    std::filesystem::create_directories(outputDir);
                    auto pages = CSL::toHtmlDoc(schemas);
                    for (const auto& kv : pages) {
                        auto outPath = (std::filesystem::path(outputDir) / kv.first).string();
                        auto outStream = getStreamForDiskFile(outPath, std::ios::out);
                        (*outStream) << kv.second;
                    }
                    std::cout << "generated " << pages.size() << " file(s) in " << outputDir << "\n";
                }
#ifndef DEBUG
            }
            catch (const std::exception& e) {
                std::cerr << e.what() << '\n';
                return 1;
            }
#endif // DEBUG
        }
        else {
            printInfo(std::cerr);
            std::cerr << "file " << inputPath << " is not valid\n";
            return 1;
        }
        for (auto& openedFileStream : openedFileStreams) {
            openedFileStream->flush();
            openedFileStream->close();
        }
        return retVal;
    }
    else if (argc >= 3 && argVector[1] == "--test") {
        int retVal = 0;
        std::string outputPath;
        for (size_t i = 3; i < argVector.size(); ++i) {
            const std::string& arg = argv[i];

            printInfo(std::cerr);
            std::cerr << "invalid arguments:" << arg;
            for (const auto& arg : argVector) {
                std::cerr << " " << arg;
            }
            std::cerr << "\n";
            return 2;
        }
        const auto inputPath = argVector[2];
        printInfo(std::cout);
        std::list<std::unique_ptr<std::fstream>> openedFileStreams;
        auto getStreamForDiskFile = [&openedFileStreams](const std::string& path, std::ios_base::openmode mode) -> std::iostream* {
            if ((mode & std::ios::out) && !std::filesystem::exists(path)) {
                std::ofstream(path, std::ios::out).close();
            }
            auto file = std::make_unique<std::fstream>(path, mode);
            if (!file->is_open()) {
                throw std::runtime_error("unable to open " + path);
            }
            openedFileStreams.push_back(std::move(file));
            return openedFileStreams.back().get();
        };
        if (std::filesystem::is_regular_file(inputPath)) {
#ifndef DEBUG
            try {
#endif // DEBUG
                auto inputStream = getStreamForDiskFile(inputPath, std::ios::in);
                std::vector<std::tuple<std::string, FilePosition::Region>> errors;
                std::vector<std::tuple<std::string, FilePosition::Region>> warnings;
                auto [cslTokenList, cslLexErrors, cslLexWarnings] = CslLexerMain(*inputStream, false);
                auto [schemas, cslParseErrors, cslParseWarnings, tokenCslReprMapping] = CslParserMain(cslTokenList);
                errors.insert(errors.end(), cslLexErrors.begin(), cslLexErrors.end());
                errors.insert(errors.end(), cslParseErrors.begin(), cslParseErrors.end());
                warnings.insert(warnings.end(), cslLexWarnings.begin(), cslLexWarnings.end());
                warnings.insert(warnings.end(), cslParseWarnings.begin(), cslParseWarnings.end());
                if (errors.size()) {
                    std::cerr << "\nErrors in " << inputPath << ":\n";
                    for (const auto& error : errors) {
                        auto errorStart = std::get<1>(error).start;
                        std::cerr << "Error (line " << errorStart.line << ", col " << errorStart.column << "): " << std::get<0>(error) << "\n";
                    }
                }
                if (warnings.size()) {
                    std::cerr << "\nWarnings in " << inputPath << ":\n";
                    for (const auto& warning : warnings) {
                        auto warningStart = std::get<1>(warning).start;
                        std::cerr << "Warning (line " << warningStart.line << ", col " << warningStart.column << "): " << std::get<0>(warning) << "\n";
                    }
                }

                retVal = errors.size() + warnings.size() ? 1 : 0;
#ifndef DEBUG
            }
            catch (const std::exception& e) {
                std::cerr << e.what() << '\n';
                return 1;
            }
#endif // DEBUG
        }
        else {
            printInfo(std::cerr);
            std::cerr << "file " << inputPath << " is not valid\n";
            return 1;
        }
        for (auto& openedFileStream : openedFileStreams) {
            openedFileStream->flush();
            openedFileStream->close();
        }
        return retVal;
    }
    else if (argc == 2 && (argVector[1] == "--help" || argVector[1] == "-h")) {
        printInfo(std::cout);
        printHelp(std::cout);
    }
    else {
        printInfo(std::cerr);
        std::cerr << "invalid arguments:";
        for (const auto& arg : argVector) {
            std::cerr << " " << arg;
        }
        std::cerr << "\n";
        printHelp(std::cerr);
        return 2;
    }
    return 0;
}
