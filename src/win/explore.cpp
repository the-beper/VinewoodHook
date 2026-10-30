#include "win/explore.h"

#include "core/hashmap.h"
#include "core/natives.h"
#include "win/diagnostics.h"
#include "win/log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace vh;

namespace {

constexpr size_t kTickSlot = 3;                           // vtable[3] = +0x18, inferred from the Enhanced scheduler code

uintptr_t module_base() { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); }

bool read_mem(uint64_t addr, void* out, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(addr), out, n, &got) && got == n;
}

bool is_code(uint64_t addr) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return false;
    return mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
}

std::wstring dll_dir() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&dll_dir), &self);
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring dir(path, n < MAX_PATH ? n : 0);
    dir.resize(dir.find_last_of(L"\\/") + 1);
    return dir;
}

struct NativeTable {
    uint64_t addr = 0;
    uint64_t pointer_slot = 0; // address of the global holding addr, 0 when the table is static
    int layout = -1;
    TableScore score;
};

// Looks for the 256-pointer array either directly inside the writable sections of the main
// module or one pointer away from them (heap-allocated). Both are tried because it is not
// known which one Enhanced uses.
bool find_native_table(NativeTable& out) {
    const auto* base = reinterpret_cast<const uint8_t*>(module_base());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    std::unordered_set<uint64_t> tried;
    std::vector<uint64_t> buf;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
        const uint64_t start = reinterpret_cast<uint64_t>(base) + sec[i].VirtualAddress;
        const size_t count = sec[i].Misc.VirtualSize / 8;
        buf.resize(count);
        if (!read_mem(start, buf.data(), count * 8)) continue;

        auto looks_like_ptr = [](uint64_t v) { return v >= 0x10000 && v < (1ull << 47) && !(v & 7); };
        std::vector<uint32_t> prefix(count + 1, 0); // pointer-looking qwords seen so far
        for (size_t k = 0; k < count; ++k) prefix[k + 1] = prefix[k] + (looks_like_ptr(buf[k]) ? 1 : 0);

        for (size_t k = 0; k < count; ++k) {
            // static array: at least half of the next 256 slots must look like pointers
            if (k + kBuckets <= count && prefix[k + kBuckets] - prefix[k] >= kBuckets / 2) {
                TableScore s = score_table(read_mem, is_code, start + k * 8);
                if (s.ok()) return out = {start + k * 8, 0, s.layout, s}, true;
            }
            // heap array behind a global
            const uint64_t v = buf[k];
            if (looks_like_ptr(v) && tried.insert(v).second) {
                uint64_t first[8];
                if (!read_mem(v, first, sizeof first)) continue;
                TableScore s = score_table(read_mem, is_code, v);
                if (s.ok()) return out = {v, start + k * 8, s.layout, s}, true;
            }
        }
    }
    return false;
}

void dump_natives(const std::vector<NativeEntry>& all) {
    FILE* f = _wfopen((dll_dir() + L"VinewoodHook.natives.txt").c_str(), L"w");
    if (!f) return;
    for (const auto& e : all)
        std::fprintf(f, "%016llX %llX\n", (unsigned long long)e.hash, (unsigned long long)(e.handler - module_base()));
    std::fclose(f);
}

NativeTable g_table;
bool g_table_found = false;

HashMap g_hashes;
std::unordered_map<uint64_t, uint64_t> g_natives; // registered hash -> handler

void load_hashes() {
    FILE* f = _wfopen((dll_dir() + L"VinewoodHook.hashes.txt").c_str(), L"rb");
    if (!f) return logf("explore: VinewoodHook.hashes.txt not found, public hashes will not be translated");
    std::string text;
    char buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    logf("explore: hash translation map loaded, %zu entries", g_hashes.load_text(text));
}

// Handler for a native given its public hash (translated first), 0 when not registered.
uint64_t find_handler(uint64_t public_hash) {
    auto it = g_natives.find(g_hashes.translate(public_hash));
    return it == g_natives.end() ? 0 : it->second;
}

struct ThreadArray {
    uint64_t array = 0;
    uint16_t count = 0;
};

uint64_t g_collection = 0;

