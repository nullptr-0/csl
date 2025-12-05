#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <variant>
#include <stdexcept>
#include <functional>
#include "../shared/Type.h"
#include "../shared/Token.h"
#include "../shared/CslRepresentation.h"

namespace CSLParser {

    using namespace CSL;

    class Parser {
    protected:
        Token::TokenList<std::string, std::unique_ptr<Type::Type>>& input;
        Token::TokenList<std::string, std::unique_ptr<Type::Type>>::iterator position;
        std::vector<std::tuple<std::string, FilePosition::Region>> errors;
        std::vector<std::tuple<std::string, FilePosition::Region>> warnings;
        using ReprPtr = std::variant<std::shared_ptr<ConfigSchema>, std::shared_ptr<TableType::KeyDefinition>>;
        std::unordered_map<size_t, ReprPtr> tokenCslReprMapping;
        std::unordered_map<size_t, std::unordered_map<std::string, std::vector<size_t>>> identifierInExpr;
        size_t currentDepth = 0;

    public:
        Parser(Token::TokenList<std::string, std::unique_ptr<Type::Type>>& input) : input(input), position(input.begin()) {}

        // Entry point: Parse multiple schemas
        std::vector<std::shared_ptr<ConfigSchema>> parseSchemas() {
            std::vector<std::shared_ptr<ConfigSchema>> schemas;
            while (position != input.end()) {
                if (position->value == "config") {
                    schemas.push_back(parseConfigSchema());
                }
                else {
                    advance(); // Skip unknown tokens
                }
            }
            return schemas;
        }

        std::vector<std::tuple<std::string, FilePosition::Region>> getErrors() const {
            return errors;
        }

        std::vector<std::tuple<std::string, FilePosition::Region>> getWarnings() const {
            return warnings;
        }

        const std::unordered_map<size_t, ReprPtr>& getTokenCslReprMapping() const {
            return tokenCslReprMapping;
        }

    protected:
        void validateAnnotationsForType(const std::shared_ptr<CSLType>& type) {
            if (!type) return;
            if (type->getKind() != CSLType::Kind::Primitive) return;
            auto pt = std::static_pointer_cast<PrimitiveType>(type);
            auto prim = pt->getPrimitive();
            for (const auto& a : pt->getAnnotations()) {
                if (!a) continue;
                const auto& name = a->getName();
                if ((name == "min" || name == "max") && prim == PrimitiveType::Primitive::String) {
                    errors.push_back({ "Numeric annotation '@" + name + "' is not allowed on string", a->getRegion() });
                }
                if (name == "regex" && prim == PrimitiveType::Primitive::Number) {
                    errors.push_back({ "String annotation '@regex' is not allowed on number", a->getRegion() });
                }
            }
        }

        void validateDefaultForKey(const std::shared_ptr<TableType::KeyDefinition>& key) {
            if (!key) return;
            const auto& dv = key->getDefaultValue();
            if (!dv) return;
            auto type = key->getType();
            if (!type || type->getKind() != CSLType::Kind::Primitive) return;
            auto pt = std::static_pointer_cast<PrimitiveType>(type);
            auto prim = pt->getPrimitive();
            auto defaultTypeName = dv->second->toString();
            bool mismatch = false;
            switch (prim) {
            case PrimitiveType::Primitive::String:
                mismatch = !(defaultTypeName == "Basic" || defaultTypeName == "Raw" || defaultTypeName == "MultiLineBasic" || defaultTypeName == "MultiLineRaw");
                break;
            case PrimitiveType::Primitive::Number:
                mismatch = !(defaultTypeName == "Integer" || defaultTypeName == "Float" || defaultTypeName == "NaN" || defaultTypeName == "Infinity");
                break;
            case PrimitiveType::Primitive::Boolean:
                mismatch = defaultTypeName != "Boolean";
                break;
            case PrimitiveType::Primitive::Datetime:
                mismatch = !(defaultTypeName == "OffsetDateTime" || defaultTypeName == "LocalDateTime" || defaultTypeName == "LocalDate" || defaultTypeName == "LocalTime");
                break;
            case PrimitiveType::Primitive::Duration:
                mismatch = defaultTypeName != "Duration";
                break;
            }
            if (mismatch) {
                errors.push_back({ "Default value type does not match declared type", key->getNameRegion() });
            }
        }

