#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <functional>
#include <memory>
#include <chrono>
#include <thread>
#include <future>
#include <optional>
#include <vector>
#include "../shared/Components.h"

//#define DEBUG

int main(int argc, char* argv[]) {
    std::vector<std::string> argVector(argv, argv + argc);
    auto printInfo = [](std::ostream& stream) {
        stream << "csl-test: A Config Schema Language Utility Test Runner\n";
        stream << "Built at: " __TIME__ " " __DATE__ << "\n";
        stream << "Copyright (C) 2023-2025 nullptr-0.\n";
        stream.flush();
    };
    auto printHelp = [&argVector](std::ostream& stream) {
        stream << "Usage:\n"
            << argVector[0] << " --test <path>\n"
            << "    <path> must contain 'valid' and 'invalid' subdirectories, each\n"
            << "    with one or more '.csl' test files.\n"
            << "    - Valid tests are expected to produce no errors and no warnings.\n"
            << "    - Invalid tests are expected to produce errors or warnings.\n"
            << argVector[0] << " --help\n"
            << argVector[0] << " -h\n"
            << "    Print this help message.\n";
    };
    if (argc >= 3 && argVector[1] == "--test") {
        int retVal = 0;
        for (size_t i = 3; i < argVector.size(); ++i) {
            const std::string& arg = argv[i];
            printInfo(std::cerr);
            std::cerr << "invalid arguments:" << arg;
            for (const auto& a : argVector) {
                std::cerr << " " << a;
            }
            std::cerr << "\n";
            printHelp(std::cerr);
            return 2;
        }
        const auto inputPath = argVector[2];
        printInfo(std::cout);
        if (!std::filesystem::exists(inputPath) || !std::filesystem::is_directory(inputPath)) {
            std::cerr << "provided path is not a directory: " << inputPath << "\n";
            printHelp(std::cerr);
            return 2;
        }

        auto validDir = std::filesystem::path(inputPath) / "valid";
        auto invalidDir = std::filesystem::path(inputPath) / "invalid";
        if (!std::filesystem::exists(validDir) || !std::filesystem::is_directory(validDir) ||
            !std::filesystem::exists(invalidDir) || !std::filesystem::is_directory(invalidDir)) {
            std::cerr << "test directory must contain 'valid' and 'invalid' subdirectories\n";
            printHelp(std::cerr);
            return 2;
        }

        struct SingleRunResult {
            std::vector<std::tuple<std::string, FilePosition::Region>> errors;
            std::vector<std::tuple<std::string, FilePosition::Region>> warnings;
        };

        auto runSingle = [](const std::string& path) -> SingleRunResult {
            std::ifstream input(path, std::ios::in);
            if (!input.is_open()) {
                throw std::runtime_error(std::string("unable to open ") + path);
            }
            auto [cslTokenList, cslLexErrors, cslLexWarnings] = CslLexerMain(input, false);
            auto [schemas, cslParseErrors, cslParseWarnings, tokenCslReprMapping] = CslParserMain(cslTokenList);
            SingleRunResult r;
            r.errors.insert(r.errors.end(), cslLexErrors.begin(), cslLexErrors.end());
            r.errors.insert(r.errors.end(), cslParseErrors.begin(), cslParseErrors.end());
            r.warnings.insert(r.warnings.end(), cslLexWarnings.begin(), cslLexWarnings.end());
            r.warnings.insert(r.warnings.end(), cslParseWarnings.begin(), cslParseWarnings.end());
            return r;
        };

        struct TestOutcome {
            std::string name;
            std::string path;
            size_t timeMs;
            bool success;
            std::string reason;
            std::vector<std::string> details;
        };

        auto listTests = [](const std::filesystem::path& dir) -> std::vector<std::filesystem::path> {
            std::vector<std::filesystem::path> files;
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".csl") {
                    files.push_back(entry.path());
                }
            }
            std::sort(files.begin(), files.end());
            return files;
        };

        auto validTests = listTests(validDir);
        auto invalidTests = listTests(invalidDir);

        const size_t timeoutMs = 5000;
        std::vector<TestOutcome> outcomes;
        auto runWithTimeout = [&](const std::filesystem::path& p, bool expectInvalid) -> TestOutcome {
            auto start = std::chrono::steady_clock::now();
            std::promise<SingleRunResult> prom;
            auto fut = prom.get_future();
            std::thread th([&prom, p, &runSingle]() {
                try {
                    auto res = runSingle(p.string());
                    prom.set_value(std::move(res));
                }
                catch (...) {
                    prom.set_exception(std::current_exception());
                }
            });
            TestOutcome out;
            out.name = p.filename().string();
            out.path = p.string();
            auto waitRes = fut.wait_for(std::chrono::milliseconds(timeoutMs));
            if (waitRes != std::future_status::ready) {
                th.detach();
                auto end = std::chrono::steady_clock::now();
                out.timeMs = (size_t)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                out.success = false;
                out.reason = "timeout";
                out.details.push_back(std::string("path: ") + out.path);
                out.details.push_back(std::string("timeout after ") + std::to_string(timeoutMs) + " ms");
                return out;
            }
            try {
                auto r = fut.get();
                th.join();
                auto end = std::chrono::steady_clock::now();
                out.timeMs = (size_t)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                bool hasDiag = r.errors.size() || r.warnings.size();
                bool ok = expectInvalid ? hasDiag : !hasDiag;
                out.success = ok;
                if (!ok) {
                    out.reason = "expectation not met";
                    out.details.push_back(std::string("path: ") + out.path);
                    out.details.push_back(expectInvalid ? "expected diagnostics" : "expected no diagnostics");
                    std::ostringstream d0;
                    d0 << "errors=" << r.errors.size() << ", warnings=" << r.warnings.size();
                    out.details.push_back(d0.str());
                    const size_t maxList = 5;
                    size_t ei = 0;
                    for (const auto& e : r.errors) {
                        if (ei++ >= maxList) break;
                        auto pos = std::get<1>(e).start;
                        std::ostringstream d;
                        d << "error #" << ei << ": (line " << pos.line << ", col " << pos.column << ") " << std::get<0>(e);
                        out.details.push_back(d.str());
                    }
                    size_t wi = 0;
                    for (const auto& w : r.warnings) {
                        if (wi++ >= maxList) break;
                        auto pos = std::get<1>(w).start;
                        std::ostringstream d;
                        d << "warning #" << wi << ": (line " << pos.line << ", col " << pos.column << ") " << std::get<0>(w);
                        out.details.push_back(d.str());
                    }
                }
            }
            catch (const std::exception& e) {
                th.join();
                auto end = std::chrono::steady_clock::now();
                out.timeMs = (size_t)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                out.success = false;
                out.reason = "exception thrown";
                out.details.push_back(std::string("path: ") + out.path);
                out.details.push_back(e.what());
            }
            return out;
        };

        auto allStart = std::chrono::steady_clock::now();
        for (const auto& p : validTests) {
            outcomes.push_back(runWithTimeout(p, false));
        }
        for (const auto& p : invalidTests) {
            outcomes.push_back(runWithTimeout(p, true));
        }
        auto allEnd = std::chrono::steady_clock::now();
        auto totalMs = (size_t)std::chrono::duration_cast<std::chrono::milliseconds>(allEnd - allStart).count();

        size_t total = outcomes.size();
        size_t successCount = 0;
        for (const auto& o : outcomes) {
            if (o.success) ++successCount;
        }

        std::cout << "Ran " << total << " test(s) in " << totalMs << " ms\n";
        std::cout << "Success: " << successCount << " / " << total << "\n";
        if (successCount != total) {
            std::cout << "Failed tests:\n";
            for (const auto& o : outcomes) {
                if (o.success) continue;
                std::cout << "- " << o.name << " (" << o.timeMs << " ms) - " << o.reason << "\n";
                for (const auto& d : o.details) {
                    std::cout << "    " << d << "\n";
                }
            }
        }

        retVal = successCount == total ? 0 : 1;
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
