// Strict ECMA-404 compliant JSON parser & printer (single-header)
//
// Public entry points:
//   jsonio::Value jsonio::parseText(std::string_view text, std::size_t maxDepth = 1000);
//   std::string   escape           (std::string_view s)
//   std::string   jsonio::dump     (const jsonio::Value& v);
//
// duplicate-key helpers:
//   const Value*  jsonio::getFirst (const Value& obj, std::string_view key);
//   const Value*  jsonio::getLast  (const Value& obj, std::string_view key);

#pragma once

#ifndef JSON_IO_HPP
#define JSON_IO_HPP

#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <charconv>
#include <algorithm>
#include <type_traits>
#include <cmath>
#include <bit>
#include <initializer_list>
#include <iterator>

namespace jsonio {

// ---------- Exact decimal core (value = sign ? -1: +1 * unscaled * 10^exp10) ----------
struct BigInt {
    // little-endian base-1e9 limbs (limbs[0] is least significant)
    static constexpr uint32_t BASE = 1000000000u;
    std::vector<uint32_t> limb;

    bool isZero() const { return limb.empty(); }
    void trim() { while (!limb.empty() && limb.back() == 0) limb.pop_back(); }

    static BigInt from_uint64(unsigned long long v) {
        BigInt n;
        while (v) {
            n.limb.push_back(static_cast<uint32_t>(v % BASE));
            v /= BASE;
        }
        n.trim();
        return n;
    }

    // multiply by small k (k <= 1e9)
    void mul_small(uint32_t k) {
        if (isZero() || k == 1u) return;
        if (k == 0u) { limb.clear(); return; }
        uint64_t carry = 0;
        for (auto& x : limb) {
            uint64_t v = uint64_t(x) * k + carry;
            x = uint32_t(v % BASE);
            carry = v / BASE;
        }
        if (carry) limb.push_back(uint32_t(carry));
    }

    // multiply by 2^k (k >= 0) – simple repeated doubling is fine here
    void mul_pow2(int k) {
        for (int i = 0; i < k; ++i) mul_small(2);
    }

    // n = n*10 + d (0..9)
    void mul10_add(uint32_t d) {
        uint64_t carry = d;
        for (auto& x : limb) {
            uint64_t v = uint64_t(x) * 10u + carry;
            x = uint32_t(v % BASE);
            carry = v / BASE;
        }
        if (carry) limb.push_back(uint32_t(carry));
    }

    static BigInt fromDecimalDigits(std::string_view digits) {
        BigInt n;
        for (char c : digits) {
            n.mul10_add(uint32_t(c - '0'));
        }
        n.trim();
        return n;
    }

    // Divide by small m (2..1e9). Returns remainder; updates *this to quotient.
    uint32_t div_small(uint32_t m) {
        uint64_t rem = 0;
        for (size_t i = limb.size(); i-- > 0;) {
            uint64_t cur = limb[i] + rem * BASE;
            limb[i] = uint32_t(cur / m);
            rem = cur % m;
        }
        trim();
        return uint32_t(rem);
    }

    std::string toDecimalString() const {
        if (isZero()) return "0";
        BigInt tmp = *this;
        std::vector<uint32_t> parts;
        while (!tmp.isZero()) parts.push_back(tmp.div_small(BASE));
        std::string s = std::to_string(parts.back());
        for (size_t i = parts.size(); i-- > 0;) {
            if (i == 0) break;
            char buf[10];
            std::snprintf(buf, sizeof(buf), "%09u", parts[i-1]);
            s += buf;
        }
        return s;
    }

    // Try to convert to unsigned long long; returns false on overflow.
    bool to_ull(unsigned long long& out) const {
        std::string d = toDecimalString();
        unsigned long long v = 0;
        for (char c : d) {
            uint32_t dig = uint32_t(c - '0');
            if (v > std::numeric_limits<unsigned long long>::max() / 10ULL) return false;
            v *= 10ULL;
            if (v > std::numeric_limits<unsigned long long>::max() - dig) return false;
            v += dig;
        }
        out = v;
        return true;
    }
};

struct Decimal {
    bool     neg = false;
    BigInt   unscaled;     // integer made from all digits (int+frac) with zeros stripped
    int32_t  exp10 = 0;    // base-10 exponent after normalizing trailing zeros

    Decimal() = default;

    // ----- Integral (lossless) -----
    template <typename I, typename = std::enable_if_t<std::is_integral_v<I>>>
    /*implicit*/ Decimal(I x) {
        using U = std::make_unsigned_t<I>;
        if constexpr (std::is_signed_v<I>) {
            if (x < 0) { neg = true; }
        }
        unsigned long long ux;
        if constexpr (std::is_signed_v<I>) {
            // handle min value safely
            using W = std::conditional_t<(sizeof(I) < sizeof(long long)), long long, I>;
            W wx = static_cast<W>(x);
            unsigned long long mag = (wx < 0) ?
                                     (unsigned long long)(-(int64_t)wx) : (unsigned long long)wx;
            ux = mag;
        } else {
            ux = static_cast<unsigned long long>(x);
        }
        unscaled = BigInt::from_uint64(ux);
        exp10 = 0;
        if (unscaled.isZero()) neg = false; // canonicalize -0 -> +0
    }

