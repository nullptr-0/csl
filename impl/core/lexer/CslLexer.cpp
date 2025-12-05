#include <iostream>
#include <string>
#include <list>
#include <tuple>
#include <regex>
#include <optional>
#include <memory>
#include "../shared/CslStringUtils.h"
#include "../shared/Token.h"
#include "../shared/Type.h"
#include "../shared/FilePosition.h"

namespace CSLLexer {
    class Lexer {
    protected:
        std::istream& inputCode;
        bool multilineToken;
        std::vector<std::tuple<std::string, FilePosition::Region>> errors;
        std::vector<std::tuple<std::string, FilePosition::Region>> warnings;

        FilePosition::Position getEndPosition(const std::string& text, const FilePosition::Position& start) {
            auto line = start.line;
            auto col = start.column;

            for (char ch : text) {
                if (ch == '\n') {
                    ++line;
                    col = 0;
                }
                else {
                    ++col;
                }
            }

            return { line, col };
        }

        bool isNumberReasonablyGrouped(const std::string& str) {
            size_t dotPos = str.find('.');
            std::string beforeDot = str.substr(0, dotPos);
            if (beforeDot.size() && (beforeDot[0] == '+' || beforeDot[0] == '-')) {
                beforeDot.erase(0, 1);
            }
            if (beforeDot.size() > 2 && beforeDot[0] == '0' && (beforeDot[1] == 'b' || beforeDot[1] == 'o' || beforeDot[1] == 'x')) {
                beforeDot.erase(0, 2);
            }
            std::string afterDot = dotPos > str.size() ? "" : str.substr(dotPos + 1);

            std::vector<std::string> parts;
            std::vector<size_t> sizes;

            // Split by underscores
            size_t start = 0, end;
            while ((end = beforeDot.find('_', start)) != std::string::npos) {
                const auto part = beforeDot.substr(start, end - start);
                if (part.empty()) return false; // invalid like "1__000"
                parts.push_back(part);
                sizes.push_back(part.size());
                start = end + 1;
            }
            parts.push_back(beforeDot.substr(start));
            sizes.push_back(parts.back().size());

            if (parts.size() != 1) { // Has underscores
                bool allSame = true;

                for (size_t i = 1; i < sizes.size(); ++i) {
                    if (sizes[i] != sizes[1]) {
                        allSame = false;
                        break;
                    }
                }
                if (allSame) {
                    if (sizes[1] == 1) {
                        return false;
                    }
                }
                else {
                    allSame = true;
                    for (size_t i = 1; i < sizes.size() - 1; ++i) {
                        if (sizes[i] != 2) {
                            allSame = false;
                            break;
                        }
                    }
                    if (!allSame || sizes[sizes.size() - 1] != 3) {
                        return false;
                    }
                }
            }

            parts.clear();
            sizes.clear();
            start = 0;
            while ((end = afterDot.find('_', start)) != std::string::npos) {
                const auto part = afterDot.substr(start, end - start);
                if (part.empty()) return false; // invalid like "1__000"
                parts.push_back(part);
                sizes.push_back(part.size());
                start = end + 1;
            }
            parts.push_back(afterDot.substr(start));
            sizes.push_back(parts.back().size());

            if (parts.size() == 1) return true; // No underscores

            bool allSame = true;

            for (size_t i = 1; i < sizes.size(); ++i) {
                if (sizes[i] != sizes[1]) {
                    allSame = false;
                    break;
                }
            }
            if (!allSame) return false;
            if (sizes[1] == 1) {
                return false;
            }

            return true;
        }