bool read_threads(Edition edition, ThreadArray& t) {
    if (!g_collection) g_collection = find_address("scrThreadCollection", edition);
    if (!g_collection) return false;
    return read_mem(g_collection, &t.array, 8) && read_mem(g_collection + 8, &t.count, 2) && t.array && t.count;
}

} // namespace

bool explore_game(Edition edition) {
    ThreadArray threads;
    if (!read_threads(edition, threads)) {
        logf("explore: scrThreadCollection not available yet");
    } else {
        logf("explore: %u script threads, array at %p, collection global at rva 0x%llX", threads.count,
             (void*)threads.array, (unsigned long long)(g_collection - module_base()));
        for (uint16_t i = 0; i < threads.count && i < 4; ++i) {
            uint64_t t = 0, vt = 0;
            read_mem(threads.array + 8 * i, &t, 8);
            if (!t || !read_mem(t, &vt, 8)) continue;
            uint32_t id = 0;
            read_mem(t + 8, &id, 4);
            logf("explore: thread[%u] obj %p id %u vtable rva 0x%llX", i, (void*)t, id, (unsigned long long)(vt - module_base()));
            for (size_t s = 0; s < 8; ++s) {
                uint64_t fn = 0;
                if (read_mem(vt + 8 * s, &fn, 8)) logf("explore:   vtable[%zu] -> rva 0x%llX%s", s, (unsigned long long)(fn - module_base()), is_code(fn) ? "" : " (not code)");
            }
        }
    }

    if (g_table_found) return true;
    if (!find_native_table(g_table)) {
        logf("explore: native table not found yet");
        return false;
    }
    g_table_found = true;
    logf("explore: native table at %p (%s, layout %d), %zu/%zu buckets decode", (void*)g_table.addr,
         g_table.pointer_slot ? "heap" : "static", g_table.layout, g_table.score.good, g_table.score.nonnull);
    if (g_table.pointer_slot) logf("explore:   held by global at rva 0x%llX", (unsigned long long)(g_table.pointer_slot - module_base()));
    else logf("explore:   static array at rva 0x%llX", (unsigned long long)(g_table.addr - module_base()));

    auto all = read_all_natives(read_mem, g_table.addr, g_table.layout);
    logf("explore: %zu natives registered", all.size());
    for (const auto& e : all) g_natives[e.hash] = e.handler;
    load_hashes();
    dump_natives(all);
    return true;
}

// ---- frame hook ----

namespace {

using TickFn = int (*)(void* thread, uint32_t budget);

TickFn g_orig_tick = nullptr;
FrameFn g_frame = nullptr;
void* g_anchor = nullptr;
DWORD g_anchor_seen = 0;
Edition g_edition = Edition::Legacy;
uint64_t g_vector_results = 0;
uint64_t g_script_entity = 0;
uint64_t g_script_globals = 0;

// The game marks "which script is running" in a global and in a per-thread block; natives read it
// (script handler, resource ownership, ...). The original Tick restores both before returning, so
// they are set again around our frame and restored afterwards.
struct ActiveThreadSlots {
    uint64_t global = 0;     // address of the global holding the active thread
    uint32_t tls_index = 0;
    uint32_t field_off = 0;  // offset of the active-thread field in the per-thread block
    uint32_t flag_off = 0;   // offset of the "inside a script" byte
    bool ok = false;
};
ActiveThreadSlots g_active;

struct SavedActive {
    uint64_t global, field;
    uint8_t flag;
};

uint8_t* thread_block() {
    const auto tls_array = reinterpret_cast<uint8_t**>(__readgsqword(0x58));
    return tls_array ? tls_array[g_active.tls_index] : nullptr;
}

bool enter_script_context(void* thread, SavedActive& saved) {
    uint8_t* block = thread_block();
    if (!g_active.ok || !block) return false;
    auto* global = reinterpret_cast<volatile uint64_t*>(g_active.global);
    auto* field = reinterpret_cast<volatile uint64_t*>(block + g_active.field_off);
    saved = {*global, *field, block[g_active.flag_off]};
    *global = reinterpret_cast<uint64_t>(thread);
    *field = reinterpret_cast<uint64_t>(thread);
    block[g_active.flag_off] = 1;
    return true;
}

void leave_script_context(const SavedActive& saved) {
    uint8_t* block = thread_block();
    if (!block) return;
    *reinterpret_cast<volatile uint64_t*>(g_active.global) = saved.global;
    *reinterpret_cast<volatile uint64_t*>(block + g_active.field_off) = saved.field;
    block[g_active.flag_off] = saved.flag;
}

// Thread object of the script with this name, 0 when there is none.
uint64_t find_script_thread(const char* wanted);

const char* const kPreferredAnchor = "main_persistent"; // lives for the whole session in story mode

// Runs after the game ticked a script thread. Scripts run once per frame, on one anchor thread: the
// persistent main script when it exists (it owns a proper script handler), otherwise the first
// thread that ticked. If the anchor goes quiet for a second another one takes over.
int hooked_tick(void* thread, uint32_t budget) {
    const int r = g_orig_tick(thread, budget);
    const DWORD now = GetTickCount();
    static DWORD last_search = 0;
    static bool anchor_preferred = false;
    if (!anchor_preferred && now - last_search > 500) {
        last_search = now;
        if (const uint64_t t = find_script_thread(kPreferredAnchor)) {
            g_anchor = reinterpret_cast<void*>(t);
            g_anchor_seen = now;
            anchor_preferred = true;
            logf("hook: anchor thread is %s (%p)", kPreferredAnchor, g_anchor);
        }
    }
    if (!g_anchor || now - g_anchor_seen > 1000) {
        g_anchor = thread;
        anchor_preferred = false;
    }
    if (thread == g_anchor) {
        g_anchor_seen = now;
        SavedActive saved{};
        const bool entered = enter_script_context(thread, saved);
        g_frame();
        if (entered) leave_script_context(saved);
    }
    return r;
}

} // namespace