    // Helpers for IEEE-754 decoding (float/double)
    static void from_ieee_double_impl(double v, Decimal& out) {
        // Reject NaN/Inf per JSON
        if (!std::isfinite(v)) throw std::invalid_argument("NaN/Inf not allowed in JSON numbers");
        // Zero fast path
        if (v == 0.0) { out.neg = false; out.unscaled.limb.clear(); out.exp10 = 0; return; }

        uint64_t bits = std::bit_cast<uint64_t>(v);
        unsigned sign = unsigned(bits >> 63);
        unsigned exp  = unsigned((bits >> 52) & 0x7FFu);
        uint64_t frac = bits & ((uint64_t(1) << 52) - 1);
        const int bias = 1023;
        const int mant_bits = 52;

        out.neg = (sign != 0);
        uint64_t m;
        int e2;
        if (exp == 0) {
            // subnormal: value = frac * 2^(1-bias-mant_bits)
            m  = frac;
            e2 = 1 - bias - mant_bits; // -1074
        } else {
            // normal: value = (2^mant_bits + frac) * 2^(exp-bias-mant_bits)
            m  = (uint64_t(1) << mant_bits) | frac;
            e2 = int(exp) - bias - mant_bits;
        }
        // Build exact decimal:
        out.unscaled = BigInt::from_uint64(m);
        out.exp10 = 0;
        if (e2 >= 0) {
            out.unscaled.mul_pow2(e2);          // multiply by 2^e2
        } else {
            int k = -e2;
            for (int i = 0; i < k; ++i) out.unscaled.mul_small(5); // *5^k
            out.exp10 = -k;                  // *10^{-k}
        }
        // Normalize by removing trailing *10 factors where possible
        // (divide unscaled by 10 while divisible; bump exp10)
        if (!out.unscaled.isZero()) {
            while (true) {
                uint32_t r = out.unscaled.div_small(10);
                if (r == 0) { ++out.exp10; } else { out.unscaled.mul_small(10); break; }
            }
        } else {
            out.neg = false;
            out.exp10 = 0;
        }
    }

    static void from_ieee_float_impl(float v, Decimal& out) {
        if (!std::isfinite(v)) throw std::invalid_argument("NaN/Inf not allowed in JSON numbers");
        if (v == 0.0f) { out.neg = false; out.unscaled.limb.clear(); out.exp10 = 0; return; }
        uint32_t bits = std::bit_cast<uint32_t>(v);
        unsigned sign = unsigned(bits >> 31);
        unsigned exp  = unsigned((bits >> 23) & 0xFFu);
        uint32_t frac = bits & ((uint32_t(1) << 23) - 1);
        const int bias = 127;
        const int mant_bits = 23;
        out.neg = (sign != 0);
        uint64_t m;
        int e2;
        if (exp == 0) {
            m  = frac;
            e2 = 1 - bias - mant_bits; // -149
        } else {
            m  = (uint64_t(1) << mant_bits) | frac;
            e2 = int(exp) - bias - mant_bits;
        }
        out.unscaled = BigInt::from_uint64(m);
        out.exp10 = 0;
        if (e2 >= 0) {
            out.unscaled.mul_pow2(e2);
        } else {
            int k = -e2;
            for (int i = 0; i < k; ++i) out.unscaled.mul_small(5);
            out.exp10 = -k;
        }
        if (!out.unscaled.isZero()) {
            while (true) {
                uint32_t r = out.unscaled.div_small(10);
                if (r == 0) { ++out.exp10; } else { out.unscaled.mul_small(10); break; }
            }
        } else {
            out.neg = false;
            out.exp10 = 0;
        }
    }

