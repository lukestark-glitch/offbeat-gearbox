#include "Json.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace offbeat {

const JsonValue* JsonValue::get(const char* key) const
{
    if (type != Object)
        return nullptr;
    for (const auto& kv : obj)
        if (kv.first == key)
            return &kv.second;
    return nullptr;
}

namespace {
struct Parser {
    const char* p;
    const char* start;
    std::string err;
    int depth = 0;

    void ws()
    {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            ++p;
    }
    bool fail(const char* what)
    {
        if (err.empty()) {
            char buf[80];
            std::snprintf(buf, sizeof(buf), "%s bei Zeichen %d", what, (int)(p - start));
            err = buf;
        }
        return false;
    }
    bool string(std::string& out)
    {
        ++p; // "
        while (*p && *p != '"') {
            if (*p == '\\') {
                ++p;
                switch (*p) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned c = 0;
                    for (int i = 1; i <= 4; ++i) {
                        const char h = p[i];
                        if (!h)
                            return fail("\\u unvollständig");
                        c = c * 16 + (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
                    }
                    p += 4;
                    if (c < 0x80) out += (char)c; // als UTF-8 (ohne Ersatzpaare)
                    else if (c < 0x800) out += (char)(0xc0 | c >> 6), out += (char)(0x80 | (c & 63));
                    else out += (char)(0xe0 | c >> 12), out += (char)(0x80 | ((c >> 6) & 63)), out += (char)(0x80 | (c & 63));
                    break;
                }
                case 0: return fail("Text nicht abgeschlossen");
                default: out += *p;
                }
                ++p;
            } else {
                out += *p++;
            }
        }
        if (*p != '"')
            return fail("Text nicht abgeschlossen");
        ++p;
        return true;
    }
    bool value(JsonValue& v)
    {
        if (++depth > 32)
            return fail("zu tief verschachtelt");
        ws();
        bool ok = true;
        if (*p == '{') {
            v.type = JsonValue::Object;
            ++p, ws();
            if (*p == '}')
                ++p;
            else
                for (;;) {
                    ws();
                    if (*p != '"') { ok = fail("Schlüssel erwartet"); break; }
                    std::pair<std::string, JsonValue> kv;
                    if (!string(kv.first)) { ok = false; break; }
                    ws();
                    if (*p != ':') { ok = fail("':' erwartet"); break; }
                    ++p;
                    if (!value(kv.second)) { ok = false; break; }
                    v.obj.push_back(std::move(kv));
                    ws();
                    if (*p == ',') { ++p; continue; }
                    if (*p == '}') { ++p; break; }
                    ok = fail("',' oder '}' erwartet");
                    break;
                }
        } else if (*p == '[') {
            v.type = JsonValue::Array;
            ++p, ws();
            if (*p == ']')
                ++p;
            else
                for (;;) {
                    v.arr.emplace_back();
                    if (!value(v.arr.back())) { ok = false; break; }
                    ws();
                    if (*p == ',') { ++p; continue; }
                    if (*p == ']') { ++p; break; }
                    ok = fail("',' oder ']' erwartet");
                    break;
                }
        } else if (*p == '"') {
            v.type = JsonValue::String;
            ok = string(v.str);
        } else if (!std::strncmp(p, "true", 4)) {
            v.type = JsonValue::Bool, v.b = true, p += 4;
        } else if (!std::strncmp(p, "false", 5)) {
            v.type = JsonValue::Bool, v.b = false, p += 5;
        } else if (!std::strncmp(p, "null", 4)) {
            v.type = JsonValue::Null, p += 4;
        } else {
            char* end = nullptr;
            v.num = std::strtod(p, &end);
            if (end == p)
                ok = fail("Wert erwartet");
            else
                v.type = JsonValue::Number, p = end;
        }
        --depth;
        return ok;
    }
};
} // namespace

bool parseJson(const char* text, JsonValue& out, std::string& err)
{
    Parser ps { text, text, {} };
    out = JsonValue();
    if (!ps.value(out)) {
        err = ps.err;
        return false;
    }
    ps.ws();
    if (*ps.p) {
        ps.fail("Text nach dem JSON");
        err = ps.err;
        return false;
    }
    return true;
}

std::string jsonQuote(const std::string& s)
{
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            out += '\\', out += (char)c;
        else if (c == '\n')
            out += "\\n";
        else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else
            out += (char)c;
    }
    return out + "\"";
}

} // namespace offbeat