        void validateConstraintsSemantics(const std::vector<std::shared_ptr<Constraint>>& constraints, const std::vector<std::shared_ptr<TableType::KeyDefinition>>& rootKeys) {
            auto keyExists = [&rootKeys](const std::string& name) -> bool {
                return std::find_if(rootKeys.begin(), rootKeys.end(), [&name](const std::shared_ptr<TableType::KeyDefinition>& k){ return k->getName() == name; }) != rootKeys.end();
            };
            for (const auto& c : constraints) {
                if (!c) continue;
                if (c->getKind() == Constraint::Kind::Dependency) {
                    auto dc = std::static_pointer_cast<DependencyConstraint>(c);
                    auto cond = dc->getCondition();
                    if (cond && cond->getKind() == Expr::Kind::Identifier) {
                        auto id = std::static_pointer_cast<IdentifierExpr>(cond);
                        if (!keyExists(id->getName())) {
                            errors.push_back({ "Dependency references missing key: " + id->getName(), id->getRegion() });
                        }
                    }
                    // Walk dependent expression for unknown identifiers
                    std::function<void(std::shared_ptr<Expr>)> walk;
                    walk = [&](std::shared_ptr<Expr> e){
                        if (!e) return;
                        switch (e->getKind()) {
                        case Expr::Kind::Identifier: {
                            auto id = std::static_pointer_cast<IdentifierExpr>(e);
                            size_t idx = getIdentifierTokenIndexFromRegion(id->getRegion());
                            if (!keyExists(id->getName()) && (idx == static_cast<size_t>(-1) || tokenCslReprMapping.find(idx) == tokenCslReprMapping.end())) {
                                errors.push_back({ std::string("Unknown identifier in current context: ") + id->getName(), id->getRegion() });
                            }
                            break;
                        }
                        case Expr::Kind::BinaryOp: {
                            auto be = std::static_pointer_cast<BinaryExpr>(e);
                            if (be->getOp() == ".") {
                                walk(be->getLHS());
                                // Skip RHS identifier unknown check; it represents a property of LHS
                            } else {
                                walk(be->getLHS()); walk(be->getRHS());
                            }
                            break;
                        }
                        case Expr::Kind::UnaryOp: {
                            auto ue = std::static_pointer_cast<UnaryExpr>(e);
                            walk(ue->getOperand());
                            break;
                        }
                        case Expr::Kind::TernaryOp: {
                            auto te = std::static_pointer_cast<TernaryExpr>(e);
                            walk(te->getCondition()); walk(te->getTrueExpr()); walk(te->getFalseExpr());
                            break;
                        }
                        case Expr::Kind::FunctionCall: {
                            auto fc = std::static_pointer_cast<FunctionCallExpr>(e);
                            for (const auto& a : fc->getArgs()) {
                                if (!a) continue;
                                auto val = std::static_pointer_cast<FunctionArgExpr>(a)->getValue();
                                if (std::holds_alternative<std::shared_ptr<Expr>>(val)) walk(std::get<std::shared_ptr<Expr>>(val));
                                else {
                                    if (fc->getFuncName() != "subset") {
                                        for (const auto& se : std::get<std::vector<std::shared_ptr<Expr>>>(val)) walk(se);
                                    }
                                }
                            }
                            break;
                        }
                        default: break;
                        }
                    };
                    walk(dc->getDependentExpr());
                } else if (c->getKind() == Constraint::Kind::Validate) {
                    auto vc = std::static_pointer_cast<ValidateConstraint>(c);
                    auto ex = vc->getExpr();
                    // Disallow referencing parent keys in nested tables: identifiers not found in rootKeys
                    std::function<void(std::shared_ptr<Expr>)> walk = [&](std::shared_ptr<Expr> e){
                        if (!e) return;
                        switch (e->getKind()) {
                        case Expr::Kind::Identifier: {
                            auto id = std::static_pointer_cast<IdentifierExpr>(e);
                            // If this identifier was resolved via mapping (e.g., RHS of '.') then skip unknown check
                            size_t idx = getIdentifierTokenIndexFromRegion(id->getRegion());
                            if (!keyExists(id->getName()) && (idx == static_cast<size_t>(-1) || tokenCslReprMapping.find(idx) == tokenCslReprMapping.end())) {
                                errors.push_back({ std::string("Unknown identifier in current context: ") + id->getName(), id->getRegion() });
                            }
                            break;
                        }
                        case Expr::Kind::BinaryOp: {
                            auto be = std::static_pointer_cast<BinaryExpr>(e);
                            if (be->getOp() == ".") {
                                walk(be->getLHS());
                                // Skip RHS diagnostic; treated as property
                            } else {
                                walk(be->getLHS()); walk(be->getRHS());
                            }
                            break;
                        }
                        case Expr::Kind::UnaryOp: {
                            auto ue = std::static_pointer_cast<UnaryExpr>(e);
                            walk(ue->getOperand());
                            break;
                        }
                        case Expr::Kind::TernaryOp: {
                            auto te = std::static_pointer_cast<TernaryExpr>(e);
                            walk(te->getCondition()); walk(te->getTrueExpr()); walk(te->getFalseExpr());
                            break;
                        }
                        case Expr::Kind::FunctionCall: {
                            auto fc = std::static_pointer_cast<FunctionCallExpr>(e);
                            // subset() with property list requires arrays of table types
                            if (fc->getFuncName() == "subset" && fc->getArgs().size() >= 3) {
                                auto arrTypeA = resolveTableTypeFromExpr(std::static_pointer_cast<FunctionArgExpr>(fc->getArgs()[0])->getValue().index() == 0 ? std::get<std::shared_ptr<Expr>>(std::static_pointer_cast<FunctionArgExpr>(fc->getArgs()[0])->getValue()) : nullptr, rootKeys);
                                auto arrTypeB = resolveTableTypeFromExpr(std::static_pointer_cast<FunctionArgExpr>(fc->getArgs()[1])->getValue().index() == 0 ? std::get<std::shared_ptr<Expr>>(std::static_pointer_cast<FunctionArgExpr>(fc->getArgs()[1])->getValue()) : nullptr, rootKeys);
                                if (!arrTypeA || !arrTypeB) {
                                    errors.push_back({ "subset with property list requires table arrays", fc->getRegion() });
                                }
                            }
                            for (const auto& a : fc->getArgs()) {
                                if (!a) continue;
                                auto val = std::static_pointer_cast<FunctionArgExpr>(a)->getValue();
                                if (std::holds_alternative<std::shared_ptr<Expr>>(val)) {
                                    walk(std::get<std::shared_ptr<Expr>>(val));
                                } else {
                                    // Skip unknown-identifier checks for property lists in 'subset'
                                    if (fc->getFuncName() != "subset") {
                                        for (const auto& se : std::get<std::vector<std::shared_ptr<Expr>>>(val)) walk(se);
                                    }
                                }
                            }
                            break;
                        }
                        default: break;
                        }
                    };
                    walk(ex);
                } else if (c->getKind() == Constraint::Kind::Conflict) {
                    auto cc = std::static_pointer_cast<ConflictConstraint>(c);
                    std::function<void(std::shared_ptr<Expr>)> walk;
                    walk = [&](std::shared_ptr<Expr> e){
                        if (!e) return;
                        switch (e->getKind()) {
                        case Expr::Kind::Identifier: {
                            auto id = std::static_pointer_cast<IdentifierExpr>(e);
                            size_t idx = getIdentifierTokenIndexFromRegion(id->getRegion());
                            if (!keyExists(id->getName()) && (idx == static_cast<size_t>(-1) || tokenCslReprMapping.find(idx) == tokenCslReprMapping.end())) {
                                errors.push_back({ std::string("Unknown identifier in current context: ") + id->getName(), id->getRegion() });
                            }
                            break;
                        }
                        case Expr::Kind::BinaryOp: {
                            auto be = std::static_pointer_cast<BinaryExpr>(e);
                            if (be->getOp() == ".") {
                                walk(be->getLHS());
                            } else {
                                walk(be->getLHS()); walk(be->getRHS());
                            }
                            break;
                        }
                        case Expr::Kind::UnaryOp: {
                            auto ue = std::static_pointer_cast<UnaryExpr>(e);
                            walk(ue->getOperand());
                            break;
                        }
                        case Expr::Kind::TernaryOp: {
                            auto te = std::static_pointer_cast<Expr>(e);
                            // conflicts should not contain ternary; ignore
                            break;
                        }
                        case Expr::Kind::FunctionCall: {
                            auto fc = std::static_pointer_cast<FunctionCallExpr>(e);
                            for (const auto& a : fc->getArgs()) {
                                if (!a) continue;
                                auto val = std::static_pointer_cast<FunctionArgExpr>(a)->getValue();
                                if (std::holds_alternative<std::shared_ptr<Expr>>(val)) walk(std::get<std::shared_ptr<Expr>>(val));
                            }
                            break;
                        }
                        default: break;
                        }
                    };
                    walk(cc->getFirstExpr());
                    walk(cc->getSecondExpr());
                }
            }
        }