    // ----- Floating (exact for float/double) -----
    /*implicit*/ Decimal(double x)  { from_ieee_double_impl(x, *this); }
    /*implicit*/ Decimal(float  x)  { from_ieee_float_impl(x,  *this); }
};

// Emit a canonical JSON number for Decimal.
inline void emitNumber(const Decimal& d, std::string& out) {
    if (d.unscaled.isZero()) { out += "0"; return; }
    if (d.neg) out.push_back('-');
    std::string s = d.unscaled.toDecimalString(); // digits only
    // Choose between fixed and scientific; pick the shorter textual form.
    // fixed form
    long long k = d.exp10;
    auto emit_fixed = [&](std::string& o){
        if (k >= 0) {
            o += s;
            o.append(static_cast<size_t>(k), '0');
            return;
        }
        // insert decimal point
        long long pos = (long long)s.size() + k; // k<0
        if (pos > 0) {
            o.append(s.data(), size_t(pos));
            o.push_back('.');
            o.append(s.data()+pos, s.data()+s.size());
            // trim trailing zeros in fraction
            while (o.back()=='0') o.pop_back();
            if (o.back()=='.') o.pop_back(); // no bare trailing dot
        } else {
            o += "0.";
            o.append(static_cast<size_t>(-pos), '0');
            o += s;
        }
    };
    auto fixed_len = [&]()->size_t{
        if (k >= 0) return s.size() + size_t(k);
        long long pos = (long long)s.size() + k;
        if (pos > 0) return s.size() + 1;            // dot
        return 2 + size_t(-pos) + s.size();          // "0."+zeros+digits
    };
    // scientific form: d[0][.rest]e[+/-]E
    long long E = (long long)s.size() - 1 + k;
    auto emit_sci = [&](std::string& o){
        o.push_back(s[0]);
        if (s.size() > 1) {
            o.push_back('.');
            // trim trailing zeros in mantissa
            size_t last = s.size()-1;
            while (last>0 && s[last]=='0') --last;
            o.append(s.data()+1, s.data()+last+1);
            if (o.back()=='.') o.pop_back();
        }
        o.push_back('e');
        if (E < 0) { o.push_back('-'); E = -E; }
        else       { /* no '+' to keep output short */ }
        // append exponent digits
        char buf[32]; std::snprintf(buf, sizeof(buf), "%lld", (long long)E);
        o += buf;
    };
    auto sci_len = [&]()->size_t{
        size_t mant = (s.size()>1 ? 2 + (s.find_last_not_of('0')==0 ? 0
                                      : (s.find_last_not_of('0')==std::string::npos ? 0
                                      : (s.find_last_not_of('0') - 0))) : 1);
        // approximate conservatively: 1 digit + optional ".rest" + 'e' + up to ~11 exp chars
        // (we won't overfit; we actually build below)
        return 1 /*d0*/ + (s.size()>1 ? 1 + (s.size()-1) : 0) + 1 /*e*/ + (E<0?1:0) + 11;
    };
    // Pick shorter rendering (ties go to fixed).
    bool use_sci = (sci_len() < fixed_len());
    if (use_sci) emit_sci(out); else emit_fixed(out);
}

// ==== JSON Value ===============================================================================

template<typename ValueType>
struct KeyPairTpl {
    std::string name;
    ValueType   value;
};

struct Value {
    using array_t  = std::vector<Value>;
    using KeyPair  = KeyPairTpl<Value>;
    using object_t = std::vector<KeyPair>; // preserves order & duplicates

    // Number is stored as original, validated JSON number lexeme for lossless round-trip.
    // Accessors can parse on-demand.
    using number_t = Decimal;

    enum class Type { Null, Bool, Number, String, Array, Object };

    Type        type = Type::Null;
    bool        b    = false;
    std::string s;
    Decimal     n;
    array_t     a;
    object_t    o;

    static Value Null()                 { return {}; }
    static Value Bool(bool v)           { Value x; x.type=Type::Bool;   x.b=v; return x; }
    static Value Number(number_t d)     { Value x; x.type=Type::Number; x.n=std::move(d); return x; }
    template <typename T,
              typename = std::enable_if_t<std::is_integral_v<T> ||
                                          std::is_same_v<T,double> ||
                                          std::is_same_v<T,float>>>
    static Value Number(T v)            { return Number(Decimal(v)); }
    static Value String(std::string v)  { Value x; x.type=Type::String; x.s=std::move(v); return x; }
    static Value Array(array_t v)       { Value x; x.type=Type::Array;  x.a=std::move(v); return x; }
    static Value Object(object_t v)     { Value x; x.type=Type::Object; x.o=std::move(v); return x; }

    bool isNull()   const { return type==Type::Null; }
    bool isBool()   const { return type==Type::Bool; }
    bool isNumber() const { return type==Type::Number; }
    bool isString() const { return type==Type::String; }
    bool isArray()  const { return type==Type::Array; }
    bool isObject() const { return type==Type::Object; }

    // ----- Indexing API -------------------------------------------------------------------------
    // Throws std::logic_error if used on a non-Array/Object.
    // Array indexing: throws std::out_of_range if idx >= size().
    // Object indexing: returns the *last* value for 'key' (duplicate keys supported);
    //                  throws std::out_of_range if the key is not present.

private:
    static constexpr const char* typeName(Type t) noexcept {
        switch (t) {
            case Type::Null:   return "Null";
            case Type::Bool:   return "Bool";
            case Type::Number: return "Number";
            case Type::String: return "String";
            case Type::Array:  return "Array";
            case Type::Object: return "Object";
        }
        return "?";
    }
    const char* typeName() const noexcept { return typeName(type); }

public:
    // Array []
    Value& operator[](size_t idx) {
        if (!isArray()) {
            throw std::logic_error(std::string("jsonio::Value: operator[](size_t) requires Array, got ")
                                   + typeName());
        }
        if (idx >= a.size()) {
            throw std::out_of_range("jsonio::Value: array index out of range");
        }
        return a[idx];
    }
    const Value& operator[](size_t idx) const {
        if (!isArray()) {
            throw std::logic_error(std::string("jsonio::Value: operator[](size_t) requires Array, got ")
                                   + typeName());
        }
        if (idx >= a.size()) {
            throw std::out_of_range("jsonio::Value: array index out of range");
        }
        return a[idx];
    }

