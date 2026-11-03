// A small JSON reader, enough for the game's definition files (arrays of objects with strings and numbers).
#pragma once

#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace lab {

struct Json {
    enum Kind { kNull, kBool, kNumber, kString, kArray, kObject } kind = kNull;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::map<std::string, Json> o;

    const Json* get(const char* key) const {
        if (kind != kObject) return nullptr;
        auto it = o.find(key);
        return it == o.end() ? nullptr : &it->second;
    }
    std::string str(const char* key) const {
        const Json* v = get(key);
        return v && v->kind == kString ? v->s : std::string();
    }
    double num(const char* key, double def) const {
        const Json* v = get(key);
        return v && v->kind == kNumber ? v->n : def;
    }

    static Json Parse(const char* text) {
        const char* p = text;
        Json j;
        if (!Value(p, j, 0)) j = Json();
        return j;
    }

private:
    static void Ws(const char*& p) {
        for (;;) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
            if (p[0] == '/' && p[1] == '*') { p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++; if (*p) p += 2; continue; }
            return;
        }
    }
    static void Utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    static bool String(const char*& p, std::string& out) {
        if (*p != '"') return false;
        p++;
        while (*p && *p != '"') {
            if (*p == '\\') {
                p++;
                switch (*p) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        unsigned cp = (unsigned)strtoul(std::string(p + 1, 4).c_str(), nullptr, 16);
                        p += 4;
                        if (cp >= 0xD800 && cp < 0xDC00 && p[1] == '\\' && p[2] == 'u') {
                            unsigned lo = (unsigned)strtoul(std::string(p + 3, 4).c_str(), nullptr, 16);
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            p += 6;
                        }
                        Utf8(out, cp);
                        break;
                    }
                    case 0: return false;
                    default: out += *p;
                }
                p++;
            } else {
                out += *p++;
            }
        }
        if (*p != '"') return false;
        p++;
        return true;
    }
    static bool Value(const char*& p, Json& j, int depth) {
        if (depth > 64) return false;
        Ws(p);
        if (*p == '{') {
            j.kind = kObject;
            p++;
            Ws(p);
            if (*p == '}') { p++; return true; }
            for (;;) {
                Ws(p);
                std::string key;
                if (!String(p, key)) return false;
                Ws(p);
                if (*p != ':') return false;
                p++;
                Json v;
                if (!Value(p, v, depth + 1)) return false;
                j.o[key] = std::move(v);
                Ws(p);
                if (*p == ',') { p++; Ws(p); if (*p == '}') { p++; return true; } continue; }
                if (*p == '}') { p++; return true; }
                return false;
            }
        }
        if (*p == '[') {
            j.kind = kArray;
            p++;
            Ws(p);
            if (*p == ']') { p++; return true; }
            for (;;) {
                Json v;
                if (!Value(p, v, depth + 1)) return false;
                j.a.push_back(std::move(v));
                Ws(p);
                if (*p == ',') { p++; Ws(p); if (*p == ']') { p++; return true; } continue; }
                if (*p == ']') { p++; return true; }
                return false;
            }
        }
        if (*p == '"') { j.kind = kString; return String(p, j.s); }
        if (!strncmp(p, "true", 4)) { j.kind = kBool; j.b = true; p += 4; return true; }
        if (!strncmp(p, "false", 5)) { j.kind = kBool; p += 5; return true; }
        if (!strncmp(p, "null", 4)) { p += 4; return true; }
        char* end = nullptr;
        j.n = strtod(p, &end);
        if (end == p) return false;
        j.kind = kNumber;
        p = end;
        return true;
    }
};

}  // namespace lab