        bool isStringContentValid(const std::string& stringToCheck, int stringType) {
            size_t i = 0;
            while (i < stringToCheck.size()) {
                uint32_t codepoint = 0;
                unsigned char c = stringToCheck[i];

                size_t bytes = 0;
                if ((c & 0x80) == 0) {
                    // 1-byte ASCII
                    codepoint = c;
                    bytes = 1;
                }
                else if ((c & 0xE0) == 0xC0) {
                    // 2-byte
                    if (i + 1 >= stringToCheck.size()) return false;
                    unsigned char c1 = stringToCheck[i + 1];
                    if ((c1 & 0xC0) != 0x80) return false;

                    codepoint = ((c & 0x1F) << 6) | (c1 & 0x3F);
                    if (codepoint < 0x80) return false;  // Overlong
                    bytes = 2;
                }
                else if ((c & 0xF0) == 0xE0) {
                    // 3-byte
                    if (i + 2 >= stringToCheck.size()) return false;
                    unsigned char c1 = stringToCheck[i + 1];
                    unsigned char c2 = stringToCheck[i + 2];
                    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;

                    codepoint = ((c & 0x0F) << 12) |
                        ((c1 & 0x3F) << 6) |
                        (c2 & 0x3F);
                    if (codepoint < 0x800) return false;        // Overlong
                    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return false;  // Surrogates
                    bytes = 3;
                }
                else if ((c & 0xF8) == 0xF0) {
                    // 4-byte
                    if (i + 3 >= stringToCheck.size()) return false;
                    unsigned char c1 = stringToCheck[i + 1];
                    unsigned char c2 = stringToCheck[i + 2];
                    unsigned char c3 = stringToCheck[i + 3];
                    if ((c1 & 0xC0) != 0x80 ||
                        (c2 & 0xC0) != 0x80 ||
                        (c3 & 0xC0) != 0x80) return false;

                    codepoint = ((c & 0x07) << 18) |
                        ((c1 & 0x3F) << 12) |
                        ((c2 & 0x3F) << 6) |
                        (c3 & 0x3F);
                    if (codepoint < 0x10000 || codepoint > 0x10FFFF) return false;  // Overlong or out of range
                    bytes = 4;
                }
                else {
                    // Invalid leading byte
                    return false;
                }

                if ((stringType == 0 || stringType == 2) &&
                    (codepoint >= 0x0000 && codepoint <= 0x0008 ||
                        codepoint >= 0x000A && codepoint <= 0x001F ||
                        codepoint == 0x007F)) {
                    return false;
                }

                if (stringType == 1 || stringType == 3) {
                    if (codepoint >= 0x0000 && codepoint <= 0x0008 ||
                        codepoint == 0x000B || codepoint == 0x000C ||
                        codepoint >= 0x000E && codepoint <= 0x001F ||
                        codepoint == 0x007F) {
                        return false;
                    }
                    else if (codepoint == 0x000D &&
                        (i + 1 >= stringToCheck.size() || stringToCheck[i] != 0x000A)) {
                        return false;
                    }
                }

                i += bytes;
            }

            return true;
        }

        bool customGetline(std::istream& in, std::string& line) {
            line.clear();
            char ch;

            while (in.get(ch)) {
                if (ch == '\n') {
                    // Check if we have a CRLF sequence
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();  // Remove the '\r' before '\n'
                    }
                    return true;  // End of line
                }
                else {
                    line += ch;
                }
            }

            // Return true if we got any characters, even if we hit EOF without newline
            return !line.empty();
        }

        bool HasIncompleteStringOrId(std::string input) {
            std::regex commentRegex(R"(//[^\n]*)");
            std::regex stringLiteralRegex(R"((\"([^\"\\]|\\.)*\")|(R\"([a-zA-Z0-9!\"#%&'*+,\-.\/:;<=>?\[\]^_{|}~]{0,16})\(((.|\n)*?)\)\4\"))");
            std::regex quotedIdentifierRegex(R"((`([^`\\]|\\.)*`)|(R`([a-zA-Z0-9!\"#%&'*+,\-.\/:;<=>?\[\]^_{|}~]{0,16})\(((.|\n)*?)\)\4`))");
            std::regex startRegex(R"("|R"|`|R`)");

            std::regex allRegexes[] = { commentRegex, stringLiteralRegex, quotedIdentifierRegex };

            while (true) {
                std::optional<std::tuple<size_t, size_t>> bestMatch; // start, length

                for (const auto& regex : allRegexes) {
                    std::smatch match;
                    if (std::regex_search(input, match, regex)) {
                        size_t pos = match.position(0);
                        size_t len = match.length(0);
                        if (!bestMatch || pos < std::get<0>(*bestMatch)) {
                            bestMatch = std::make_tuple(pos, len);
                        }
                    }
                }

                if (!bestMatch) break;

                // Erase the match from the input
                input.erase(std::get<0>(*bestMatch), std::get<1>(*bestMatch));
            }

            // Final check
            return std::regex_search(input, startRegex);
        }

        std::tuple<size_t, std::string> ParseComment(std::string strToCheck) {
            size_t commentStartIndex;
            std::string commentContent;
            std::regex commentRegex(R"(^(\s*)(//[^\n]*))");
            std::smatch match;
            if (std::regex_search(strToCheck, match, commentRegex) && !match.prefix().length()) {
                commentStartIndex = match[1].length();
                commentContent = match[0].str().substr(commentStartIndex);
            }
            return { commentStartIndex, commentContent };
        }

