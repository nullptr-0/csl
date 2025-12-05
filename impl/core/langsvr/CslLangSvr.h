#pragma once

#ifndef LANGUAGE_SERVER_H
#define LANGUAGE_SERVER_H

#include <functional>
#include <iostream>
#include <vector>
#include <string>
#include <tuple>
#include <memory>
#include <variant>
#include <unordered_map>
#include "../shared/Token.h"
#include "../shared/FilePosition.h"
#include "../shared/CslRepresentation.h"

using CslLexerFunction = std::function<std::tuple<Token::TokenList<std::string, std::unique_ptr<Type::Type>>, std::vector<std::tuple<std::string, FilePosition::Region>>, std::vector<std::tuple<std::string, FilePosition::Region>>>(std::istream&, bool, bool)>;
using CslLexerFunctionWithStringInput = std::function<std::tuple<Token::TokenList<std::string, std::unique_ptr<Type::Type>>, std::vector<std::tuple<std::string, FilePosition::Region>>, std::vector<std::tuple<std::string, FilePosition::Region>>>(const std::string&, bool, bool)>;
using CslParserFunction = std::function<std::tuple<std::vector<std::shared_ptr<CSL::ConfigSchema>>, std::vector<std::tuple<std::string, FilePosition::Region>>, std::vector<std::tuple<std::string, FilePosition::Region>>, std::unordered_map<size_t, std::variant<std::shared_ptr<CSL::ConfigSchema>, std::shared_ptr<CSL::TableType::KeyDefinition>>>>(Token::TokenList<std::string, std::unique_ptr<Type::Type>>&)>;

int CslLangSvrMain(
    std::istream& inChannel,
    std::ostream& outChannel,
    const CslLexerFunctionWithStringInput& cslLexer,
    const CslParserFunction& cslParser
);

#endif // LANGUAGE_SERVER_H