    // Object []
    Value& operator[](std::string_view key) {
        if (!isObject()) {
            throw std::logic_error(std::string("jsonio::Value: operator[](string_view) requires Object, got ")
                                   + typeName());
        }
        for (std::size_t i = o.size(); i > 0; --i) {
            if (o[i-1].name == key) return o[i-1].value; // last match wins
        }
        throw std::out_of_range("jsonio::Value: key not found");
    }
    const Value& operator[](std::string_view key) const {
        if (!isObject()) {
            throw std::logic_error(std::string("jsonio::Value: operator[](string_view) requires Object, got ")
                                   + typeName());
        }
        for (std::size_t i = o.size(); i > 0; --i) {
            if (o[i-1].name == key) return o[i-1].value; // last match wins
        }
        throw std::out_of_range("jsonio::Value: key not found");
    }
    // Convenience for C-string keys
    Value& operator[](const char* key)             { return (*this)[std::string_view(key ? key : "")]; }
    const Value& operator[](const char* key) const { return (*this)[std::string_view(key ? key : "")]; }

    // ---- Container-style helpers for Array/Object ---------------------------------------------
    // size / empty / reserve / clear (throws if not Array or Object)
    inline size_t size() const {
        if (isArray())  return a.size();
        if (isObject()) return o.size();
        throw std::logic_error(std::string("jsonio::Value: size() requires Array or Object, got ")
                               + typeName());
    }

    inline bool empty() const {
        if (isArray())  return a.empty();
        if (isObject()) return o.empty();
        throw std::logic_error(std::string("jsonio::Value: empty() requires Array or Object, got ")
                               + typeName());
    }

    inline void reserve(size_t n) {
        if (isArray())  { a.reserve(n); return; }
        if (isObject()) { o.reserve(n); return; }
        throw std::logic_error(std::string("jsonio::Value: reserve(size_t) requires Array or Object, got ")
                               + typeName());
    }

    inline void clear() {
        if (isArray())  { a.clear(); return; }
        if (isObject()) { o.clear(); return; }
        throw std::logic_error(std::string("jsonio::Value: clear() requires Array or Object, got ")
                               + typeName());
    }

    // Array-specific push_back / emplace_back
    inline void push_back(const Value& v_) {
        if (!isArray()) {
            throw std::logic_error(std::string("jsonio::Value: push_back(const Value&) requires Array, got ")
                                   + typeName());
        }
        a.push_back(v_);
    }

    inline void push_back(Value&& v_) {
        if (!isArray()) {
            throw std::logic_error(std::string("jsonio::Value: push_back(Value&&) requires Array, got ")
                                   + typeName());
        }
        a.push_back(std::move(v_));
    }

    template <class... Args>
    inline Value& emplace_back(Args&&... args) {
        if (!isArray()) {
            throw std::logic_error(std::string("jsonio::Value: emplace_back(Args&&...) requires Array, got ")
                                   + typeName());
        }
        return a.emplace_back(std::forward<Args>(args)...);
    }

    // Object-specific push_back / emplace_back
    inline void push_back(const KeyPair& kv) {
        if (!isObject()) {
            throw std::logic_error(std::string("jsonio::Value: push_back(const KeyPair&) requires Object, got ")
                                   + typeName());
        }
        o.push_back(kv);
    }

    inline void push_back(KeyPair&& kv) {
        if (!isObject()) {
            throw std::logic_error(std::string("jsonio::Value: push_back(KeyPair&&) requires Object, got ")
                                   + typeName());
        }
        o.push_back(std::move(kv));
    }

    inline void push_back(std::string name, Value v) {
        if (!isObject()) {
            throw std::logic_error(std::string("jsonio::Value: push_back(std::string, Value) requires Object, got ")
                                   + typeName());
        }
        o.push_back(KeyPair{std::move(name), std::move(v)});
    }

    inline Value::KeyPair& emplace_back(std::string name, Value v) {
        if (!isObject()) {
            throw std::logic_error(std::string("jsonio::Value: emplace_back(std::string, Value) requires Object, got ")
                                   + typeName());
        }
        o.push_back(KeyPair{std::move(name), std::move(v)});
        return o.back();
    }