        size_t getIdentifierTokenIndexFromRegion(const FilePosition::Region& region) {
            for (auto it = input.begin(); it != input.end(); ++it) {
                if (it->type == "identifier" && it->range.start == region.start && it->range.end == region.end) {
                    return std::distance(input.begin(), it);
                }
            }
            return static_cast<size_t>(-1);
        }

        std::shared_ptr<TableType> getTableTypeFromCSLType(const std::shared_ptr<CSLType>& type) {
            if (!type) return nullptr;
            if (type->getKind() == CSLType::Kind::Table) {
                return std::static_pointer_cast<TableType>(type);
            }
            if (type->getKind() == CSLType::Kind::Union) {
                auto ut = std::static_pointer_cast<UnionType>(type);
                for (const auto& member : ut->getMemberTypes()) {
                    if (member && member->getKind() == CSLType::Kind::Table) {
                        return std::static_pointer_cast<TableType>(member);
                    }
                }
            }
            if (type->getKind() == CSLType::Kind::Array) {
                auto at = std::static_pointer_cast<ArrayType>(type);
                auto elem = at->getElementType();
                if (elem && elem->getKind() == CSLType::Kind::Table) {
                    return std::static_pointer_cast<TableType>(elem);
                }
                if (elem && elem->getKind() == CSLType::Kind::Union) {
                    auto ut = std::static_pointer_cast<UnionType>(elem);
                    for (const auto& member : ut->getMemberTypes()) {
                        if (member && member->getKind() == CSLType::Kind::Table) {
                            return std::static_pointer_cast<TableType>(member);
                        }
                    }
                }
            }
            return nullptr;
        }

        std::shared_ptr<TableType> resolveTableTypeFromExpr(const std::shared_ptr<Expr>& expr, const std::vector<std::shared_ptr<TableType::KeyDefinition>>& keys) {
            if (!expr) return nullptr;
            if (expr->getKind() == Expr::Kind::Identifier) {
                auto id = std::static_pointer_cast<IdentifierExpr>(expr);
                auto it = std::find_if(keys.begin(), keys.end(), [&id](const std::shared_ptr<TableType::KeyDefinition>& k){ return k->getName() == id->getName(); });
                if (it != keys.end()) {
                    return getTableTypeFromCSLType((*it)->getType());
                }
                return nullptr;
            }
            if (expr->getKind() == Expr::Kind::BinaryOp) {
                auto be = std::static_pointer_cast<BinaryExpr>(expr);
                if (be->getOp() == ".") {
                    auto leftTable = resolveTableTypeFromExpr(be->getLHS(), keys);
                    if (!leftTable) return nullptr;
                    auto rhs = be->getRHS();
                    if (rhs && rhs->getKind() == Expr::Kind::Identifier) {
                        auto rid = std::static_pointer_cast<IdentifierExpr>(rhs);
                        const auto& subKeys = leftTable->getExplicitKeys();
                        auto it = std::find_if(subKeys.begin(), subKeys.end(), [&rid](const std::shared_ptr<TableType::KeyDefinition>& k){ return k->getName() == rid->getName(); });
                        if (it != subKeys.end()) {
                            return getTableTypeFromCSLType((*it)->getType());
                        }
                    }
                    return nullptr;
                }
            }
            return nullptr;
        }

        void mapIdentifiersInExpr(const std::shared_ptr<Expr>& expr, const std::vector<std::shared_ptr<TableType::KeyDefinition>>& rootKeys) {
            if (!expr) return;
            if (expr->getKind() == Expr::Kind::Identifier) {
                auto id = std::static_pointer_cast<IdentifierExpr>(expr);
                auto it = std::find_if(rootKeys.begin(), rootKeys.end(), [&id](const std::shared_ptr<TableType::KeyDefinition>& k){ return k->getName() == id->getName(); });
                if (it != rootKeys.end()) {
                    size_t idx = getIdentifierTokenIndexFromRegion(id->getRegion());
                    if (idx != static_cast<size_t>(-1)) {
                        tokenCslReprMapping[idx] = *it;
                    }
                }
                return;
            }
            if (expr->getKind() == Expr::Kind::BinaryOp) {
                auto be = std::static_pointer_cast<BinaryExpr>(expr);
                if (be->getOp() == ".") {
                    auto ctxTable = resolveTableTypeFromExpr(be->getLHS(), rootKeys);
                    if (ctxTable) {
                        auto rhs = be->getRHS();
                        if (rhs && rhs->getKind() == Expr::Kind::Identifier) {
                            auto rid = std::static_pointer_cast<IdentifierExpr>(rhs);
                            const auto& subKeys = ctxTable->getExplicitKeys();
                            auto it = std::find_if(subKeys.begin(), subKeys.end(), [&rid](const std::shared_ptr<TableType::KeyDefinition>& k){ return k->getName() == rid->getName(); });
                            if (it != subKeys.end()) {
                                size_t idx = getIdentifierTokenIndexFromRegion(rid->getRegion());
                                if (idx != static_cast<size_t>(-1)) {
                                    tokenCslReprMapping[idx] = *it;
                                }
                            }
                        }
                    }
                }
                mapIdentifiersInExpr(be->getLHS(), rootKeys);
                mapIdentifiersInExpr(be->getRHS(), rootKeys);
                return;
            }
            if (expr->getKind() == Expr::Kind::UnaryOp) {
                auto ue = std::static_pointer_cast<UnaryExpr>(expr);
                mapIdentifiersInExpr(ue->getOperand(), rootKeys);
                return;
            }
            if (expr->getKind() == Expr::Kind::TernaryOp) {
                auto te = std::static_pointer_cast<TernaryExpr>(expr);
                mapIdentifiersInExpr(te->getCondition(), rootKeys);
                mapIdentifiersInExpr(te->getTrueExpr(), rootKeys);
                mapIdentifiersInExpr(te->getFalseExpr(), rootKeys);
                return;
            }
            if (expr->getKind() == Expr::Kind::FunctionCall) {
                auto fc = std::static_pointer_cast<FunctionCallExpr>(expr);
                for (const auto& arg : fc->getArgs()) {
                    if (!arg) continue;
                    auto val = std::static_pointer_cast<FunctionArgExpr>(arg)->getValue();
                    if (std::holds_alternative<std::shared_ptr<Expr>>(val)) {
                        mapIdentifiersInExpr(std::get<std::shared_ptr<Expr>>(val), rootKeys);
                    }
                    else {
                        const auto& vec = std::get<std::vector<std::shared_ptr<Expr>>>(val);
                        for (const auto& e : vec) mapIdentifiersInExpr(e, rootKeys);
                    }
                }
                return;
            }
            if (expr->getKind() == Expr::Kind::Annotation) {
                auto ae = std::static_pointer_cast<AnnotationExpr>(expr);
                mapIdentifiersInExpr(ae->getTarget(), rootKeys);
                const auto& annArgs = ae->getAnnotation()->getArgs();
                for (const auto& e : annArgs) mapIdentifiersInExpr(e, rootKeys);
                return;
            }
        }

