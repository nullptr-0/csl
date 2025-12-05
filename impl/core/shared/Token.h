#pragma once

#ifndef TOKEN_H
#define TOKEN_H

#include <string>
#include <list>
#include <stdexcept>
#include <tuple>
#include "FilePosition.h"

namespace Token {
    template <typename TokenTypeType, typename TokenPropertyType>
    struct Token {
        std::string value;
        TokenTypeType type;
        TokenPropertyType prop;
        FilePosition::Region range;
    };

    template <typename TokenTypeType, typename TokenPropertyType>
    class TokenList {
    public:
        TokenList() : curTokenProp(TokenPropertyType{}), tokenBuffered(false) {}
        TokenList(TokenList&&) noexcept = default;
        TokenList& operator=(TokenList&&) noexcept = default;
        TokenList(const TokenList&) = delete;
        TokenList& operator=(const TokenList&) = delete;
        ~TokenList() = default;

        // Add the specified token to list.
        // Note that this operation will cause the current
        // buffered token (if available) to be flushed to
        // the list first and then the buffered content and
        // information to be cleared.
        void AddTokenToList(std::string tokenValue, TokenTypeType tokenType,
            TokenPropertyType tokenProp = {},
            FilePosition::Region range = {}) {
            FlushBuffer();
            Token<TokenTypeType, TokenPropertyType> t{ tokenValue, tokenType,
                std::move(tokenProp), range };
            tokenList.push_back(std::move(t));
        }

        void AddTokenToList(Token<TokenTypeType, TokenPropertyType> token) {
            FlushBuffer();
            tokenList.push_back(std::move(token));
        }

        // Set information for the current buffered token.
        void SetTokenInfo(TokenTypeType tokenType,
            TokenPropertyType tokenProp = {},
            FilePosition::Region range = {}) {
            curTokenType = tokenType;
            curTokenProp = std::move(tokenProp);
            curRange = range;
        }

        void AppendBufferedToken(const char newContent, FilePosition::Position locForThisChar) {
            curTokenContent.append(1, newContent);
            // Grow end of current range
            if (curRange.start == FilePosition::Position{}) curRange.start = locForThisChar;
            curRange.end = locForThisChar;
            tokenBuffered = true;
        }

        bool IsTokenBuffered() const {
            return tokenBuffered;
        }

        void FlushBuffer() {
            if (!curTokenContent.empty()) {
                Token<TokenTypeType, TokenPropertyType> token{
                    curTokenContent, curTokenType,
                    std::move(curTokenProp), curRange
                };
                tokenList.push_back(std::move(token));
                curTokenContent = "";
                curTokenType = {};
                curTokenProp = {};
                curRange = {};
                tokenBuffered = false;
            }
        }

        std::list<Token<TokenTypeType, TokenPropertyType>>& GetTokenList() {
            return tokenList;
        }

        // Iterator for the elements
        using iterator = typename std::list<Token<TokenTypeType, TokenPropertyType>>::iterator;

        iterator begin() {
            return tokenList.begin();
        }

        iterator end() {
            return tokenList.end();
        }

        using reverse_iterator = typename std::reverse_iterator<iterator>;

        reverse_iterator rbegin() {
            return tokenList.rbegin();
        }

        reverse_iterator rend() {
            return tokenList.rend();
        }

        // Const iterator for the elements
        using const_iterator = typename std::list<Token<TokenTypeType, TokenPropertyType>>::const_iterator;

        const_iterator begin() const {
            return tokenList.begin();
        }

        const_iterator end() const {
            return tokenList.end();
        }

        using const_reverse_iterator = typename std::reverse_iterator<const_iterator>;

        const_reverse_iterator rbegin() const {
            return tokenList.rbegin();
        }

        const_reverse_iterator rend() const {
            return tokenList.rend();
        }

        void clear() {
            FlushBuffer();
            tokenList.clear();
        }

        size_t size() const {
            return tokenList.size() + (tokenBuffered ? 1 : 0);
        }

        bool empty() const {
            return tokenList.empty() && !tokenBuffered;
        }

        iterator insert(const_iterator pos, const Token<TokenTypeType, TokenPropertyType>& token) {
            return tokenList.insert(pos, token);
        }

        template <class Iter>
        iterator insert(const const_iterator pos, Iter first, Iter last) {
            return tokenList.insert(pos, first, last);
        }

        iterator erase(const_iterator pos) {
            auto token = *pos;
            return tokenList.erase(pos);
        }

        const Token<TokenTypeType, TokenPropertyType>& front() const {
            if (tokenBuffered) {
                throw std::runtime_error("TokenList::front(): token is buffered, flush it first");
            }
            return tokenList.front();
        }

        Token<TokenTypeType, TokenPropertyType>& front() {
            if (tokenBuffered) {
                throw std::runtime_error("TokenList::front(): token is buffered, flush it first");
            }
            return tokenList.front();
        }

        const Token<TokenTypeType, TokenPropertyType>& back() const {
            if (tokenBuffered) {
                throw std::runtime_error("TokenList::back(): token is buffered, flush it first");
            }
            return tokenList.back();
        }

        Token<TokenTypeType, TokenPropertyType>& back() {
            if (tokenBuffered) {
                throw std::runtime_error("TokenList::back(): token is buffered, flush it first");
            }
            return tokenList.back();
        }

    protected:
        std::string curTokenContent;
        TokenTypeType curTokenType;
        TokenPropertyType curTokenProp;
        FilePosition::Region curRange{};
        bool tokenBuffered;
        std::list<Token<TokenTypeType, TokenPropertyType>> tokenList;
    };
};

#endif