uint64_t native_handler(uint64_t public_hash) { return find_handler(public_hash); }

namespace {

uint64_t find_script_thread(const char* wanted) {
    if (!g_collection) return 0;
    uint64_t array = 0;
    uint16_t count = 0;
    if (!read_mem(g_collection, &array, 8) || !read_mem(g_collection + 8, &count, 2) || !array) return 0;
    const size_t name_off = g_edition == Edition::Enhanced ? 0x154 : 0xD4;
    for (uint16_t i = 0; i < count; ++i) {
        uint64_t t = 0;
        uint32_t id = 0;
        char name[65] = {};
        if (!read_mem(array + 8 * i, &t, 8) || !t || !read_mem(t + 8, &id, 4) || !id) continue;
        if (read_mem(t + name_off, name, 64) && std::strcmp(name, wanted) == 0) return t;
    }
    return 0;
}

} // namespace

std::vector<std::string> live_script_names() {
    std::vector<std::string> names;
    if (!g_collection) return names;
    uint64_t array = 0;
    uint16_t count = 0;
    if (!read_mem(g_collection, &array, 8) || !read_mem(g_collection + 8, &count, 2) || !array) return names;
    const size_t name_off = g_edition == Edition::Enhanced ? 0x154 : 0xD4;
    for (uint16_t i = 0; i < count; ++i) {
        uint64_t t = 0;
        uint32_t id = 0;
        char name[65] = {};
        if (!read_mem(array + 8 * i, &t, 8) || !t || !read_mem(t + 8, &id, 4) || !id) continue;
        if (!read_mem(t + name_off, name, 64)) continue;
        names.emplace_back(name);
    }
    return names;
}

uint64_t vector_results_function() { return g_vector_results; }
uint64_t script_entity_function() { return g_script_entity; }
uint64_t script_globals_table() { return g_script_globals; }