        std::tuple<std::unique_ptr<Type::Type>, size_t, std::string> ParseStringLiteral(std::string strToCheck) {
            std::unique_ptr<Type::Type> literalType;
            size_t literalStartIndex;
            std::string literalContent;
            std::regex stringLiteralRegex(R"(^(\s*)((\"([^\"\\]|\\.)*\")|(R\"([a-zA-Z0-9!\"#%&'*+,\-.\/:;<=>?\[\]^_{|}~]{0,16})\(((.|\n)*?)\)\6\")))");
            std::smatch match;
            if (std::regex_search(strToCheck, match, stringLiteralRegex) && !match.prefix().length()) {
                literalStartIndex = match[1].length();
                literalContent = match[0].str().substr(literalStartIndex);
                literalType = std::make_unique<Type::String>(literalContent[0] == 'R' ? (literalContent.find('\n') == std::string::npos ? Type::String::Raw : Type::String::MultiLineRaw) : (literalContent.find('\n') == std::string::npos ? Type::String::Basic : Type::String::MultiLineBasic));
            }
            return std::make_tuple(std::move(literalType), literalStartIndex, literalContent);
        }

        std::tuple<std::unique_ptr<Type::Type>, size_t, std::string> ParseDateTimeLiteral(std::string strToCheck) {
            auto isLeapYear = [](int year) {
                return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
            };

            auto isValidDatePart = [](const std::string& part, int min, int max) {
                if (part.empty() || part.size() > 2 || !std::all_of(part.begin(), part.end(), ::isdigit)) {
                    return false;
                }
                int value = std::stoi(part);
                return value >= min && value <= max;
            };

            auto isValidDate = [&isLeapYear, &isValidDatePart](const std::string& dateStr) {
                if (dateStr.length() != 10 || dateStr[4] != '-' || dateStr[7] != '-') {
                    return false;
                }

                std::string yearStr = dateStr.substr(0, 4);
                std::string monthStr = dateStr.substr(5, 2);
                std::string dayStr = dateStr.substr(8, 2);

                // Check if all characters are digits
                if (!std::all_of(yearStr.begin(), yearStr.end(), ::isdigit)) return false;

                int year = std::stoi(yearStr);
                int month = std::stoi(monthStr);
                int day = std::stoi(dayStr);

                if (year < 1 || month < 1 || month > 12) return false;

                // Days in each month
                int daysInMonth[] = { 31, isLeapYear(year) ? 29 : 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31 };

                if (day < 1 || day > daysInMonth[month - 1]) return false;

                return true;
            };

            std::unique_ptr<Type::Type> literalType;
            size_t literalStartIndex;
            std::string literalContent;
            std::regex offsetDateTimeRegex(R"(^(\s*)((\d{4}-\d{2}-\d{2})[Tt ]([01]\d|2[0-3]):[0-5]\d:[0-5]\d(\.\d+)?([Zz]|[+-]([01]\d|2[0-3]):[0-5]\d)))");
            std::regex localDateTimeRegex(R"(^(\s*)((\d{4}-\d{2}-\d{2})[Tt ]([01]\d|2[0-3]):[0-5]\d:[0-5]\d(\.\d+)?))");
            std::regex localDateRegex(R"(^(\s*)(\d{4}-\d{2}-\d{2}))");
            std::regex localTimeRegex(R"(^(\s*)(([01]\d|2[0-3]):[0-5]\d:[0-5]\d(\.\d+)?))");
            std::smatch match;
            if (std::regex_search(strToCheck, match, offsetDateTimeRegex) && !match.prefix().length() && isValidDate(match[3].str())) {
                literalType = std::make_unique<Type::DateTime>(Type::DateTime::OffsetDateTime);
            }
            else if (std::regex_search(strToCheck, match, localDateTimeRegex) && !match.prefix().length() && isValidDate(match[3].str())) {
                literalType = std::make_unique<Type::DateTime>(Type::DateTime::LocalDateTime);
            }
            else if (std::regex_search(strToCheck, match, localDateRegex) && !match.prefix().length() && isValidDate(match[2].str())) {
                literalType = std::make_unique<Type::DateTime>(Type::DateTime::LocalDate);
            }
            else if (std::regex_search(strToCheck, match, localTimeRegex) && !match.prefix().length()) {
                literalType = std::make_unique<Type::DateTime>(Type::DateTime::LocalTime);
            }
            if (literalType) {
                literalStartIndex = match[1].length();
                literalContent = match[0].str().substr(literalStartIndex);
            }
            return std::make_tuple(std::move(literalType), literalStartIndex, literalContent);
        }

