// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CPUX_PACKAGE_MATCH_HPP
#define CPUX_PACKAGE_MATCH_HPP
#include <string>
#include <string_view>
#include <utility>
#include "databases.h"

namespace cpu_package {
// Deliberately ASCII and locale independent: CPUID brand strings are ASCII.
inline bool alnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
inline std::string normalize(std::string_view text)
{
    std::string out;
    bool space = false;
    for(char c : text) {
        if(c == ' ' || c == '\t' || c == '\n' || c == '\r') { space = !out.empty(); continue; }
        if(space) out += ' ';
        space = false;
        out += (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
    }
    return out;
}
inline bool bounded_contains(std::string_view text, std::string_view pattern)
{
    for(auto pos = text.find(pattern); pos != std::string_view::npos; pos = text.find(pattern, pos + 1)) {
        const auto end = pos + pattern.size();
        if((pos == 0 || !alnum(text[pos-1]) || !alnum(pattern.front())) &&
           (end == text.size() || !alnum(text[end]) || !alnum(pattern.back()))) return true;
    }
    return false;
}
inline std::string exact_brand(std::string_view brand)
{
    auto result = normalize(brand);
    // Only the explicitly supported CPUID decoration, never arbitrary suffixes.
    const std::string suffix = " with radeon graphics";
    if(result.size() >= suffix.size() && result.compare(result.size()-suffix.size(), suffix.size(), suffix) == 0)
        result.erase(result.size()-suffix.size());
    return result;
}
struct Match {
    const Package_DB *entry = nullptr;
    bool ambiguous = false;
};
inline Match lookup(const Package_DB *db, std::string_view codename, std::string_view brand)
{
    const auto code = normalize(codename), model = normalize(brand);
    Match result;
    std::pair<int, std::size_t> best{0, 0};
    for(auto row = db; row->socket; ++row) {
        const auto c = normalize(row->codename ? row->codename : "");
        const auto m = normalize(row->model ? row->model : "");
        if(row->exact) {
            if(exact_brand(brand) != normalize(row->model)) continue;
        } else if((!c.empty() && !bounded_contains(code, c)) ||
                  (!m.empty() && !bounded_contains(model, m)) || (c.empty() && m.empty())) continue;
        const std::pair<int, std::size_t> rank{row->exact ? 3 : (!m.empty() ? 2 : 1), m.size() + c.size()};
        if(rank > best) {
            best = rank;
            result = {row, false};
        } else if(rank == best && result.entry && std::string_view(result.entry->socket) != row->socket) {
            result.ambiguous = true;
        }
    }
    if(result.ambiguous) result.entry = nullptr;
    return result;
}
} // namespace cpu_package
#endif
