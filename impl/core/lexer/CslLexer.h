#pragma once

#ifndef LEXER_H
#define LEXER_H

#include <iostream>
#include <tuple>
#include <string>
#include <memory>
#include <vector>
#include "../shared/Type.h"
#include "../shared/Token.h"
#include "../shared/FilePosition.h"

std::tuple<Token::TokenList<std::string, std::unique_ptr<Type::Type>>, std::vector<std::tuple<std::string, FilePosition::Region>>, std::vector<std::tuple<std::string, FilePosition::Region>>> CslLexerMain(std::istream& inputCode, bool preserveComment, bool multilineToken = true);

#endif // LEXER_H