        void mapIdentifiersInAnnotations(const std::vector<std::shared_ptr<Annotation>>& annotations, const std::vector<std::shared_ptr<TableType::KeyDefinition>>& rootKeys) {
            for (const auto& a : annotations) {
                if (!a) continue;
                const auto& args = a->getArgs();
                for (const auto& e : args) mapIdentifiersInExpr(e, rootKeys);
            }
        }

        void mapIdentifiersInType(const std::shared_ptr<CSLType>& type, const std::vector<std::shared_ptr<TableType::KeyDefinition>>& rootKeys) {
            if (!type) return;
            switch (type->getKind()) {
            case CSLType::Kind::Primitive: {
                auto pt = std::static_pointer_cast<PrimitiveType>(type);
                mapIdentifiersInAnnotations(pt->getAnnotations(), rootKeys);
                break;
            }
            case CSLType::Kind::Array: {
                auto at = std::static_pointer_cast<ArrayType>(type);
                mapIdentifiersInType(at->getElementType(), rootKeys);
                break;
            }
            case CSLType::Kind::Union: {
                auto ut = std::static_pointer_cast<UnionType>(type);
                for (const auto& m : ut->getMemberTypes()) mapIdentifiersInType(m, rootKeys);
                break;
            }
            case CSLType::Kind::Table: {
                auto tt = std::static_pointer_cast<TableType>(type);
                const auto& subKeys = tt->getExplicitKeys();
                validateConstraintsSemantics(tt->getConstraints(), subKeys);
                for (const auto& k : subKeys) {
                    mapIdentifiersInAnnotations(k->getAnnotations(), subKeys);
                    mapIdentifiersInType(k->getType(), subKeys);
                }
                break;
            }
            default:
                break;
            }
        }

        void mapIdentifiersInConstraints(const std::vector<std::shared_ptr<Constraint>>& constraints, const std::vector<std::shared_ptr<TableType::KeyDefinition>>& rootKeys) {
            for (const auto& c : constraints) {
                if (!c) continue;
                switch (c->getKind()) {
                case Constraint::Kind::Conflict: {
                    auto cc = std::static_pointer_cast<ConflictConstraint>(c);
                    mapIdentifiersInExpr(cc->getFirstExpr(), rootKeys);
                    mapIdentifiersInExpr(cc->getSecondExpr(), rootKeys);
                    break;
                }
                case Constraint::Kind::Dependency: {
                    auto dc = std::static_pointer_cast<DependencyConstraint>(c);
                    mapIdentifiersInExpr(dc->getDependentExpr(), rootKeys);
                    mapIdentifiersInExpr(dc->getCondition(), rootKeys);
                    break;
                }
                case Constraint::Kind::Validate: {
                    auto vc = std::static_pointer_cast<ValidateConstraint>(c);
                    mapIdentifiersInExpr(vc->getExpr(), rootKeys);
                    break;
                }
                default:
                    break;
                }
            }
        }

        void advance() {
            if (position == input.end()) {
                errors.push_back({ "Unexpected end of input.", {} });
            }
            else {
                ++position;
            }
        }

        void expect(std::string token, const std::string& msg) {
            if (position == input.end()) {
                errors.push_back({ msg + ". Found end of input.", {} });
            }
            else if (position->value != token) {
                errors.push_back({ msg + ". Found: " + position->value, position->range });
            }
        }

        void expect(const std::vector<std::pair<std::string , std::string>>& tokenMsgPairs) {
            for (const auto& tokenMsgPair : tokenMsgPairs) {
                auto [token, msg] = tokenMsgPair;
                expect(token, msg);
            }
        }

        void expectType(std::string type, const std::string& msg) {
            if (position == input.end()) {
                errors.push_back({ msg + ". Found end of input.", {} });
            }
            else if (position->type != type) {
                errors.push_back({ msg + ". Found: " + position->value, position->range });
            }
        }

        void parseDelimitedArgs(std::vector<std::shared_ptr<Expr>>& args, const std::string& contextName, bool allowListArg) {
            while (position != input.end() && position->value != ")") {
                std::shared_ptr<Expr> arg;
                if (allowListArg && position->value == "[") {
                    auto argStart = position->range.start;
                    advance();
                    std::vector<std::shared_ptr<Expr>> elems;
                    while (position != input.end() && position->value != "]") {
                        elems.push_back(parseExpression());
                        if (position != input.end() && position->value == ",") advance();
                    }
                    if (position != input.end()) advance();
                    arg = std::make_shared<FunctionArgExpr>(elems, FilePosition::Region{ argStart, std::prev(position)->range.end });
                } else {
                    auto argStart = position->range.start;
                    arg = std::make_shared<FunctionArgExpr>(parseExpression(), FilePosition::Region{ argStart, std::prev(position)->range.end });
                }
                args.push_back(arg);
                if (position == input.end()) break;
                if (position->value == ",") {
                    advance();
                } else if (position->value != ")") {
                    errors.push_back({ std::string("Expected ',' or ')' in ") + contextName, position->range });
                    advance();
                }
            }
        }

        void parseDelimitedAnnotationArgs(std::vector<std::shared_ptr<Expr>>& args) {
            while (position != input.end() && position->value != ")") {
                args.push_back(parseExpression());
                if (position == input.end()) break;
                if (position->value == ",") {
                    advance();
                } else if (position->value != ")") {
                    errors.push_back({ "Expected ',' or ')' in annotation", position->range });
                    advance();
                }
            }
        }