        std::tuple<std::unique_ptr<Type::Type>, size_t, std::string> ParseDurationLiteral(std::string strToCheck) {
            std::unique_ptr<Type::Type> literalType;
            size_t literalStartIndex;
            std::string literalContent;

            std::regex isoDateTimePartRegex(R"(^(\s*)P(\d+Y|\d+M|\d+W|\d+D)+(T(\d+H|\d+M|\d+S)+)?)");
            std::regex isoTimeOnlyRegex(R"(^(\s*)PT(\d+H|\d+M|\d+S)+)");
            std::regex shorthandRegex(R"(^(\s*)(\d+)(ms|y|mo|w|d|h|m|s))");

            std::smatch match;
            if (std::regex_search(strToCheck, match, isoDateTimePartRegex) && !match.prefix().length()) {
                literalType = std::make_unique<Type::Duration>();
            }
            else if (std::regex_search(strToCheck, match, isoTimeOnlyRegex) && !match.prefix().length()) {
                literalType = std::make_unique<Type::Duration>();
            }
            else if (std::regex_search(strToCheck, match, shorthandRegex) && !match.prefix().length()) {
                literalType = std::make_unique<Type::Duration>();
            }

            if (literalType) {
                literalStartIndex = match[1].length();
                literalContent = match[0].str().substr(literalStartIndex);
            }
            return std::make_tuple(std::move(literalType), literalStartIndex, literalContent);
        }

        std::tuple<std::unique_ptr<Type::Type>, size_t, std::string> ParseNumericLiteral(std::string strToCheck) {
            std::unique_ptr<Type::Type> literalType;
            size_t literalStartIndex;
            std::string literalContent;
            std::regex integerLiteralRegex(R"(^(\s*)(0(?![xob])|[1-9]+(_?\d+)*|0x[\da-fA-F]+(_?[\da-fA-F]+)*|0o[0-7]+(_?[0-7]+)*|0b[01]+(_?[01]+)*))");
            std::regex floatLiteralRegex(R"(^(\s*)((0(?![xob])|[1-9]+(_?\d+)*)(\.((\d+_)*\d+))?(e[-+]?\d+(_?\d+)*)?))");
            std::regex specialNumLiteralRegex(R"(^(\s*)((nan|inf)(?![-\w])))");
            std::smatch match;
            if (std::regex_search(strToCheck, match, specialNumLiteralRegex) && !match.prefix().length()) {
                auto matchedStr = match[3].str();
                literalType = std::make_unique<Type::SpecialNumber>(matchedStr == "nan" ? Type::SpecialNumber::NaN : Type::SpecialNumber::Infinity);
            }
            else {
                bool matched = false;
                std::smatch integerMatch;
                std::smatch floatMatch;
                if (std::regex_search(strToCheck, integerMatch, integerLiteralRegex) && !integerMatch.prefix().length()) {
                    matched = true;
                }
                if (std::regex_search(strToCheck, floatMatch, floatLiteralRegex) && !floatMatch.prefix().length()) {
                    matched = true;
                }
                if (matched) {
                    if (integerMatch[0].length() >= floatMatch[0].length()) {
                        match = integerMatch;
                        literalType = std::make_unique<Type::Integer>();
                    }
                    else {
                        match = floatMatch;
                        literalType = std::make_unique<Type::Float>();
                    }
                }
            }
            if (literalType) {
                literalStartIndex = match[1].length();
                literalContent = match[0].str().substr(literalStartIndex);
                auto [identifierStartIndex, identifierContent] = ParseIdentifier(strToCheck);
                if (literalContent.length() < identifierContent.length()) {
                    literalStartIndex = 0;
                    literalContent = "";
                    literalType.reset();
                }
            }
            return std::make_tuple(std::move(literalType), literalStartIndex, literalContent);
        }

        std::tuple<std::unique_ptr<Type::Type>, size_t, std::string> ParseBooleanLiteral(std::string strToCheck) {
            std::unique_ptr<Type::Type> literalType;
            size_t literalStartIndex;
            std::string literalContent;
            std::regex boolLiteralRegex(R"(^(\s*)((true|false)(?![-\w])))");
            std::smatch match;
            if (std::regex_search(strToCheck, match, boolLiteralRegex) && !match.prefix().length()) {
                literalType = std::make_unique<Type::Boolean>();
                literalStartIndex = match[1].length();
                literalContent = match[0].str().substr(literalStartIndex);
            }
            return std::make_tuple(std::move(literalType), literalStartIndex, literalContent);
        }

        std::tuple<size_t, std::string> ParseKeyword(std::string strToCheck) {
            size_t keywordStartIndex;
            std::string keywordContent;
            std::regex keywordRegex(R"(^(\s*)((config|constraints|requires|conflicts|with|validate|exists|count_keys|all_keys|wildcard_keys|subset|\*)(?![-\w])))");
            std::smatch match;
            if (std::regex_search(strToCheck, match, keywordRegex) && !match.prefix().length()) {
                keywordStartIndex = match[1].length();
                keywordContent = match[0].str().substr(keywordStartIndex);
            }
            return { keywordStartIndex, keywordContent };
        }

