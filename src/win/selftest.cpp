#include "win/selftest.h"

#include "win/log.h"
#include "win/scripts.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Probe {
    std::string name;
    uint64_t hash;
};

std::wstring probes_path() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&probes_path), &self);
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring dir(path, n < MAX_PATH ? n : 0);
    dir.resize(dir.find_last_of(L"\\/") + 1);
    return dir + L"VinewoodHook.probes.txt";
}

} // namespace

void selftest_main() {
    std::vector<Probe> probes;
    if (FILE* f = _wfopen(probes_path().c_str(), L"r")) {
        char name[128], hash[32];
        while (std::fscanf(f, "%127s %31s", name, hash) == 2) probes.push_back({name, std::strtoull(hash, nullptr, 16)});
        std::fclose(f);
    }
    for (int round = 1; round <= 5; ++round) {
        for (const Probe& p : probes) {
            scripts::native_init(p.hash);
            const uint64_t v = *scripts::native_call();
            float fv;
            std::memcpy(&fv, &v, sizeof fv);
            logf("selftest:   %-36s int %d float %g", p.name.c_str(), static_cast<int>(v), fv);
        }
        scripts::native_init(0x71D93B57D07F9804ull); // SQRT
        const float in = 16.0f;
        uint64_t arg = 0;
        std::memcpy(&arg, &in, sizeof in);
        scripts::native_push(arg);
        const uint64_t r = *scripts::native_call();
        float root;
        std::memcpy(&root, &r, sizeof root);
        scripts::native_init(0x9CD27B0045628463ull); // GET_GAME_TIMER
        logf("selftest: round %d: SQRT(16) = %g, GET_GAME_TIMER = %d", round, root,
             static_cast<int>(*scripts::native_call()));
        scripts::wait(1000);
    }
    logf("selftest: finished");
}
