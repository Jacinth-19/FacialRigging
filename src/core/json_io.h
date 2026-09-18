#pragma once
// Tiny dependency-free JSON reader/writer helpers used by the clip JSON, project files and
// manifests. Reader is a recursive-descent cursor (`JsonCursor`) with callback-style object /
// array iteration; writer helpers escape strings and format floats compactly.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace fr {
namespace json {

inline std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if (c == '\n') { o += "\\n"; continue; } o += c; } return o; }
inline std::string str(const std::string& s) { return "\"" + esc(s) + "\""; }
inline std::string num(float v) { char b[32]; std::snprintf(b, sizeof b, "%.6g", double(v)); return b; }
inline std::string num(double v) { char b[32]; std::snprintf(b, sizeof b, "%.9g", v); return b; }
inline std::string num(int v) { return std::to_string(v); }
inline std::string arr(const std::vector<float>& v) { std::string o = "["; for (size_t i = 0; i < v.size(); ++i) { if (i) o += ','; o += num(v[i]); } return o + "]"; }

struct Cursor {
    const std::string& s; size_t i = 0;
    explicit Cursor(const std::string& str) : s(str) {}
    void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool peek(char c) { ws(); return i < s.size() && s[i] == c; }
    bool eat(char c) { if (peek(c)) { ++i; return true; } return false; }
    bool str(std::string& out) { ws(); if (!eat('"')) return false; out.clear(); while (i < s.size() && s[i] != '"') { if (s[i] == '\\' && i + 1 < s.size()) ++i; out += s[i++]; } return eat('"'); }
    bool num(float& out) { ws(); char* e = nullptr; out = std::strtof(s.c_str() + i, &e); if (e == s.c_str() + i) return false; i = size_t(e - s.c_str()); return true; }
    bool skipValue() { // any JSON value
        ws(); if (i >= s.size()) return false;
        if (s[i] == '"') { std::string d; return str(d); }
        if (s[i] == '[' || s[i] == '{') { char o = s[i], c = o == '[' ? ']' : '}'; int depth = 0; do { if (s[i] == '"') { std::string d; str(d); continue; } if (s[i] == o) ++depth; else if (s[i] == c) --depth; ++i; } while (depth > 0 && i < s.size()); return depth == 0; }
        while (i < s.size() && !std::strchr(",]}", s[i])) ++i;
        return true;
    }
    bool numArr(std::vector<float>& v) { v.clear(); if (!eat('[')) return false; if (eat(']')) return true; do { float f; if (!num(f)) return false; v.push_back(f); } while (eat(',')); return eat(']'); }
    bool vecArr(std::vector<std::vector<float>>& v) { v.clear(); if (!eat('[')) return false; if (eat(']')) return true; do { std::vector<float> e; if (!numArr(e)) return false; v.push_back(e); } while (eat(',')); return eat(']'); }
};

template <class F> bool objEach(Cursor& p, F&& onKey) { if (!p.eat('{')) return false; if (p.eat('}')) return true; do { std::string k; if (!p.str(k) || !p.eat(':')) return false; if (!onKey(k)) return false; } while (p.eat(',')); return p.eat('}'); }
template <class F> bool arrEach(Cursor& p, F&& onItem) { if (!p.eat('[')) return false; if (p.eat(']')) return true; do { if (!onItem()) return false; } while (p.eat(',')); return p.eat(']'); }
inline bool boolean(Cursor& p, bool& out) { p.ws(); if (p.s.compare(p.i, 4, "true") == 0) { p.i += 4; out = true; return true; } if (p.s.compare(p.i, 5, "false") == 0) { p.i += 5; out = false; return true; } return false; }

} // namespace json
} // namespace fr