        std::tuple<size_t, std::string> ParseType(std::string strToCheck) {
            size_t keywordStartIndex;
            std::string keywordContent;
            std::regex keywordRegex(R"(^(\s*)((any\{\}|any\[\]|string|number|boolean|datetime|duration)(?![-\w])))");
            std::smatch match;
            if (std::regex_search(strToCheck, match, keywordRegex) && !match.prefix().length()) {
                keywordStartIndex = match[1].length();
                keywordContent = match[0].str().substr(keywordStartIndex);
            }
            return { keywordStartIndex, keywordContent };
        }

        std::tuple<size_t, std::string> ParseOperator(std::string strToCheck) {
            auto escapeRegex = [](const std::string& str) -> std::string {
                static const std::string specialChars = ".^$|()[]{}*+?\\";
                std::string escaped;
                for (char c : str) {
                    if (specialChars.find(c) != std::string::npos) {
                        escaped += '\\';
                    }
                    escaped += c;
                }
                return escaped;
            };

            std::vector<std::string> operators = {
                "~", "!", "+", "-",
                ".", "@", "[", "(",
                "*", "/", "%",
                "<<", ">>",
                "<", "<=", ">", ">=",
                "==", "!=", "&", "^", "|",
                "&&", "||", "=",
                "]", ")",
                "?", ":",
            };
            std::sort(operators.begin(), operators.end(), [](const std::string& a, const std::string& b) -> bool {
                return a.size() > b.size();
            });
            std::string regexPattern = "^(\\s*)(";
            for (auto i = operators.begin(); i != operators.end(); ++i) {
                regexPattern += escapeRegex(*i);
                if (std::next(i) != operators.end()) {
                    regexPattern += "|";
                }
            }
            regexPattern += ")";
            size_t operatorStartIndex;
            std::string operatorContent;
            std::regex operatorRegex(regexPattern);
            std::smatch match;
            if (std::regex_search(strToCheck, match, operatorRegex) && !match.prefix().length()) {
                operatorStartIndex = match[1].length();
                operatorContent = match[0].str().substr(operatorStartIndex);
            }
            return { operatorStartIndex, operatorContent };
        }

        std::tuple<size_t, std::string> ParseIdentifier(std::string strToCheck) {
            size_t identifierStartIndex;
            std::string identifierContent;
            std::regex bareIdentifierRegex(R"(^(\s*)([a-zA-Z_][a-zA-Z0-9_]*))");
            std::regex quotedIdentifierRegex(R"(^(\s*)((`([^`\\]|\\.)*`)|(R`([a-zA-Z0-9!\"#%&'*+,\-.\/:;<=>?\[\]^_{|}~]{0,16})\(((.|\n)*?)\)\6`)))");
            std::smatch match;
            if (std::regex_search(strToCheck, match, bareIdentifierRegex) && !match.prefix().length()) {
                identifierStartIndex = match[1].length();
                identifierContent = match[0].str().substr(identifierStartIndex);
                if (identifierContent == "true" || identifierContent == "false") {
                    identifierStartIndex = 0;
                    identifierContent = "";
                }
            }
            else if (std::regex_search(strToCheck, match, quotedIdentifierRegex) && !match.prefix().length()) {
                identifierStartIndex = match[1].length();
                identifierContent = match[0].str().substr(identifierStartIndex);
            }
            return { identifierStartIndex, identifierContent };
        }

        std::tuple<size_t, std::string> ParsePunctuator(std::string strToCheck) {
            size_t punctuatorStartIndex;
            std::string punctuatorContent;
            std::regex punctuatorRegex(R"(^(\s*)(\{|\}|\[|\]|,|:|;|@|=>))");
            std::smatch match;
            if (std::regex_search(strToCheck, match, punctuatorRegex) && !match.prefix().length()) {
                punctuatorStartIndex = match[1].length();
                punctuatorContent = match[0].str().substr(punctuatorStartIndex);
            }
            return { punctuatorStartIndex, punctuatorContent };
        }

    public:
        Lexer(std::istream& inputCode, bool multilineToken = true) :
            inputCode(inputCode), multilineToken(multilineToken) {
        }