    // get<T>() (value)
    template <typename T>
    T get() const {
        // Null
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            if (!isNull()) throw std::logic_error("json get<std::nullptr_t>: value is not null");
            return nullptr;
        }
        // Bool
        else if constexpr (std::is_same_v<T, bool>) {
            if (!isBool()) throw std::logic_error("json get<bool>: value is not a boolean");
            return b;
        }
        // String
        else if constexpr (std::is_same_v<T, std::string>) {
            if (!isString()) throw std::logic_error("json get<std::string>: value is not a string");
            return s;
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            if (!isString()) throw std::logic_error("json get<std::string_view>: value is not a string");
            return std::string_view{s};
        }
        // Array / Object
        else if constexpr (std::is_same_v<T, array_t>) {
            if (!isArray()) throw std::logic_error("json get<array_t>: value is not an array");
            return a;
        } else if constexpr (std::is_same_v<T, object_t>) {
            if (!isObject()) throw std::logic_error("json get<object_t>: value is not an object");
            return o;
        }
        // Number (exact)
        else if constexpr (std::is_same_v<T, number_t>) {
            if (!isNumber()) throw std::logic_error("json get<Decimal>: value is not a number");
            return n;
        }
        // Integral types (exact integer only; range-checked)
        else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            if (!isNumber()) throw std::logic_error("json get<integral>: value is not a number");
            const Decimal& d = n;
            // Must have no fractional part (i.e., decimal exponent >= 0)
            if (d.exp10 < 0)
                throw std::logic_error("json get<integral>: number has a fractional component");
            // Build the full base-10 integer as text: unscaled * 10^exp10
            std::string digits = d.unscaled.toDecimalString();
            if (digits == "0") return T{0};
            digits.append(size_t(d.exp10), '0');
            // accumulate into T with overflow checks (stay negative when neg)
            T out = 0;
            if (d.neg) {
                const T minv = std::numeric_limits<T>::min();
                if (minv == 0) throw std::out_of_range("json get<integral>: value out of range");
                for (char c : digits) {
                    int dig = c - '0';
                    if (out < (minv + dig) / 10)
                        throw std::out_of_range("json get<integral>: value out of range");
                    out = T(out * 10 - dig);
                }
                return out;
            } else {
                const T maxv = std::numeric_limits<T>::max();
                for (char c : digits) {
                    int dig = c - '0';
                    if (out > (maxv - dig) / 10)
                        throw std::out_of_range("json get<integral>: value out of range");
                    out = T(out * 10 + dig);
                }
                return out;
            }
        }
        // Floating-point types (range-checked)
        else if constexpr (std::is_floating_point_v<T>) {
            if (!isNumber()) throw std::logic_error("json get<floating>: value is not a number");
            std::string tmp;
            emitNumber(n, tmp); // canonical decimal text of exact value
            T out{};
            auto res = std::from_chars(tmp.data(), tmp.data()+tmp.size(), out);
            if (res.ec != std::errc() || res.ptr != tmp.data()+tmp.size() || !std::isfinite(out))
                throw std::logic_error("json get<floating>: value not representable in target type");
            return out;
        }
        // Unsupported T
        else {
            throw std::runtime_error("jsonio::Value: get<floating>() unsupported target type ");
        }
    }

    static inline Value to_value(const Value& v)             { return v; }
    static inline Value to_value(Value&& v)                  { return std::move(v); }
    static inline Value to_value(const Decimal& d)           { return Value::Number(d); }
    static inline Value to_value(Decimal&& d)                { return Value::Number(std::move(d)); }
    static inline Value to_value(std::nullptr_t)             { return Value::Null(); }
    static inline Value to_value(bool b)                     { return Value::Bool(b); }
    template <class I, std::enable_if_t<std::is_integral_v<I> && !std::is_same_v<I,bool>, int> = 0>
    static inline Value to_value(I x)                        { return Value::Number(x); }
    template <class F, std::enable_if_t<std::is_floating_point_v<F>, int> = 0>
    static inline Value to_value(F x)                        { return Value::Number(x); }
    static inline Value to_value(const std::string& s)       { return Value::String(s); }
    static inline Value to_value(std::string&& s)            { return Value::String(std::move(s)); }
    static inline Value to_value(std::string_view sv)        { return Value::String(std::string(sv)); }
    static inline Value to_value(const char* s)              { return Value::String(s ? std::string(s) : std::string()); }

    // key conversion (string-like → std::string)
    static inline std::string to_key(std::string k)          { return k; }
    static inline std::string to_key(const std::string& k)   { return k; }
    static inline std::string to_key(std::string_view k)     { return std::string(k); }
    static inline std::string to_key(const char* k)          { return k ? std::string(k) : std::string(); }

    // pair → KeyPair conversion
    static inline Value::KeyPair to_keypair(const Value::KeyPair& kv) { return kv; }
    static inline Value::KeyPair to_keypair(Value::KeyPair&& kv)      { return std::move(kv); }
    template <class K, class V>
    static inline Value::KeyPair to_keypair(K&& k, V&& v) {
        return Value::KeyPair{ to_key(std::forward<K>(k)), to_value(std::forward<V>(v)) };
    }
    template <class K, class V>
    static inline Value::KeyPair to_keypair(const std::pair<K,V>& p) {
        return to_keypair(p.first, p.second);
    }
    template <class K, class V>
    static inline Value::KeyPair to_keypair(std::pair<K,V>&& p) {
        return to_keypair(std::move(p.first), std::move(p.second));
    }

    // Accept initializer_list<Value> or initializer_list of convertible types
    static Value Array(std::initializer_list<Value> il) {
        Value x; x.type=Type::Array; x.a.assign(il.begin(), il.end()); return x;
    }
    template <class T>
    static Value Array(std::initializer_list<T> il) {
        Value x; x.type=Type::Array; x.a.reserve(il.size());
        for (const auto& e : il) x.a.push_back(to_value(e));
        return x;
    }
    // Generic container of elements convertible to Value
    template <class Container,
              class Elem = std::decay_t<decltype(*std::begin(std::declval<const Container&>()))>,
              std::enable_if_t<!std::is_same_v<std::decay_t<Container>, array_t>, int> = 0>
    static Value Array(const Container& c) {
        Value x; x.type=Type::Array;
        if constexpr (requires { std::size(c); }) x.a.reserve(std::size(c));
        for (const auto& e : c) x.a.push_back(to_value(e));
        return x;
    }
    // Object from initializer_list of KeyPair or pair-like
    static Value Object(std::initializer_list<KeyPair> il) {
        Value x; x.type=Type::Object; x.o.assign(il.begin(), il.end()); return x;
    }
    template <class K, class V>
    static Value Object(std::initializer_list<std::pair<K,V>> il) {
        Value x; x.type=Type::Object; x.o.reserve(il.size());
        for (const auto& kv : il) x.o.push_back(to_keypair(kv));
        return x;
    }
    // Generic container of KeyPair or pair<string-like, value-convertible>
    template <class Container,
              class Elem = std::decay_t<decltype(*std::begin(std::declval<const Container&>()))>,
              std::enable_if_t<!std::is_same_v<std::decay_t<Container>, object_t>, int> = 0>
    static Value Object(const Container& c) {
        Value x; x.type=Type::Object;
        if constexpr (requires { std::size(c); }) x.o.reserve(std::size(c));
        for (const auto& e : c) {
            if constexpr (std::is_same_v<Elem, KeyPair>) {
                x.o.push_back(e);
            } else if constexpr (requires { e.first; e.second; }) {
                x.o.push_back(to_keypair(e));
            } else {
                throw std::runtime_error("Object(Container): element must be KeyPair or pair-like");
            }
        }
        return x;
    }
};

