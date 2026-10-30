#include "core/hashmap.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

int main(int argc, char** argv) {
    vh::HashMap m;
    CHECK(m.load_text("9CD27B0045628463 1DD05E817C89C737\r\nzz 12\n\n  \n0000000000000001 0000000000000002") == 2);
    CHECK(m.size() == 2);
    CHECK(m.translate(0x9CD27B0045628463ull) == 0x1DD05E817C89C737ull);
    CHECK(m.translate(1) == 2);
    CHECK(m.translate(0x71D93B57D07F9804ull) == 0x71D93B57D07F9804ull); // unknown: unchanged

    // the shipped data file must parse completely (path given by CMake)
    if (argc > 1) {
        std::ifstream f(argv[1]);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        vh::HashMap full;
        CHECK(full.load_text(text) == full.size());
        CHECK(full.size() > 6000);
        CHECK(full.translate(0x9CD27B0045628463ull) == 0x1DD05E817C89C737ull);
    }
    if (g_failures == 0) std::puts("all tests passed");
    return g_failures ? 1 : 0;
}