        // Parse a single config schema
        std::shared_ptr<ConfigSchema> parseConfigSchema() {
            auto defStart = position->range.start;
            advance(); // Consume 'config'
            expectType("identifier", "Expected schema name after 'config'");
            size_t nameIndex = std::distance(input.begin(), position);
            if (position->type == "identifier") {
                tokenCslReprMapping[nameIndex];
            }
            std::string name = position->value;
            auto nameRegion = position->range;
            advance();
            auto schema = std::make_shared<ConfigSchema>(name, parseTableType(), FilePosition::Region{defStart, position->range.end}, nameRegion);
            if (tokenCslReprMapping.find(nameIndex) != tokenCslReprMapping.end()) {
                tokenCslReprMapping[nameIndex] = schema;
            }
            return schema;
        }

        // Parse a table type { ... }
        std::shared_ptr<TableType> parseTableType() {
            expect("{", "Expected '{' after schema name");
            advance();
            ++currentDepth;

            auto tableStart = position->range.start;
            std::vector<std::shared_ptr<TableType::KeyDefinition>> explicitKeys;
            std::shared_ptr<TableType::KeyDefinition> wildcardKey = nullptr;
            std::vector<std::shared_ptr<Constraint>> constraints;
            bool constraintsBlockSeen = false;

            while (position != input.end() && position->value != "}") {
                if (position->value == "constraints") {
                    if (constraintsBlockSeen) {
                        errors.push_back({ "Duplicate constraints block", position->range });
                    }
                    constraintsBlockSeen = true;
                    auto cs = parseConstraints();
                    constraints.insert(constraints.end(), cs.begin(), cs.end());
                }
                else {
                    if (position->value == "*") {
                        wildcardKey = parseWildcardKey();
                    }
                else if (position->type == "identifier") {
                    explicitKeys.push_back(parseKeyDefinition());
                }
                else if (position->type == "number") {
                    errors.push_back({ "Key name must be an identifier", position->range });
                    advance();
                }
                else {
                    advance();
                }
                }
            }

            expect("}", "Expected '}' after schema definition");
            auto tableEnd = position != input.end() ? position->range.end : (position != input.begin() ? std::prev(position)->range.end : FilePosition::Position{});
            mapIdentifiersInConstraints(constraints, explicitKeys);
            // Semantic validations
            validateConstraintsSemantics(constraints, explicitKeys);
            for (const auto& key : explicitKeys) {
                mapIdentifiersInAnnotations(key->getAnnotations(), explicitKeys);
                mapIdentifiersInType(key->getType(), explicitKeys);
                validateAnnotationsForType(key->getType());
                validateDefaultForKey(key);
            }
            identifierInExpr[currentDepth].clear();
            --currentDepth;
            advance(); // Consume '}'

            return std::make_shared<TableType>(explicitKeys, wildcardKey, constraints, FilePosition::Region{ tableStart, tableEnd });
        }

        std::shared_ptr<TableType::KeyDefinition> parseKeyDefinition() {
            if (position->type != "identifier") {
                errors.push_back({ "Key name must be an identifier", position->range });
            }
            auto name = position->value;
            size_t nameIndex = std::distance(input.begin(), position);
            if (position->type == "identifier") {
                tokenCslReprMapping[nameIndex];
            }
            auto defRegion = position->range;
            bool isOptional = false;
            advance();

            if (position != input.end() && position->value == "?") {
                isOptional = true;
                advance();
            }

            std::shared_ptr<CSLType> type;
            std::optional<std::pair<std::string, std::unique_ptr<Type::Type>>> defaultValue = std::nullopt;
            std::vector<std::shared_ptr<Annotation>> annotations;
            if (position != input.end() && position->value == ":") {
                advance();
                type = parseType();
                annotations = parseAnnotations(true);
                if (position != input.end() && position->value == "=") {
                    advance();
                    if (position != input.end() && (position->type == "string" || position->type == "number" || position->type == "boolean" || position->type == "datetime" || position->type == "duration")) {
                        defaultValue = std::make_pair(position->value, position->prop->clone());
                        advance();
                    }
                    else if (position != input.end() && (position->value == "+" || position->value == "-")) {
                        auto it = position;
                        auto nextIt = std::next(it);
                        if (nextIt != input.end() && nextIt->type == "number") {
                            std::string v = it->value + nextIt->value;
                            defaultValue = std::make_pair(v, nextIt->prop->clone());
                            advance();
                            advance();
                        }
                        else {
                            errors.push_back({ "Expected literal default value after '='", position->range });
                        }
                    }
                    else {
                        errors.push_back({ "Expected literal default value after '='", position->range });
                    }
                }
            }
            else if (position != input.end() && position->value == "=") {
                advance();
                std::string literalTokenValue;
                std::unique_ptr<Type::Type> literalTokenProp;
                FilePosition::Region literalRegion;
                bool hasLiteral = false;
                if (position != input.end() && (position->type == "string" || position->type == "number" || position->type == "boolean" || position->type == "datetime" || position->type == "duration")) {
                    literalTokenValue = position->value;
                    literalTokenProp = position->prop->clone();
                    literalRegion = position->range;
                    hasLiteral = true;
                    advance();
                }
                else if (position != input.end() && (position->value == "+" || position->value == "-")) {
                    auto it = position;
                    auto nextIt = std::next(it);
                    if (nextIt != input.end() && nextIt->type == "number") {
                        literalTokenValue = it->value + nextIt->value;
                        literalTokenProp = nextIt->prop->clone();
                        literalRegion = FilePosition::Region{ it->range.start, nextIt->range.end };
                        hasLiteral = true;
                        advance();
                        advance();
                    }
                }
                if (hasLiteral) {
                    defaultValue = std::make_pair(literalTokenValue, std::move(literalTokenProp));
                    PrimitiveType::Primitive primitiveType;
                    if (defaultValue->second->toString() == "Boolean") primitiveType = PrimitiveType::Primitive::Boolean;
                    else if (defaultValue->second->toString() == "Basic" || defaultValue->second->toString() == "MultiLineBasic" || defaultValue->second->toString() == "Raw" || defaultValue->second->toString() == "MultiLineRaw") primitiveType = PrimitiveType::Primitive::String;
                    else if (defaultValue->second->toString() == "OffsetDateTime" || defaultValue->second->toString() == "LocalDateTime" || defaultValue->second->toString() == "LocalDate" || defaultValue->second->toString() == "LocalTime") primitiveType = PrimitiveType::Primitive::Datetime;
                    else if (defaultValue->second->toString() == "Duration" || defaultValue->second->toString() == "NaN" || defaultValue->second->toString() == "Infinity" || defaultValue->second->toString() == "Integer" || defaultValue->second->toString() == "Float") primitiveType = PrimitiveType::Primitive::Number;
                    else primitiveType = PrimitiveType::Primitive::Number;
                    type = std::make_shared<PrimitiveType>(primitiveType, std::move(std::vector<std::pair<std::string, std::unique_ptr<Type::Type>>>{}), std::vector<std::shared_ptr<Annotation>>{}, literalRegion);
                    annotations = parseAnnotations(true);
                }
                else {
                    errors.push_back({ "Expected literal default value after '='", position->range });
                }
            }
            else {
                if (position != input.end() && position->type == "identifier") {
                    // Assume start of next key; tolerate missing ':'/'=' after previous key name
                }
                else {
                    expect({ { ":", "Expected ':' after key name" }, { "=", "Expected '=' after key name" } });
                    if (position != input.end()) advance();
                }
            }

            if (position != input.end() && position->value == ";") {
                advance();
            }
            else {
                // Be permissive when the next token clearly starts a new declaration or block
                if (position == input.end() || position->type == "identifier" || position->value == ":" || position->value == "*" || position->value == "constraints" || position->value == "}") {
                    // Do not emit error; assume line termination
                }
                else {
                    expect(";", "Expected ';' after key definition");
                    if (position != input.end()) advance();
                }
            }

            auto keyDef = std::make_shared<TableType::KeyDefinition>(name, false, isOptional, type, annotations, std::move(defaultValue), defRegion);
            if (tokenCslReprMapping.find(nameIndex) != tokenCslReprMapping.end()) {
                tokenCslReprMapping[nameIndex] = keyDef;
            }
            return keyDef;
        }

