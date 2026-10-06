/**
 * @file
 * @brief strtol_10_16() and strtod_noE() of Firmware/strtod.c, compiled for the host.
 */

#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers_floating_point.hpp"
#include <climits>
#include <cmath>
#include <cstdlib>
#include <random>
#include <string>

extern "C" {
double strtod_noE(const char* nptr, char** endptr);
long strtol_10_16(const char* nptr, char** endptr, unsigned char base);
}

using Catch::Matchers::WithinRel;

namespace {

struct LongCase {
    const char *str;
    unsigned char base;
    long value;
    long end; // characters consumed
};

void check_long(const LongCase &c) {
    INFO("input \"" << c.str << "\" base " << int(c.base));
    char *end = nullptr;
    CHECK(strtol_10_16(c.str, &end, c.base) == c.value);
    CHECK(end - c.str == c.end);
    CHECK(strtol_10_16(c.str, nullptr, c.base) == c.value);
}

struct DoubleCase {
    const char *str;
    double value;
    long end;
};

void check_double(const DoubleCase &c) {
    INFO("input \"" << c.str << "\"");
    char *end = nullptr;
    const double v = strtod_noE(c.str, &end);
    if (c.value == 0)
        CHECK(v == 0);
    else
        CHECK_THAT(v, WithinRel(c.value, 1e-6));
    CHECK(end - c.str == c.end);
}

} // namespace

TEST_CASE("strtol_10_16 parses the decimal numbers", "[strtod]") {
    const LongCase cases[] = {
        {"123", 10, 123, 3},
        {"  -42X", 10, -42, 5},
        {"+7", 10, 7, 2},
        {"0", 10, 0, 1},
        {"12.5", 10, 12, 2},
        {"0x1A", 10, 0, 1},
        {"", 10, 0, 0},
        {"-", 10, 0, 0},
        {"abc", 10, 0, 0},
        {"\t\n 5", 10, 5, 4},
    };
    for (const auto &c : cases)
        check_long(c);
}

TEST_CASE("strtol_10_16 parses the hexadecimal numbers", "[strtod]") {
    const LongCase cases[] = {
        {"0x1A", 16, 26, 4},
        {"0X1a", 16, 26, 4},
        {"1a", 16, 26, 2},
        {"FF", 16, 255, 2},
        {"-0x10", 16, -16, 5},
        {"0xg", 16, 0, 1},
        {"0x", 16, 0, 1},
        {"+0x", 16, 0, 2},
        {" -0xG", 16, 0, 3},
        {"0x0", 16, 0, 3},
        {"0x7fff", 16, 0x7fff, 6},
        {"g", 16, 0, 0},
        {"0d00", 16, 0xd00, 4},
    };
    for (const auto &c : cases)
        check_long(c);
}

TEST_CASE("strtol_10_16 saturates on overflow and reads all the digits", "[strtod]") {
    check_long({"99999999999", 10, LONG_MAX, 11});
    check_long({"-99999999999", 10, LONG_MIN, 12});
    check_long({"fffffffffff", 16, LONG_MAX, 11});
}

TEST_CASE("strtol_10_16 handles the 32-bit limits like avr-libc", "[strtod]") {
    // The firmware long is 32-bit: the exact limits only exist on a host with the same width.
    if (sizeof(long) != 4) {
        WARN("long is not 32-bit on this host: limits not checked");
        return;
    }
    check_long({"2147483647", 10, 2147483647L, 10});
    check_long({"2147483648", 10, LONG_MAX, 10});
    check_long({"-2147483648", 10, LONG_MIN, 11});
    check_long({"-2147483649", 10, LONG_MIN, 11});
    check_long({"0x7fffffff", 16, 2147483647L, 10});
    check_long({"0x80000000", 16, LONG_MAX, 10});
}

TEST_CASE("strtol_10_16 matches the C library strtol on random inputs", "[strtod]") {
    // Short strings stay in the 32-bit range, so the result does not depend on the host long width.
    // No 'x': on a bare "0x" the Windows UCRT strtol rewinds the end pointer to the start, against
    // the C standard; the 0x prefix is checked by the explicit cases instead.
    static const char alphabet[] = " +-0123456789abcdefABCDEF.G";
    std::mt19937 rng(20261006);
    std::uniform_int_distribution<size_t> len_dist(0, 7);
    std::uniform_int_distribution<size_t> chr_dist(0, sizeof(alphabet) - 2);
    for (unsigned char base : {10, 16}) {
        for (int n = 0; n < 20000; ++n) {
            std::string s(len_dist(rng), ' ');
            for (auto &ch : s)
                ch = alphabet[chr_dist(rng)];
            char *end_ref = nullptr;
            char *end = nullptr;
            const long ref = std::strtol(s.c_str(), &end_ref, base);
            const long v = strtol_10_16(s.c_str(), &end, base);
            if (v != ref || end != end_ref) {
                INFO("input \"" << s << "\" base " << int(base));
                CHECK(v == ref);
                CHECK(end - s.c_str() == end_ref - s.c_str());
            }
        }
    }
}

TEST_CASE("strtod_noE parses the G-code numbers", "[strtod]") {
    const DoubleCase cases[] = {
        {"12.5", 12.5, 4},
        {"-0.04", -0.04, 5},
        {"+3", 3, 2},
        {".5", 0.5, 2},
        {"5.", 5, 2},
        {"1.2.3", 1.2, 3},
        {"  7", 7, 3},
        {"0.0001", 0.0001, 6},
        {"123456.789", 123456.789, 10},
        {"", 0, 0},
        {"X", 0, 0},
        {"-", 0, 0},
    };
    for (const auto &c : cases)
        check_double(c);
}

TEST_CASE("strtod_noE leaves the capital E to the next G-code parameter", "[strtod]") {
    check_double({"10E5", 10, 2});
    check_double({"1.5E-2", 1.5, 3});
    check_double({"1e2", 100, 3});
    check_double({"25e-1", 2.5, 5});
    check_double({"1e", 1, 1});
    check_double({"1e+", 1, 1});
    check_double({"1ex", 1, 1});
}

TEST_CASE("strtod_noE recognizes INF and NAN, and only them", "[strtod]") {
    char *end = nullptr;
    const char *s = "inf";
    CHECK(std::isinf(strtod_noE(s, &end)));
    CHECK(end - s == 3);

    s = "-Infinity";
    const double minf = strtod_noE(s, &end);
    CHECK((std::isinf(minf) && minf < 0));
    CHECK(end - s == 9);

    s = "NaN";
    CHECK(std::isnan(strtod_noE(s, &end)));
    CHECK(end - s == 3);

    // An 'I' or an 'N' that does not spell INF or NAN is not a number.
    check_double({"I5", 0, 0});
    check_double({"N10", 0, 0});
    check_double({"in", 0, 0});
    check_double({"na", 0, 0});
}
