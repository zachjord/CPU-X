// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/package_match.hpp"
#include <cstdlib>
#include <iostream>

static int cases = 0;
static void check(bool ok, const char *name)
{
    ++cases;
    if(!ok) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
static bool socket(const Package_DB *db, const char *code, const char *brand, const char *expected)
{
    auto m = cpu_package::lookup(db, code, brand);
    return expected ? m.entry && std::string(m.entry->socket) == expected : !m.entry;
}
int main()
{
    check(socket(package_amd, "Ryzen 9 (Raphael)", "AMD Ryzen 9 7945HX with Radeon Graphics", "FL1"), "7945HX overrides Raphael");
    check(socket(package_amd, "", "  AMD   Ryzen 9 7945HX  ", "FL1"), "model works without codename");
    auto hx = cpu_package::lookup(package_amd, "Raphael", "AMD Ryzen 9 7945HX");
    check(hx.entry && std::string(hx.entry->display_codename) == "Dragon Range", "manufacturer codename");
    check(socket(package_amd, "", "AMD Ryzen 9 7945HX3D", nullptr), "do not infer HX3D from HX");
    check(socket(package_amd, "", "AMD Ryzen 9 7945HX engineering sample", nullptr), "do not accept unknown suffix");
    check(socket(package_amd, "Ryzen 9 (Raphael)", "AMD Ryzen 9 7950X", "AM5 (LGA-1718)"), "desktop unchanged");
    check(socket(package_amd, "Deneb", "AMD Phenom(tm) II X4 920", "AM2+ (PGA-940)"), "model-specific Deneb");
    check(socket(package_amd, "Deneb", "AMD Phenom(tm) II X4 955", "AM3 (PGA-938)"), "generic Deneb");
    check(socket(package_amd, "Turion X2", "AMD Turion X2 TL-60", "S1g1 (PGA-638)"), "legacy fragment");
    check(socket(package_intel, "", "Intel(R) Xeon(R) CPU E5-2690 v3 @ 2.60GHz", "LGA 2011-3"), "Xeon v3 wins over base");
    check(socket(package_intel, "", "Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz", "LGA 2011-3"), "Xeon v4 wins over base");
    check(socket(package_intel, "", "Intel(R) Core(TM) i9-12900HK", "BGA 1744"), "Intel mobile");
    check(socket(package_intel, "", "Intel(R) Core(TM) i9-12900K", "LGA 1700"), "Intel desktop");
    check(socket(package_intel, "", "Intel(R) Core(TM) i9-12900KS", nullptr), "suffix boundary");
    check(socket(package_intel, "", "AMD Ryzen 9 7945HX", nullptr), "vendor isolation");
    check(socket(package_amd, "", "", nullptr), "empty inputs");
    check(socket(package_amd, "Unknown ARM", "Unknown model", nullptr), "unknown inputs");
    const Package_DB conflict[] = {{"Test", nullptr, "A", false, nullptr, "a"}, {"Test", nullptr, "B", false, nullptr, "b"}, {nullptr, nullptr, nullptr, false, nullptr, nullptr}};
    check(cpu_package::lookup(conflict, "Test", "").ambiguous, "equal-specificity conflict");
    const Package_DB ordered[] = {{"Test", nullptr, "A", false, nullptr, "a"}, {nullptr, "Model", "B", false, nullptr, "b"}, {nullptr, nullptr, nullptr, false, nullptr, nullptr}};
    const Package_DB reversed[] = {ordered[1], ordered[0], ordered[2]};
    check(socket(ordered, "Test", "Model", "B") && socket(reversed, "Test", "Model", "B"), "order independent specificity");
    std::cout << cases << " package matching checks passed\n";
}