        // Parse wildcard key (*: type;)
        std::shared_ptr<TableType::KeyDefinition> parseWildcardKey() {
            auto defRegion = position->range;
            advance(); // Consume '*'
            expect(":", "Expected ':' after wildcard");
            advance();

            auto type = parseType();
            std::vector<std::shared_ptr<Annotation>> annotations = parseAnnotations(true);

            expect(";", "Expected ';' after wildcard key");
            advance();

            return std::make_shared<TableType::KeyDefinition>(
                "*", true, false, type, annotations, std::nullopt, defRegion
            );
        }

        // Parse type (primitive, enum, table, array, etc.)
        std::shared_ptr<CSLType> parseType() {
            auto typeStart = position->range.start;
            auto type = parsePostfixType();
            while (position != input.end() && position->value == "|") {
                advance(); // Consume '|'
                auto rightType = parsePostfixType();

                std::vector<std::shared_ptr<CSLType>> members;
                if (type->getKind() == CSLType::Kind::Union) {
                    auto unionType = std::static_pointer_cast<UnionType>(type);
                    members = unionType->getMemberTypes();
                }
                else {
                    members.push_back(type);
                }

                if (rightType->getKind() == CSLType::Kind::Union) {
                    auto rightUnion = std::static_pointer_cast<UnionType>(rightType);
                    members.insert(members.end(), rightUnion->getMemberTypes().begin(), rightUnion->getMemberTypes().end());
                }
                else {
                    members.push_back(rightType);
                }

                type = std::make_shared<UnionType>(members, FilePosition::Region{ typeStart, std::prev(position)->range.end });
            }
            // Disallow union mixing a primitive type and literal of the same primitive
            if (type && type->getKind() == CSLType::Kind::Union) {
                auto ut = std::static_pointer_cast<UnionType>(type);
                bool hasStringType = false, hasStringLiteral = false;
                bool hasNumberType = false, hasNumberLiteral = false;
                for (const auto& m : ut->getMemberTypes()) {
                    if (!m) continue;
                    if (m->getKind() == CSLType::Kind::Primitive) {
                        auto pm = std::static_pointer_cast<PrimitiveType>(m);
                        if (!pm->getAllowedValues().empty()) {
                            auto tname = pm->getAllowedValues()[0].second->toString();
                            if (tname == "Basic" || tname == "Raw" || tname == "MultiLineBasic" || tname == "MultiLineRaw") hasStringLiteral = true;
                            else hasNumberLiteral = true;
                        } else {
                            switch (pm->getPrimitive()) {
                            case PrimitiveType::Primitive::String: hasStringType = true; break;
                            case PrimitiveType::Primitive::Number: hasNumberType = true; break;
                            default: break;
                            }
                        }
                    }
                }
                if ((hasStringType && hasStringLiteral) || (hasNumberType && hasNumberLiteral)) {
                    errors.push_back({ "Union type cannot mix a primitive type with its literal", FilePosition::Region{ typeStart, std::prev(position)->range.end } });
                }
            }
            return type;
        }

        std::shared_ptr<CSLType> parsePostfixType() {
            auto type = parsePrimaryType();
            while (position != input.end() && position->value == "[") {
                auto typeStart = position->range.start;
                advance(); // Consume '['
                expect("]", "Expected ']' after array type");
                auto typeEnd = position->range.end;
                advance(); // Consume ']'
                type = std::make_shared<ArrayType>(type, FilePosition::Region{ typeStart, typeEnd });
            }
            return type;
        }

        std::shared_ptr<CSLType> parsePrimaryType() {
            std::vector<std::shared_ptr<CSLType>> members;
            auto typeStart = position->range.start;

            do {
                if (position->type == "number" ||
                    position->type == "boolean" ||
                    position->type == "string" ||
                    position->type == "datetime" ||
                    position->type == "duration") {
                    members.push_back(parseLiteralType());
                }
                else if (position->value == "string" ||
                    position->value == "number" ||
                    position->value == "boolean" ||
                    position->value == "datetime" ||
                    position->value == "duration") {
                    PrimitiveType::Primitive primitiveType;
                    if (position->value == "number") {
                        primitiveType = PrimitiveType::Primitive::Number;
                    }
                    else if (position->value == "boolean") {
                        primitiveType = PrimitiveType::Primitive::Boolean;
                    }
                    else if (position->value == "string") {
                        primitiveType = PrimitiveType::Primitive::String;
                    }
                    else if (position->value == "datetime") {
                        primitiveType = PrimitiveType::Primitive::Datetime;
                    }
                    else if (position->value == "duration") {
                        primitiveType = PrimitiveType::Primitive::Duration;
                    }
                    auto defRegion = position->range;
                    advance();
                    std::vector<std::shared_ptr<Annotation>> annotations;
                    if (position != input.end()) {
                        annotations = parseAnnotations(false);
                    }
                    members.push_back(std::make_shared<PrimitiveType>(
                        primitiveType, std::move(std::vector<std::pair<std::string, std::unique_ptr<Type::Type>>>{}), annotations, defRegion
                    ));
                }
                else if (position->value == "any{}") {
                    members.push_back(std::make_shared<AnyTableType>(position->range));
                    advance();
                }
                else if (position->value == "any[]") {
                    members.push_back(std::make_shared<AnyArrayType>(position->range));
                    advance();
                }
                else if (position->value == "{") {
                    members.push_back(parseTableType());
                }
                else if (position->value == "(") {
                    advance(); // Consume '('
                    members.push_back(parseType());
                    expect(")", "Expected ')' after parenthesized type");
                    advance();
                }
                else {
                    errors.push_back({ "Unexpected token in type: " + position->value, position->range });
                }

                // Check for union operator
                if (position != input.end())  {
                    if (position->value != "|") break;
                    advance(); // Consume '|'
                }

            } while (position != input.end());

            // Create appropriate type structure
            if (members.size() == 1) {
                return members[0];
            }
            return std::make_shared<UnionType>(members, FilePosition::Region{ typeStart, std::prev(position)->range.end });
        }

