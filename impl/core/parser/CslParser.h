#pragma once

#ifndef PARSER_H
#define PARSER_H

#include <tuple>
#include <string>
#include <memory>
#include <vector>
#include <variant>
#include <unordered_map>
#include "../shared/Token.h"
#include "../shared/FilePosition.h"
#include "../shared/CslRepresentation.h"

std::tuple<std::vector<std::shared_ptr<CSL::ConfigSchema>>, std::vector<std::tuple<std::string, FilePosition::Region>>, std::vector<std::tuple<std::string, FilePosition::Region>>, std::unordered_map<size_t, std::variant<std::shared_ptr<CSL::ConfigSchema>, std::shared_ptr<CSL::TableType::KeyDefinition>>>> CslParserMain(Token::TokenList<std::string, std::unique_ptr<Type::Type>>& tokenList);

#endif // PARSER_H
