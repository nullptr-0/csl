#pragma once

#ifndef CSL_REPR_2_CSL_H
#define CSL_REPR_2_CSL_H

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include "CslRepresentation.h"

namespace CSL {

#ifndef DEF_GLOBAL
    extern void toCsl(const std::shared_ptr<CSL::ConfigSchema>& schema, std::ostream& output);
    extern std::string toCsl(const std::shared_ptr<CSL::ConfigSchema>& schema);
    extern std::string toCsl(const std::vector<std::shared_ptr<CSL::ConfigSchema>>& schemas);
#else
    static std::string getIndent(int level) {
        return std::string(level * 2, ' ');
    }

    static bool isIdentifier(const std::string& name) {
        if (name.empty()) return false;
        if (!((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_')) return false;
        for (size_t i = 1; i < name.size(); ++i) {
            char c = name[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
        }
        return true;
    }

    static std::string quoteIdentifier(const std::string& name) {
        if (name == "*") return name;
        if (isIdentifier(name)) return name;
        std::ostringstream oss;
        oss << '`';
        for (char c : name) {
            if (c == '`' || c == '\\') {
                oss << '\\' << c;
            } else {
                oss << c;
            }
        }
        oss << '`';
        return oss.str();
    }

    static void printExpr(const std::shared_ptr<Expr>& expr, std::ostream& os);

    static void printAnnotationArgs(const std::vector<std::shared_ptr<Expr>>& args, std::ostream& os) {
        bool first = true;
        for (const auto& a : args) {
            if (!first) os << ", ";
            first = false;
            printExpr(a, os);
        }
    }

    static void printAnnotations(const std::vector<std::shared_ptr<Annotation>>& annotations, std::ostream& os) {
        for (const auto& ann : annotations) {
            os << " @" << ann->getName() << '(';
            printAnnotationArgs(ann->getArgs(), os);
            os << ')';
        }
    }

    static void printType(const std::shared_ptr<CSLType>& type, std::ostream& os, int indent);

    static void printUnionType(const std::shared_ptr<UnionType>& ut, std::ostream& os, int indent) {
        const auto& members = ut->getMemberTypes();
        for (size_t i = 0; i < members.size(); ++i) {
            if (i) os << " | ";
            printType(members[i], os, indent);
        }
    }

    static void printTableBody(const std::shared_ptr<TableType>& table, std::ostream& os, int indent) {
        std::vector<std::string> keyOrder;
        for (const auto& kd : table->getExplicitKeys()) keyOrder.push_back(kd->getName());
        std::sort(keyOrder.begin(), keyOrder.end());
        std::unordered_map<std::string, std::shared_ptr<TableType::KeyDefinition>> map;
        for (const auto& kd : table->getExplicitKeys()) map[kd->getName()] = kd;
        for (const auto& keyName : keyOrder) {
            const auto& kd = map[keyName];
            os << getIndent(indent) << quoteIdentifier(kd->getIsWildcard() ? "*" : kd->getName());
            if (kd->getIsOptional()) os << '?';
            os << ": ";
            printType(kd->getType(), os, indent);
            if (kd->getDefaultValue().has_value()) {
                os << " = " << kd->getDefaultValue().value().first;
            }
            printAnnotations(kd->getAnnotations(), os);
            os << ";\n";
        }
        if (table->getWildcardKey()) {
            const auto& kdPtr = table->getWildcardKey();
            const auto& kd = *kdPtr;
            os << getIndent(indent) << "*";
            if (kd.getIsOptional()) os << '?';
            os << ": ";
            printType(kd.getType(), os, indent);
            if (kd.getDefaultValue().has_value()) {
                os << " = " << kd.getDefaultValue().value().first;
            }
            printAnnotations(kd.getAnnotations(), os);
            os << ";\n";
        }
        const auto& constraints = table->getConstraints();
        if (!constraints.empty()) {
            os << getIndent(indent) << "constraints {\n";
            for (const auto& c : constraints) {
                if (c->getKind() == Constraint::Kind::Conflict) {
                    auto cc = std::static_pointer_cast<ConflictConstraint>(c);
                    os << getIndent(indent + 1) << "conflicts ";
                    printExpr(cc->getFirstExpr(), os);
                    os << " with ";
                    printExpr(cc->getSecondExpr(), os);
                    os << ";\n";
                } else if (c->getKind() == Constraint::Kind::Dependency) {
                    auto dc = std::static_pointer_cast<DependencyConstraint>(c);
                    os << getIndent(indent + 1) << "requires ";
                    printExpr(dc->getDependentExpr(), os);
                    os << " => ";
                    printExpr(dc->getCondition(), os);
                    os << ";\n";
                } else if (c->getKind() == Constraint::Kind::Validate) {
                    auto vc = std::static_pointer_cast<ValidateConstraint>(c);
                    os << getIndent(indent + 1) << "validate ";
                    printExpr(vc->getExpr(), os);
                    os << ";\n";
                }
            }
            os << getIndent(indent) << "};\n";
        }
    }

    static void printArrayType(const std::shared_ptr<ArrayType>& at, std::ostream& os, int indent) {
        auto elem = at->getElementType();
        if (elem->getKind() == CSLType::Kind::Table) {
            os << "{\n";
            printTableBody(std::static_pointer_cast<TableType>(elem), os, indent + 1);
            os << getIndent(indent) << "}";
            os << "[]";
        } else {
            printType(elem, os, indent);
            os << "[]";
        }
    }

    static void printType(const std::shared_ptr<CSLType>& type, std::ostream& os, int indent) {
        switch (type->getKind()) {
        case CSLType::Kind::Primitive: {
            auto pt = std::static_pointer_cast<PrimitiveType>(type);
            const auto& allowed = pt->getAllowedValues();
            if (!allowed.empty()) {
                for (size_t i = 0; i < allowed.size(); ++i) {
                    if (i) os << " | ";
                    os << allowed[i].first;
                }
            } else {
                switch (pt->getPrimitive()) {
                case PrimitiveType::Primitive::String: os << "string"; break;
                case PrimitiveType::Primitive::Number: os << "number"; break;
                case PrimitiveType::Primitive::Boolean: os << "boolean"; break;
                case PrimitiveType::Primitive::Datetime: os << "datetime"; break;
                case PrimitiveType::Primitive::Duration: os << "duration"; break;
                }
            }
            printAnnotations(pt->getAnnotations(), os);
            break;
        }
        case CSLType::Kind::Table: {
            auto tt = std::static_pointer_cast<TableType>(type);
            os << "{\n";
            printTableBody(tt, os, indent + 1);
            os << getIndent(indent) << "}";
            break;
        }
        case CSLType::Kind::Array: {
            printArrayType(std::static_pointer_cast<ArrayType>(type), os, indent);
            break;
        }
        case CSLType::Kind::Union: {
            printUnionType(std::static_pointer_cast<UnionType>(type), os, indent);
            break;
        }
        case CSLType::Kind::AnyTable: {
            os << "any{}";
            break;
        }
        case CSLType::Kind::AnyArray: {
            os << "any[]";
            break;
        }
        case CSLType::Kind::Invalid: {
            os << "";
            break;
        }
        }
    }

    static void printFunctionCall(const std::shared_ptr<FunctionCallExpr>& f, std::ostream& os) {
        os << f->getFuncName() << '(';
        for (size_t i = 0; i < f->getArgs().size(); ++i) {
            if (i) os << ", ";
            printExpr(f->getArgs()[i], os);
        }
        os << ')';
    }

    static void printFunctionArg(const std::shared_ptr<FunctionArgExpr>& a, std::ostream& os) {
        const auto& v = a->getValue();
        if (std::holds_alternative<std::shared_ptr<Expr>>(v)) {
            printExpr(std::get<std::shared_ptr<Expr>>(v), os);
        } else {
            const auto& list = std::get<std::vector<std::shared_ptr<Expr>>>(v);
            os << '[';
            for (size_t i = 0; i < list.size(); ++i) {
                if (i) os << ", ";
                printExpr(list[i], os);
            }
            os << ']';
        }
    }

    static void printExpr(const std::shared_ptr<Expr>& expr, std::ostream& os) {
        if (!expr) return;
        switch (expr->getKind()) {
        case Expr::Kind::BinaryOp: {
            auto b = std::static_pointer_cast<BinaryExpr>(expr);
            printExpr(b->getLHS(), os);
            os << ' ' << b->getOp() << ' ';
            printExpr(b->getRHS(), os);
            break;
        }
        case Expr::Kind::UnaryOp: {
            auto u = std::static_pointer_cast<UnaryExpr>(expr);
            os << u->getOp();
            printExpr(u->getOperand(), os);
            break;
        }
        case Expr::Kind::TernaryOp: {
            auto t = std::static_pointer_cast<TernaryExpr>(expr);
            printExpr(t->getCondition(), os);
            os << " ? ";
            printExpr(t->getTrueExpr(), os);
            os << " : ";
            printExpr(t->getFalseExpr(), os);
            break;
        }
        case Expr::Kind::Literal: {
            auto l = std::static_pointer_cast<LiteralExpr>(expr);
            os << l->getValue();
            break;
        }
        case Expr::Kind::Identifier: {
            auto id = std::static_pointer_cast<IdentifierExpr>(expr);
            os << id->getName();
            break;
        }
        case Expr::Kind::FunctionArg: {
            printFunctionArg(std::static_pointer_cast<FunctionArgExpr>(expr), os);
            break;
        }
        case Expr::Kind::FunctionCall: {
            printFunctionCall(std::static_pointer_cast<FunctionCallExpr>(expr), os);
            break;
        }
        case Expr::Kind::Annotation: {
            auto ae = std::static_pointer_cast<AnnotationExpr>(expr);
            printExpr(ae->getTarget(), os);
            os << ' ' << '@' << ae->getAnnotation()->getName() << '(';
            printAnnotationArgs(ae->getAnnotation()->getArgs(), os);
            os << ')';
            break;
        }
        }
    }

    void toCsl(const std::shared_ptr<CSL::ConfigSchema>& schema, std::ostream& output) {
        output << "config " << schema->getName() << " {\n";
        auto root = schema->getRootTable();
        printTableBody(root, output, 1);
        output << "}";
    }

    std::string toCsl(const std::shared_ptr<CSL::ConfigSchema>& schema) {
        std::ostringstream out;
        toCsl(schema, out);
        return out.str();
    }

    std::string toCsl(const std::vector<std::shared_ptr<CSL::ConfigSchema>>& schemas) {
        std::ostringstream out;
        for (const auto& schema : schemas) {
            toCsl(schema, out);
            out << "\n\n";
        }
        return out.str();
    }
#endif
}

#endif