        std::shared_ptr<CSLType> parseLiteralType() {
            std::shared_ptr<CSLType> type;
            PrimitiveType::Primitive primitiveType;
            if (position->type == "number") {
                primitiveType = PrimitiveType::Primitive::Number;
            }
            else if (position->type == "boolean") {
                primitiveType = PrimitiveType::Primitive::Boolean;
            }
            else if (position->type == "string") {
                primitiveType = PrimitiveType::Primitive::String;
            }
            else if (position->type == "datetime") {
                primitiveType = PrimitiveType::Primitive::Datetime;
            }
            else if (position->type == "duration") {
                primitiveType = PrimitiveType::Primitive::Duration;
            }
            else {
                errors.push_back({ "Unexpected literal type: " + position->type, position->range });
                return nullptr;
            }
            std::vector<std::pair<std::string, std::unique_ptr<Type::Type>>> allowedValues;
            allowedValues.push_back(std::make_pair(position->value, position->prop->clone()));
            type = std::make_shared<PrimitiveType>(
                primitiveType,
                std::move(allowedValues),
                std::vector<std::shared_ptr<Annotation>>{},
                position->range
            );
            advance();
            return type;
        }

        bool isGlobalAnnotation(const std::string& token) {
            return
                token == "deprecated";
        }

        // Parse annotations (@min, @regex, etc.)
        std::vector<std::shared_ptr<Annotation>> parseAnnotations(bool isParsingGlobalAnnotations) {
            std::vector<std::shared_ptr<Annotation>> annotations;
            while (position != input.end() && position->value == "@" && std::next(position) != input.end() && isGlobalAnnotation(std::next(position)->value) == isParsingGlobalAnnotations) {
                annotations.push_back(parseAnnotation(isParsingGlobalAnnotations));
            }
            return annotations;
        }

        // Parse constraints block
        std::vector<std::shared_ptr<Constraint>> parseConstraints() {
            std::vector<std::shared_ptr<Constraint>> constraints;
            advance(); // Consume 'constraints'
            expect("{", "Expected '{' after constraints");
            advance();

            while (position != input.end() && position->value != "}") {
                if (position->value == "conflicts") {
                    constraints.push_back(parseConflictConstraint());
                }
                else if (position->value == "requires") {
                    constraints.push_back(parseDependencyConstraint());
                }
                else if (position->value == "validate") {
                    constraints.push_back(parseValidateConstraint());
                }
                else {
                    advance();
                }
            }

            advance(); // Consume '}'
            if (position != input.end() && position->value == ";") advance();
            return constraints;
        }

        // Parse conflict constraint (conflicts a with b;)
        std::shared_ptr<ConflictConstraint> parseConflictConstraint() {
            auto constraintStart = position->range.start;
            advance(); // Consume 'conflicts'
            auto firstExpr = parseExpression();
            expect("with", "Expected 'with' in conflict constraint");
            advance(); // Consume 'with'
            auto secondExpr = parseExpression();
            expect(";", "Expected ';' after conflict");
            auto constraintEnd = position->range.end;
            advance(); // Consume ';'
            return std::make_shared<ConflictConstraint>(firstExpr, secondExpr, FilePosition::Region{ constraintStart, constraintEnd });
        }

        // Parse dependency constraint (requires a => b;)
        std::shared_ptr<DependencyConstraint> parseDependencyConstraint() {
            auto constraintStart = position->range.start;
            advance(); // Consume 'requires'
            auto dependent = parseExpression();
            expect("=>", "Expected '=>' in dependency");
            advance();
            auto condition = parseExpression();
            expect(";", "Expected ';' after dependency");
            auto constraintEnd = position->range.end;
            advance();
            return std::make_shared<DependencyConstraint>(dependent, condition, FilePosition::Region{ constraintStart, constraintEnd });
        }

        // Parse validate constraint (validate expr;)
        std::shared_ptr<ValidateConstraint> parseValidateConstraint() {
            auto constraintStart = position->range.start;
            advance(); // Consume 'validate'
            auto expr = parseExpression();
            expect(";", "Expected ';' after validate");
            auto constraintEnd = position->range.end;
            advance();
            return std::make_shared<ValidateConstraint>(expr, FilePosition::Region{ constraintStart, constraintEnd });
        }

        // Parse expressions (recursive descent)
        std::shared_ptr<Expr> parseExpression(size_t minPrecedence = 17) {
            auto expressionStart = position->range.start;
            auto lhs = parseUnary();

            while (true) {
                if (position == input.end()) break;
                auto op = position->value;
                const std::unordered_set<std::string> binaryOperators = {
                    ".", "@", "[", "(",
                    "*", "/", "%", "+", "-",
                    "<<", ">>",
                    "<", "<=", ">", ">=",
                    "==", "!=", "&", "^", "|",
                    "&&", "||", "="
                };
                if (binaryOperators.find(op) == binaryOperators.end()) break;

                if (getPrecedence(op, 2) >= minPrecedence + getAssociativity(op)) break;

                if (op == "@") {
                    auto annotation = parseAnnotation(false);
                    lhs = std::make_shared<AnnotationExpr>(lhs, annotation, annotation.get()->getRegion() );
                }
                else {
                    advance();
                    auto rhs = parseExpression(getPrecedence(op, 2));
                    lhs = std::make_shared<BinaryExpr>(op, lhs, rhs, FilePosition::Region{ expressionStart, std::prev(position)->range.end });
                }
            }

            if (position != input.end() && position->value == "?") {
                advance();
                auto trueExpr = parseExpression();
                expect(":", "Expected ':' in ternary");
                if (position != input.end()) advance();
                auto falseExpr = parseExpression();
                lhs = std::make_shared<TernaryExpr>(lhs, trueExpr, falseExpr, FilePosition::Region{ expressionStart, std::prev(position)->range.end });
            }

            return lhs;
        }