bool install_frame_hook(Edition edition, FrameFn on_frame) {
    g_edition = edition;
    ThreadArray threads;
    if (!g_table_found || !read_threads(edition, threads)) return logf("hook: prerequisites missing"), false;

    uint64_t thread = 0, vtable = 0, tick = 0;
    if (!read_mem(threads.array, &thread, 8) || !read_mem(thread, &vtable, 8) || !read_mem(vtable + 8 * kTickSlot, &tick, 8) ||
        !is_code(tick))
        return logf("hook: cannot read the vtable"), false;

    // Script names live somewhere inside the thread objects; log the readable strings to find them.
    for (uint16_t i = 0, shown = 0; i < threads.count && shown < 8; ++i) {
        uint64_t t = 0;
        uint8_t obj[0x300];
        uint32_t id = 0;
        if (!read_mem(threads.array + 8 * i, &t, 8) || !t || !read_mem(t, obj, sizeof obj)) continue;
        std::memcpy(&id, obj + 8, 4);
        if (!id) continue;
        ++shown;
        std::string found;
        for (size_t o = 0; o < sizeof obj;) {
            size_t e = o;
            while (e < sizeof obj && obj[e] >= 0x20 && obj[e] < 0x7F) ++e;
            if (e - o >= 4) {
                char buf[96];
                std::snprintf(buf, sizeof buf, " +0x%zX \"%.*s\"", o, static_cast<int>(std::min<size_t>(e - o, 40)), obj + o);
                found += buf;
            }
            o = e + 1;
        }
        logf("hook: thread[%u] obj %p id %u strings:%s", i, (void*)t, id, found.c_str());
    }
    g_vector_results = find_address("setVectorResults", edition);
    if (!g_vector_results) logf("hook: SetVectorResults not located, vector outputs will not be flushed");
    g_active.global = find_address("activeThread", edition);
    if (const uint64_t idx_addr = find_address("activeThreadTlsIndex", edition)) {
        uint32_t idx = 0;
        if (read_mem(idx_addr, &idx, 4)) g_active.tls_index = idx;
        g_active.field_off = edition == Edition::Enhanced ? 0x7A0 : 0x2A48;
        g_active.flag_off = edition == Edition::Enhanced ? 0x7A8 : 0x2A50;
        g_active.ok = g_active.global && idx < 1088; // TLS slot indices are small; anything else is a bad read
    }
    logf("hook: active-thread slots %s", g_active.ok ? "located, scripts run with a proper script context" : "NOT located, natives that need the active script may fail");
    g_script_entity = find_address("getScriptEntity", edition);
    if (!g_script_entity) logf("hook: handle->entity function not located, getScriptHandleBaseAddress returns null");
    g_script_globals = find_address("scriptGlobals", edition);
    if (!g_script_globals) logf("hook: script globals table not located, getGlobalPtr falls back to a zeroed buffer");

    void** slot = reinterpret_cast<void**>(vtable + 8 * kTickSlot);
    DWORD old;
    if (!VirtualProtect(slot, 8, PAGE_READWRITE, &old)) return logf("hook: VirtualProtect failed (%lu)", GetLastError()), false;
    g_orig_tick = reinterpret_cast<TickFn>(tick);
    g_frame = on_frame;
    *slot = reinterpret_cast<void*>(&hooked_tick);
    DWORD ignored;
    VirtualProtect(slot, 8, old, &ignored);
    logf("hook: Tick slot patched (vtable rva 0x%llX)", (unsigned long long)(vtable - module_base()));
    return true;
}

void dump_module_image() {
    const auto* base = reinterpret_cast<const uint8_t*>(module_base());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const size_t size = nt->OptionalHeader.SizeOfImage;
    FILE* f = _wfopen((dll_dir() + L"VinewoodHook.image.bin").c_str(), L"wb");
    if (!f) return logf("dump: cannot create the image file");
    std::vector<uint8_t> chunk(1 << 20);
    size_t unreadable = 0;
    for (size_t off = 0; off < size; off += chunk.size()) {
        const size_t n = std::min(chunk.size(), size - off);
        if (!read_mem(reinterpret_cast<uint64_t>(base) + off, chunk.data(), n)) {
            for (size_t po = 0; po < n; po += 0x1000) {
                const size_t pn = std::min<size_t>(0x1000, n - po);
                if (!read_mem(reinterpret_cast<uint64_t>(base) + off + po, chunk.data() + po, pn)) {
                    std::memset(chunk.data() + po, 0, pn);
                    ++unreadable;
                }
            }
        }
        std::fwrite(chunk.data(), 1, n, f);
    }
    std::fclose(f);
    logf("dump: wrote %zu bytes of the module image (%zu unreadable pages zero-filled)", size, unreadable);
}