// Duplicate-key convenience lookups
inline const Value* getFirst(const Value& obj, std::string_view key) {
    if (!obj.isObject()) return nullptr;
    for (const auto& m : obj.o) if (m.name == key) return &m.value;
    return nullptr;
}

inline const Value* getLast(const Value& obj, std::string_view key) {
    if (!obj.isObject()) return nullptr;
    for (std::size_t i = obj.o.size(); i > 0; --i) if (obj.o[i-1].name == key) return &obj.o[i-1].value;
    return nullptr;
}

inline bool hasKey(const Value& obj, std::string_view key) {
    if (!obj.isObject()) return false;
    for (std::size_t i = obj.o.size(); i > 0; --i) if (obj.o[i-1].name == key) return true;
    return false;
}

// ==== Parser ====================================================================================

class Parser {
public:
    Parser(std::string_view in, std::size_t maxDepth)
    : begin_(in.data()), p_(in.data()), end_(in.data()+in.size()),
      line_(1), col_(1), depth_(0), maxDepth_(maxDepth) {}

    Value parseText() {
        ws();
        Value v = parseValue();
        ws();
        if (p_ != end_) error("trailing content after top-level value");
        return v;
    }

private:
    //----- Core state
    const char* begin_;
    const char* p_;
    const char* end_;
    std::size_t line_;
    std::size_t col_;
    std::size_t depth_;
    std::size_t maxDepth_;

    //----- Helpers
    bool atEnd() const { return p_ >= end_; }
    char peek() const  { return atEnd() ? '\0' : *p_; }

    void advance() {
        if (atEnd()) return;
        if (*p_ == '\n') { ++line_; col_ = 1; }
        else             { ++col_; }
        ++p_;
    }

    void advanceN(std::size_t n) {
        while (n--) advance();
    }

    // ASCII digit helper we'll reuse elsewhere too
    static bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