        // Helper function to determine operator precedence
        size_t getPrecedence(const std::string& token, const size_t numOperand) {
            const std::unordered_map<std::string, size_t> unaryOpprecedenceMap = {
                {"~", 3}, {"!", 3}, {"+", 3}, {"-", 3}
            };
            const std::unordered_map<std::string, size_t> binaryOpPrecedenceMap = {
                {".", 1}, {"@", 1}, {"[", 2}, {"(", 2},
                {"*", 5}, {"/", 5}, {"%", 5}, {"+", 6}, {"-", 6},
                {"<<", 7}, {">>", 7},
                {"<", 8}, {"<=", 8}, {">", 8}, {">=", 8},
                {"==", 9}, {"!=", 9}, {"&", 10}, {"^", 11}, {"|", 12},
                {"&&", 13}, {"||", 14}, {"=", 15}
            };
            const std::unordered_map<std::string, size_t> otherOpPrecedenceMap = {
                {"]", 17}, {")", 17},
                {"?", 15}, {":", 17}
            };
            switch (numOperand) {
            case 1:
                return unaryOpprecedenceMap.contains(token) ? unaryOpprecedenceMap.at(token) : 17;
            case 2:
                return binaryOpPrecedenceMap.contains(token) ? binaryOpPrecedenceMap.at(token) : 17;
            default:
                return otherOpPrecedenceMap.contains(token) ? otherOpPrecedenceMap.at(token) : 17;
            }
        }

        size_t getAssociativity(const std::string& token) {
            const std::unordered_map<std::string, size_t> associativityMap = {
                {"~", 1}, {"!", 1}, {"+", 1}, {"-", 1},
                {".", 0}, {"@", 0}, {"[", 0}, {"(", 0},
                {"*", 0}, {"/", 0}, {"%", 0}, {"+", 0}, {"-", 0},
                {"<<", 0}, {">>", 0},
                {"<", 0}, {"<=", 0}, {">", 0}, {">=", 0},
                {"==", 0}, {"!=", 0}, {"&", 0}, {"^", 0}, {"|", 0},
                {"&&", 0}, {"||", 0}, {"=", 1},
                {"]", 0}, {")", 0},
                {"?", 1}, {":", 1}
            };
            return associativityMap.contains(token) ? associativityMap.at(token) : 0;
        }

        std::shared_ptr<Expr> parseUnary() {
            const std::unordered_set<std::string> unaryOperators = { "~", "!", "+", "-" };
            if (position == input.end()) return parsePrimary();
            auto op = position->value;
            if (position->type != "operator" || !unaryOperators.contains(op)) return parsePrimary();

            auto expressionStart = position->range.start;
            advance();
            auto expr = parseExpression(getPrecedence(op, 1));
            return std::make_shared<UnaryExpr>(op, expr, FilePosition::Region{ expressionStart, std::prev(position)->range.end });
        }

        std::shared_ptr<Expr> parsePrimary() {
            std::shared_ptr<Expr> expr;
            if (position == input.end()) {
                errors.push_back({ "Unexpected end of input.", {} });
            }
            else if (position->type == "string" ||
                position->type == "number" ||
                position->type == "boolean" ||
                position->type == "datetime" ||
                position->type == "duration") {
                expr = std::make_shared<LiteralExpr>(position->prop->clone(), position->value, position->range);
                advance();
            }
            else if (position->type == "identifier") {
                expr = std::make_shared<IdentifierExpr>(position->value, position->range);
                identifierInExpr[currentDepth][position->value].push_back(std::distance(input.begin(), position));
                advance();
            }
            else if (position->type == "keyword") {
                auto functionCallStart = position->range.start;
                std::string name = position->value;
                advance();
                expect("(", "Expected '(' after function name");
                if (position != input.end()) advance();
                std::vector<std::shared_ptr<Expr>> args;
                parseDelimitedArgs(args, "function call", true);
                if (position != input.end()) advance();
                expr = std::make_shared<FunctionCallExpr>(name, args, FilePosition::Region{ functionCallStart, std::prev(position)->range.end });
            }
            else if (position->value == "(") {
                advance(); // Consume '('
                expr = parseExpression();
                expect(")", "Expected ')' after expression");
                advance(); // Consume ')'
            }
            else {
                errors.push_back({ "Unexpected primary token: " + position->value, position->range });
            }
            return expr;
        }

        std::shared_ptr<Annotation> parseAnnotation(bool isParsingGlobalAnnotation) {
            auto annotationStart = position->range.start;
            advance(); // Consume '@'
            std::string name = position->value;
            if (isParsingGlobalAnnotation) {
                if (!isGlobalAnnotation(name)) {
                    errors.push_back({ "Found local annotation " + name + " when parsing global annotations", position->range });
                }
            }
            else {
                if (isGlobalAnnotation(name)) {
                    errors.push_back({ "Found global annotation " + name + " when parsing local annotations", position->range });
                }
            }
            advance();
            std::vector<std::shared_ptr<Expr>> args;
            if (position != input.end() && position->value == "(") {
                advance();
                parseDelimitedAnnotationArgs(args);
                if (position != input.end()) advance();
            }
            return std::make_shared<Annotation>(name, args, FilePosition::Region{ annotationStart, std::prev(position)->range.end });
        }
    };
}

std::tuple<std::vector<std::shared_ptr<CSL::ConfigSchema>>, std::vector<std::tuple<std::string, FilePosition::Region>>, std::vector<std::tuple<std::string, FilePosition::Region>>, std::unordered_map<size_t, std::variant<std::shared_ptr<CSL::ConfigSchema>, std::shared_ptr<CSL::TableType::KeyDefinition>>>> CslParserMain(Token::TokenList<std::string, std::unique_ptr<Type::Type>>& tokenList) {
    CSLParser::Parser rdparser(tokenList);
    auto schemas = rdparser.parseSchemas();
    return { schemas, rdparser.getErrors(), rdparser.getWarnings(), rdparser.getTokenCslReprMapping() };
}
