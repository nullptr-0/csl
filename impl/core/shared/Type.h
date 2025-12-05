#pragma once

#ifndef TYPE_H
#define TYPE_H

#include <string>
#include <sstream>
#include <memory>
#include <vector>
#include <tuple>

namespace Type
{
    class Type {
    public:
        Type() {}
        virtual ~Type() {}
        virtual std::string toString() const = 0;
        virtual std::unique_ptr<Type> clone() const = 0;
    };

    class Invalid : public Type {
    public:
        Invalid() {}
        ~Invalid() {}
        std::string toString() const override {
            return "Invalid";
        }

        std::unique_ptr<Type> clone() const override {
            return std::make_unique<Invalid>();
        }
    };

    class Valid : public Type {
    public:
        Valid() {}
        virtual ~Valid() {}
    };

    class Boolean : public Valid {
    public:
        Boolean() {}
        ~Boolean() {}
        std::string toString() const override {
            return "Boolean";
        }

        std::unique_ptr<Type> clone() const override {
            return std::make_unique<Boolean>();
        }
    };

    class Numeric : public Valid {
    public:
        Numeric() {}
        virtual ~Numeric() {}
    };

    class Integer : public Numeric {
    public:
        Integer() {}
        ~Integer() {}
        std::string toString() const override {
            return "Integer";
        }

        std::unique_ptr<Type> clone() const override {
            return std::make_unique<Integer>();
        }
    };

    class Float : public Numeric {
    public:
        Float() {}
        ~Float() {}
        std::string toString() const override {
            return "Float";
        }

        std::unique_ptr<Type> clone() const override {
            return std::make_unique<Float>();
        }
    };

    class SpecialNumber : public Numeric {
    public:
        enum SpecialNumberType {
            NaN,
            Infinity
        };

        SpecialNumber(SpecialNumberType type) : type(type) {}
        ~SpecialNumber() {}

        SpecialNumberType getType() const {
            return type;
        }

        void setType(SpecialNumberType type) {
            this->type = type;
        }

        std::string toString() const override {
            return type == NaN ? "NaN" : "Infinity";
        }

        std::unique_ptr<Type> clone() const override {
            return std::make_unique<SpecialNumber>(type);
        }

    protected:
        SpecialNumberType type;
    };

    class String : public Valid {
    public:
        enum StringType {
            Basic,
            MultiLineBasic,
            Raw,
            MultiLineRaw
        };

        String(StringType type) : type(type) {}
        ~String() {}

        StringType getType() const {
            return type;
        }

        void setType(StringType type) {
            this->type = type;
        }

        std::string toString() const override {
            return type == Basic ? "Basic" : type == MultiLineBasic ? "MultiLineBasic" : type == Raw ? "Raw" : "MultiLineRaw";
        }

        std::unique_ptr<Type> clone() const override {
            return std::make_unique<String>(type);
        }

    protected:
        StringType type;
    };

    class DateTime : public Valid {
    public:
        enum DateTimeType {
            OffsetDateTime,
            LocalDateTime,
            LocalDate,
            LocalTime
        };

        DateTime(DateTimeType type) : type(type) {}
        ~DateTime() {}

        DateTimeType getType() const {
            return type;
        }

        void setType(DateTimeType type) {
            this->type = type;
        }

        std::string toString() const override {
            return type == OffsetDateTime ? "OffsetDateTime" : type == LocalDateTime ? "LocalDateTime" : type == LocalDate ? "LocalDate" : "LocalTime";
        }

        std::unique_ptr<Type> clone() const override {
            return std::make_unique<DateTime>(type);
        }

    protected:
        DateTimeType type;
    };

    class Duration : public Valid {
    public:
        Duration() {}
        ~Duration() {}
        std::string toString() const override {
            return "Duration";
        }

        std::unique_ptr<Type> clone() const override {
            return std::make_unique<Duration>();
        }
    };
};

#endif