    // Validate one UTF-8 scalar value starting at p_, reject overlongs, surrogates, > U+10FFFF.
    // Append original bytes to 'out' on success, and advance p_ accordingly.
    void readUtf8Validated(std::string& out) {
        if (atEnd()) error("unexpected end in UTF-8 sequence");
        const unsigned char* s = reinterpret_cast<const unsigned char*>(p_);
        std::size_t rem = static_cast<std::size_t>(end_ - p_);
        unsigned char c0 = s[0];

        auto bad = [&]{ error("invalid UTF-8 encoding in string"); };

        if (c0 < 0x80) { // ASCII handled elsewhere; we shouldn't land here for <0x80
            out.push_back(static_cast<char>(c0));
            advance();
            return;
        }

        uint32_t cp = 0;
        std::size_t need = 0;

        if ((c0 & 0xE0) == 0xC0) {             // 2-byte
            if (rem < 2) bad();
            unsigned char c1 = s[1];
            if ((c1 & 0xC0) != 0x80) bad();
            uint32_t t = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
            if (t < 0x80) bad();                 // overlong
            cp = t; need = 2;
        } else if ((c0 & 0xF0) == 0xE0) {      // 3-byte
            if (rem < 3) bad();
            unsigned char c1 = s[1], c2 = s[2];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) bad();
            uint32_t t = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
            // overlongs and surrogates forbidden in UTF-8
            if (t < 0x800) bad();
            if (t >= 0xD800 && t <= 0xDFFF) bad();  // UTF-8 must not encode surrogates
            cp = t; need = 3;
        } else if ((c0 & 0xF8) == 0xF0) {      // 4-byte
            if (rem < 4) bad();
            unsigned char c1 = s[1], c2 = s[2], c3 = s[3];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) bad();
            uint32_t t = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            if (t < 0x10000 || t > 0x10FFFF) bad(); // overlong or out of range
            cp = t; need = 4;
        } else {
            bad();
        }

        // Append original bytes (already validated) to preserve exact lexeme
        out.append(reinterpret_cast<const char*>(s), reinterpret_cast<const char*>(s) + need);
        advanceN(need);
    }

    [[noreturn]] void error(const char* msg) const {
        // Show a small context excerpt (up to 30 chars around current position)
        constexpr std::size_t CTX = 30;
        std::size_t off = static_cast<std::size_t>(p_ - begin_);
        std::size_t start = (off > CTX ? off - CTX : 0);
        std::size_t len   = (static_cast<std::size_t>(end_ - begin_) - start);
        if (len > CTX*2) len = CTX*2;
        std::string snippet(begin_ + start, begin_ + start + len);
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "JSON parse error at %zu:%zu: %s\n… %s …",
            static_cast<size_t>(line_), static_cast<size_t>(col_), msg, snippet.c_str());
        throw std::runtime_error(buf);
    }

    void expect(char c) {
        if (peek() != c) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "expected '%c'", c);
            error(buf);
        }
        advance();
    }

    static bool isWS(char c) {
        // ECMA-404 whitespace is exactly: 0x20, 0x09, 0x0A, 0x0D
        return c==' ' || c=='\t' || c=='\n' || c=='\r';
    }

    void ws() {
        while (!atEnd() && isWS(peek())) advance();
    }

    // Encode a Unicode code point as UTF-8
    void emit_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | ( cp       & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6)  & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ( cp        & 0x3F)));
        } else if (cp <= 0x10FFFF) {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6)  & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ( cp        & 0x3F)));
        } else {
            error("invalid Unicode code point");
        }
    }

    // Parse 4 hex digits; returns value or error
    uint32_t hex4() {
        auto hex = [&](char c)->int{
            if (c>='0'&&c<='9') return c-'0';
            if (c>='a'&&c<='f') return 10 + (c-'a');
            if (c>='A'&&c<='F') return 10 + (c-'A');
            return -1;
        };
        uint32_t v = 0;
        for (int i=0;i<4;i++) {
            if (atEnd()) error("unexpected end while reading \\uXXXX");
            int h = hex(peek());
            if (h < 0) error("invalid hex digit in \\uXXXX escape");
            v = (v<<4) | static_cast<uint32_t>(h);
            advance();
        }
        return v;
    }

    Value parseValue() {
        if (++depth_ > maxDepth_) error("exceeded maximum nesting depth");
        auto guard = finally([&]{ --depth_; });

        ws();
        if (atEnd()) error("unexpected end of input");
        char c = peek();
        switch (c) {
        case 'n': return parseLiteral("null",  Value::Null());
        case 't': return parseLiteral("true",  Value::Bool(true));
        case 'f': return parseLiteral("false", Value::Bool(false));
        case '"': return parseString();
        case '{': return parseObject();
        case '[': return parseArray();
        default:
            if (c=='-' || (c>='0' && c<='9')) return parseNumber();
            error("unexpected character");
        }
    }

    template <typename T>
    Value parseLiteral(const char* kw, T v) {
        const char* s = kw;
        while (*s) {
            if (atEnd() || peek() != *s) error("invalid literal");
            advance(); ++s;
        }
        // Must be followed by a delimiter (WS, ',', ']', '}') or end
        if (!atEnd()) {
            char d = peek();
            if (!(isWS(d) || d==',' || d==']' || d=='}')) {
                error("invalid character following literal");
            }
        }
        return v;
    }

    Value parseString() {
        expect('"');
        std::string out;
        while (!atEnd()) {
            char c = peek();
            if (c == '"') { advance(); return Value::String(std::move(out)); }
            if (static_cast<unsigned char>(c) <= 0x1F) error("unescaped control character in string");

            if (c == '\\') {
                advance();
                if (atEnd()) error("unterminated escape sequence");
                char e = peek(); advance();
                switch (e) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                uint32_t u = hex4();
                if (u >= 0xD800 && u <= 0xDBFF) {
                    if (atEnd() || peek() != '\\') error("expected second surrogate: missing backslash");
                    advance();
                    if (atEnd() || peek() != 'u') error("expected 'u' starting second surrogate");
                    advance();
                    uint32_t u2 = hex4();
                    if (!(u2 >= 0xDC00 && u2 <= 0xDFFF)) error("invalid low surrogate");
                    uint32_t cp = 0x10000 + (((u - 0xD800) << 10) | (u2 - 0xDC00));
                    emit_utf8(out, cp);
                } else if (u >= 0xDC00 && u <= 0xDFFF) {
                    error("lone low surrogate");
                } else {
                    emit_utf8(out, u);
                }
                } break;
                default: error("invalid escape in string");
                }
            } else if (static_cast<unsigned char>(c) < 0x80) {
                out.push_back(c);
                advance();
            } else {
                // Validate & append well-formed UTF-8 for non-ASCII
                readUtf8Validated(out);
            }
        }
        error("unterminated string");
    }

    Value parseArray() {
        expect('[');
        ws();
        std::vector<Value> arr;
        if (peek() == ']') { advance(); return Value::Array(std::move(arr)); }
        while (true) {
            arr.push_back(parseValue());
            ws();
            if (peek() == ',') { advance(); ws(); continue; }
            if (peek() == ']') { advance(); break; }
            error("expected ',' or ']'");
        }
        return Value::Array(std::move(arr));
    }

    Value parseObject() {
        expect('{');
        ws();
        Value::object_t obj;
        if (peek() == '}') { advance(); return Value::Object(std::move(obj)); }
        while (true) {
            if (peek() != '"') error("object member must start with string name");
            Value name = parseString();
            ws();
            expect(':');
            ws();
            Value val = parseValue();
            obj.push_back({std::move(name.s), std::move(val)});
            ws();
            if (peek() == ',') { advance(); ws(); continue; }
            if (peek() == '}') { advance(); break; }
            error("expected ',' or '}'");
        }
        return Value::Object(std::move(obj));
    }

    // Strict ECMA-404 number grammar:
    //   -? (0 | [1-9][0-9]*) ('.' [0-9]+)? ([eE] [+-]? [0-9]+)?
    // Parse & normalize into Decimal (exact value).  :contentReference[oaicite:3]{index=3}
    Value parseNumber() {
        bool neg = false;
        if (peek() == '-') { neg = true; advance(); }
        // integer part
        if (atEnd()) error("incomplete number");
        std::string intDigits, fracDigits;
        if (peek() == '0') {
            intDigits.push_back('0'); advance();
            if (!atEnd() && isAsciiDigit(peek())) error("leading zeros are not allowed");
        } else if (isAsciiDigit(peek())) {
            if (peek() < '1' || peek() > '9') error("invalid integer part");
            while (!atEnd() && isAsciiDigit(peek())) { intDigits.push_back(peek()); advance(); }
        } else {
            error("invalid number");
        }
        // fraction
        if (!atEnd() && peek()=='.') {
            advance();
            if (atEnd() || !isAsciiDigit(peek())) error("fraction requires at least one digit");
            while (!atEnd() && isAsciiDigit(peek())) { fracDigits.push_back(peek()); advance(); }
        }
        // exponent
        long long expPart = 0; bool expNeg=false; bool haveExp=false;
        if (!atEnd() && (peek()=='e' || peek()=='E')) {
            haveExp=true; advance();
            if (!atEnd() && (peek()=='+' || peek()=='-')) { expNeg = (peek()=='-'); advance(); }
            if (atEnd() || !isAsciiDigit(peek())) error("exponent requires at least one digit");
            while (!atEnd() && isAsciiDigit(peek())) {
                int d = peek()-'0';
                expPart = expPart*10 + d;
                if (expPart > 2000000000LL) expPart = 2000000000LL; // clamp to sane bounds
                advance();
            }
        }
        if (expNeg) expPart = -expPart;
        // delimiter check
        if (!atEnd()) {
            char d = peek();
            if (!(isWS(d) || d==',' || d==']' || d=='}')) error("invalid character following number");
        }
        // normalize: combine digits, strip leading zeros, strip trailing zeros => adjust exp10
        std::string combined = intDigits + fracDigits;
        // strip leading zeros
        size_t i = combined.find_first_not_of('0');
        if (i == std::string::npos) {
            Decimal D; D.neg=false; D.exp10=0; // value = 0
            return Value::Number(std::move(D));
        }
        combined.erase(0, i);
        // strip trailing zeros (increase exponent accordingly)
        size_t tz = 0;
        while (!combined.empty() && combined.back()=='0') { combined.pop_back(); ++tz; }
        Decimal D;
        D.neg = neg;
        D.unscaled = BigInt::fromDecimalDigits(combined);
        D.exp10 = int32_t(expPart - (long long)fracDigits.size() + (long long)tz);
        if (D.unscaled.isZero()) D.neg = false; // canonicalize -0 to +0
        return Value::Number(std::move(D));
    }

    // small RAII helper
    template <class F>
    struct Final {
        F f;
        ~Final(){ f(); }
    };
    template <class F>
    static Final<F> finally(F&& f){ return Final<F>{std::forward<F>(f)}; }
};