        Token::TokenList<std::string, std::unique_ptr<Type::Type>> Lex(bool preserveComment = false) {
            Token::TokenList<std::string, std::unique_ptr<Type::Type>> tokenList;
            std::string codeToProcess;
            FilePosition::Position currentPosition = { 0, 0 };
            std::string curLine;
            bool isContinued = false;
            while (customGetline(inputCode, curLine)) {
                if (std::regex_match(curLine, std::regex(R"(\s*)"))) {
                    if (std::regex_search(curLine, std::regex(R"(\r(?!\n))"))) {
                        FilePosition::Region errorRegion = { currentPosition.line, { 0, false }, currentPosition.line, { curLine.size(), false } };
                        errors.push_back({ "Line ending is not valid.", errorRegion });
                    }
                    ++currentPosition.line;
                    currentPosition.column = 0;
                    if (inputCode.peek() != -1 || std::regex_match(codeToProcess, std::regex(R"(\s+)"))) {
                        continue;
                    }
                }
                if (isContinued) {
                    codeToProcess += curLine;
                }
                else {
                    codeToProcess = curLine;
                }
                if (HasIncompleteStringOrId(codeToProcess)) {
                    isContinued = true;
                    codeToProcess += "\n";
                    if (inputCode.peek() != -1) {
                        continue;
                    }
                    else {
                        FilePosition::Region errorRegion = { currentPosition.line, { 0, false }, currentPosition.line, { codeToProcess.find('\n'), false } };
                        errors.push_back({ "String literal or quoted identifier is not closed.", errorRegion });
                    }
                }
                isContinued = false;
                while (codeToProcess.size()) {
                    // Early error: raw quoted identifier delimiter too long
                    {
                        std::smatch m;
                        if (std::regex_search(codeToProcess, m, std::regex(R"(^(\s*)R\`)"))) {
                            size_t prefixLen = m[1].str().size();
                            size_t i = prefixLen + 2; // after R`
                            size_t delimLen = 0;
                            while (i < codeToProcess.size() && codeToProcess[i] != '(' && codeToProcess[i] != '\n') {
                                ++delimLen; ++i;
                            }
                            if (delimLen > 16) {
                                auto tokenStart = getEndPosition(codeToProcess.substr(0, prefixLen), currentPosition);
                                auto tokenEnd = getEndPosition(codeToProcess.substr(prefixLen, i - prefixLen), tokenStart);
                                FilePosition::Region tokenRegion = { tokenStart, tokenEnd };
                                errors.push_back({ "Raw quoted identifier delimiter exceeds maximum length", tokenRegion });
                            }
                        }
                    }
                    // Comment
                    {
                        auto [tokenStartIndex, tokenContent] = ParseComment(codeToProcess);
                        if (!tokenContent.empty()) {
                            auto tokenStart = getEndPosition(codeToProcess.substr(0, tokenStartIndex), currentPosition);
                            auto tokenEnd = getEndPosition(tokenContent, tokenStart);
                            FilePosition::Region tokenRegion = { tokenStart, tokenEnd };
                            if (preserveComment) {
                                tokenList.AddTokenToList(tokenContent, "comment", nullptr, tokenRegion);
                            }
                            currentPosition = tokenEnd;
                            codeToProcess.erase(0, tokenStartIndex + tokenContent.size());
                            if (tokenContent.find("//") >= tokenContent.size() ? false : !isStringContentValid(tokenContent.substr(tokenContent.find('#') + 1), 0)) {
                                errors.push_back({ "Comment contains invalid content.", tokenRegion });
                            }
                            continue;
                        }
                    }
                    // String Literal
                    {
                        auto [tokenType, tokenStartIndex, tokenContent] = ParseStringLiteral(codeToProcess);
                        if (!tokenContent.empty()) {
                            auto stringType = dynamic_cast<Type::String*>(tokenType.get())->getType();
                            auto tokenStart = getEndPosition(codeToProcess.substr(0, tokenStartIndex), currentPosition);
                            auto tokenEnd = getEndPosition(tokenContent, tokenStart);
                            FilePosition::Region tokenRegion = { tokenStart, tokenEnd };
                            tokenList.AddTokenToList(tokenContent, "string", std::move(tokenType), tokenRegion);
                            currentPosition = tokenEnd;
                            codeToProcess.erase(0, tokenStartIndex + tokenContent.size());
                            if (!isStringContentValid(tokenContent, stringType)) {
                                errors.push_back({ "String literal contains invalid content.", tokenRegion });
                            }
                            continue;
                        }
                    }
                    // Date Time Literal
                    {
                        auto [tokenType, tokenStartIndex, tokenContent] = ParseDateTimeLiteral(codeToProcess);
                        if (!tokenContent.empty()) {
                            auto tokenStart = getEndPosition(codeToProcess.substr(0, tokenStartIndex), currentPosition);
                            auto tokenEnd = getEndPosition(tokenContent, tokenStart);
                            FilePosition::Region tokenRegion = { tokenStart, tokenEnd };
                            tokenList.AddTokenToList(tokenContent, "datetime", std::move(tokenType), tokenRegion);
                            currentPosition = tokenEnd;
                            codeToProcess.erase(0, tokenStartIndex + tokenContent.size());
                            continue;
                        }
                    }
                    // Duration Literal
                    {
                        auto [tokenType, tokenStartIndex, tokenContent] = ParseDurationLiteral(codeToProcess);
                        if (!tokenContent.empty()) {
                            auto tokenStart = getEndPosition(codeToProcess.substr(0, tokenStartIndex), currentPosition);
                            auto tokenEnd = getEndPosition(tokenContent, tokenStart);
                            FilePosition::Region tokenRegion = { tokenStart, tokenEnd };
                            tokenList.AddTokenToList(tokenContent, "duration", std::move(tokenType), tokenRegion);
                            currentPosition = tokenEnd;
                            codeToProcess.erase(0, tokenStartIndex + tokenContent.size());
                            if (codeToProcess.size() && std::regex_search(codeToProcess, std::regex(R"(^([A-Za-z]))"))) {
                                FilePosition::Region errorRegion = { currentPosition, getEndPosition(codeToProcess.substr(0, 1), currentPosition) };
                                errors.push_back({ "Duration literal contains invalid suffix", errorRegion });
                            }
                            continue;
                        }
                    }
                    // Numeric Literal
                    {
                        auto [tokenType, tokenStartIndex, tokenContent] = ParseNumericLiteral(codeToProcess);
                        if (!tokenContent.empty()) {
                            auto tokenStart = getEndPosition(codeToProcess.substr(0, tokenStartIndex), currentPosition);
                            auto tokenEnd = getEndPosition(tokenContent, tokenStart);
                            FilePosition::Region tokenRegion = { tokenStart, tokenEnd };
                            tokenList.AddTokenToList(tokenContent, "number", std::move(tokenType), tokenRegion);
                            currentPosition = tokenEnd;
                            codeToProcess.erase(0, tokenStartIndex + tokenContent.size());
                            if (tokenContent.size() > 3 && (tokenContent[0] == '+' || tokenContent[0] == '-') && tokenContent[1] == '0' && (tokenContent[2] == 'b' || tokenContent[2] == 'o' || tokenContent[2] == 'x')) {
                                errors.push_back({ "Number literal in hexadecimal, octal or binary cannot have a positive or negative sign.", tokenRegion });
                            }
                            if (!isNumberReasonablyGrouped(tokenContent)) {
                                warnings.push_back({ "Number literal is not grouped reasonably.", tokenRegion });
                            }
                            continue;
                        }
                    }
                    // Boolean Literal
                    {
                        auto [tokenType, tokenStartIndex, tokenContent] = ParseBooleanLiteral(codeToProcess);
                        if (!tokenContent.empty()) {
                            auto tokenStart = getEndPosition(codeToProcess.substr(0, tokenStartIndex), currentPosition);
                            auto tokenEnd = getEndPosition(tokenContent, tokenStart);
                            FilePosition::Region tokenRegion = { tokenStart, tokenEnd };
                            tokenList.AddTokenToList(tokenContent, "boolean", std::make_unique<Type::Boolean>(), tokenRegion);
                            currentPosition = tokenEnd;
                            codeToProcess.erase(0, tokenStartIndex + tokenContent.size());
                            continue;
                        }
                    }
                    // Keyword
                    {
                        auto [tokenStartIndex, tokenContent] = ParseKeyword(codeToProcess);
                        if (!tokenContent.empty()) {
                            auto tokenStart = getEndPosition(codeToProcess.substr(0, tokenStartIndex), currentPosition);
                            auto tokenEnd = getEndPosition(tokenContent, tokenStart);
                            FilePosition::Region tokenRegion = { tokenStart, tokenEnd };
                            tokenList.AddTokenToList(tokenContent, "keyword", nullptr, tokenRegion);
                            currentPosition = tokenEnd;
                            codeToProcess.erase(0, tokenStartIndex + tokenContent.size());
                            continue;
                        }
                    }
                    // Type
                    {
                        auto [tokenStartIndex, tokenContent] = ParseType(codeToProcess);
                        if (!tokenContent.empty()) {
                            auto tokenStart = getEndPosition(codeToProcess.substr(0, tokenStartIndex), currentPosition);
                            auto tokenEnd = getEndPosition(tokenContent, tokenStart);
                            FilePosition::Region tokenRegion = { tokenStart, tokenEnd };
                            tokenList.AddTokenToList(tokenContent, "type", nullptr, tokenRegion);
                            currentPosition = tokenEnd;
                            codeToProcess.erase(0, tokenStartIndex + tokenContent.size());
                            continue;
                        }
                    }
                    // Punctuator (handle multi-character punctuators like '=>')
                    {
                        auto [tokenStartIndex, tokenContent] = ParsePunctuator(codeToProcess);
                        if (!tokenContent.empty()) {
                            auto tokenStart = getEndPosition(codeToProcess.substr(0, tokenStartIndex), currentPosition);
                            auto tokenEnd = getEndPosition(tokenContent, tokenStart);
                            FilePosition::Region tokenRegion = { tokenStart, tokenEnd };
                            tokenList.AddTokenToList(tokenContent, "punctuator", nullptr, tokenRegion);
                            currentPosition = tokenEnd;
                            codeToProcess.erase(0, tokenStartIndex + tokenContent.size());
                            continue;
                        }
                    }
                    // Operator
                    {
                        auto [tokenStartIndex, tokenContent] = ParseOperator(codeToProcess);
                        if (!tokenContent.empty()) {
                            auto tokenStart = getEndPosition(codeToProcess.substr(0, tokenStartIndex), currentPosition);
                            auto tokenEnd = getEndPosition(tokenContent, tokenStart);
                            FilePosition::Region tokenRegion = { tokenStart, tokenEnd };
                            tokenList.AddTokenToList(tokenContent, "operator", nullptr, tokenRegion);
                            currentPosition = tokenEnd;
                            codeToProcess.erase(0, tokenStartIndex + tokenContent.size());
                            continue;
                        }
                    }
                    // Identifier
                    {
                        auto [tokenStartIndex, tokenContent] = ParseIdentifier(codeToProcess);
                        if (!tokenContent.empty()) {
                            auto tokenStart = getEndPosition(codeToProcess.substr(0, tokenStartIndex), currentPosition);
                            auto tokenEnd = getEndPosition(tokenContent, tokenStart);
                            FilePosition::Region tokenRegion = { tokenStart, tokenEnd };
                            if (tokenContent.size() && (tokenContent[0] == '`' || (tokenContent[0] == 'R' && tokenContent.size() >= 5 && tokenContent[1] == '`'))) {
                                tokenList.AddTokenToList(extractQuotedIdentifierContent(tokenContent), "identifier", nullptr, tokenRegion);
                                currentPosition = tokenEnd;
                                codeToProcess.erase(0, tokenStartIndex + tokenContent.size());
                                if (codeToProcess.size() && std::regex_search(codeToProcess, std::regex(R"(^([a-zA-Z0-9!"#%&'*+,\-.\/::;<=>?\[\]^_{|}~])`)"))) {
                                    FilePosition::Region errorRegion = { currentPosition, currentPosition };
                                    errors.push_back({ "Raw quoted identifier delimiter exceeds maximum length", errorRegion });
                                    // Consume the leftover to avoid cascaded errors
                                    codeToProcess.erase(0, 2);
                                    currentPosition.column += 2;
                                }
                                continue;
                            }
                            else {
                                tokenList.AddTokenToList(tokenContent, "identifier", nullptr, tokenRegion);
                                currentPosition = tokenEnd;
                                codeToProcess.erase(0, tokenStartIndex + tokenContent.size());
                            }
                            continue;
                        }
                    }

                    if (std::regex_match(codeToProcess, std::regex(R"(\s*)"))) {
                        auto tokenEnd = getEndPosition(codeToProcess, currentPosition);
                        currentPosition = tokenEnd;
                        codeToProcess.clear();
                        continue;
                    }

                    // Unknown Content
                    if (!tokenList.IsTokenBuffered()) {
                        tokenList.SetTokenInfo("unknown");
                    }
                    tokenList.AppendBufferedToken(codeToProcess[0], currentPosition);
                    if (codeToProcess[0] == '\n') {
                        ++currentPosition.line;
                        currentPosition.column = 0;
                    }
                    else {
                        ++currentPosition.column;
                    }
                    codeToProcess.erase(0, 1);
                }
                tokenList.FlushBuffer();
                ++currentPosition.line;
                currentPosition.column = 0;
            }
            for (const auto& token : tokenList) {
                if (token.type == "unknown") {
                    errors.push_back({ "Unknown token: " + token.value + ".", token.range });
                }
            }
            return tokenList;
        }

        std::vector<std::tuple<std::string, FilePosition::Region>> GetErrors() {
            return errors;
        }

        std::vector<std::tuple<std::string, FilePosition::Region>> GetWarnings() {
            return warnings;
        }
    };
}

std::tuple<Token::TokenList<std::string, std::unique_ptr<Type::Type>>, std::vector<std::tuple<std::string, FilePosition::Region>>, std::vector<std::tuple<std::string, FilePosition::Region>>> CslLexerMain(std::istream& inputCode, bool preserveComment, bool multilineToken = true) {
    CSLLexer::Lexer lexer(inputCode, multilineToken);
    return { lexer.Lex(preserveComment), lexer.GetErrors(), lexer.GetWarnings() };
}