// ==== Public API ================================================================================

inline Value parseText(std::string_view text, std::size_t maxDepth = 1000) {
    Parser p(text, maxDepth);
    return p.parseText();
}

// String escaping for dump()
inline void escapeString(std::string& out, std::string_view s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c <= 0x1F) {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04X", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

inline std::string escape(std::string_view s) {
    std::string out;
    escapeString(out, s);
    return out;
}

inline void dumpImpl(const Value& v, std::string& out) {
    switch (v.type) {
    case Value::Type::Null:  out += "null"; break;
    case Value::Type::Bool:  out += (v.b ? "true" : "false"); break;
    case Value::Type::Number: emitNumber(v.n, out); break;
    case Value::Type::String: escapeString(out, v.s); break;
    case Value::Type::Array: {
        out.push_back('[');
        for (std::size_t i=0;i<v.a.size();++i) {
            if (i) out.push_back(',');
            dumpImpl(v.a[i], out);
        }
        out.push_back(']');
    } break;
    case Value::Type::Object: {
        out.push_back('{');
        for (std::size_t i=0;i<v.o.size();++i) {
            if (i) out.push_back(',');
            escapeString(out, v.o[i].name);
            out.push_back(':');
            dumpImpl(v.o[i].value, out);
        }
        out.push_back('}');
    } break;
    }
}

inline std::string dump(const Value& v) {
    std::string out;
    out.reserve(128);
    dumpImpl(v, out);
    return out;
}

} // namespace jsonio

#endif // JSON_IO_HPP