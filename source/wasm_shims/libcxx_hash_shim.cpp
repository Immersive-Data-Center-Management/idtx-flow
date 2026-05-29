// Provides std::__2::__hash_memory for wasm32 SIDE_MODULE builds.
//
// Emscripten builds the extension as a SIDE_MODULE (wasm dynamic library).
// In this mode, lld marks any symbol not found in the provided objects as an
// import from the host (main) module.  Godot's main module does not export
// libc++ internals such as __hash_memory (used by OpenUSD's hash containers),
// so we define it here with the correct Emscripten ABI, compiled with the
// same flags as the rest of the extension (PIC, atomics, pthreads, etc.).
//
// Symbol: _ZNSt3__213__hash_memoryEPKvm
//         = std::__2::__hash_memory(void const*, unsigned long)
//
// Algorithm: MurmurHash2 (32-bit) / MurmurHash64A (64-bit), matching libc++.
// This is safe for in-process hash maps (no cross-process or serialised use).

#if defined(__EMSCRIPTEN__)

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace std {
namespace __2 {

std::size_t __hash_memory(const void* __key, std::size_t __len) noexcept {
#if __SIZEOF_SIZE_T__ == 4
    // MurmurHash2 (32-bit) — matches libc++ __murmur2_or_cityhash<size_t, 32>
    static const std::uint32_t __m = 0x5bd1e995U;
    static const int __r = 24;
    std::uint32_t __h = static_cast<std::uint32_t>(__len);
    const unsigned char* __data = static_cast<const unsigned char*>(__key);
    while (__len >= 4) {
        std::uint32_t __k;
        __builtin_memcpy(&__k, __data, 4);
        __k *= __m;
        __k ^= __k >> __r;
        __k *= __m;
        __h *= __m;
        __h ^= __k;
        __data += 4;
        __len  -= 4;
    }
    switch (__len) {
    case 3: __h ^= static_cast<std::uint32_t>(__data[2]) << 16; [[fallthrough]];
    case 2: __h ^= static_cast<std::uint32_t>(__data[1]) << 8;  [[fallthrough]];
    case 1: __h ^= static_cast<std::uint32_t>(__data[0]);
            __h *= __m;
    }
    __h ^= __h >> 13;
    __h *= __m;
    __h ^= __h >> 15;
    return static_cast<std::size_t>(__h);
#else
    // MurmurHash64A — matches libc++ __murmur2_or_cityhash<size_t, 64>
    static const std::uint64_t __m = 0xc6a4a7935bd1e995ULL;
    static const int __r = 47;
    std::uint64_t __h = static_cast<std::uint64_t>(__len) * __m;
    const unsigned char* __data = static_cast<const unsigned char*>(__key);
    const unsigned char* __end  = __data + (__len & ~static_cast<std::size_t>(7));
    while (__data != __end) {
        std::uint64_t __k;
        __builtin_memcpy(&__k, __data, 8);
        __k *= __m;
        __k ^= __k >> __r;
        __k *= __m;
        __h ^= __k;
        __h *= __m;
        __data += 8;
    }
    switch (__len & 7) {
    case 7: __h ^= static_cast<std::uint64_t>(__data[6]) << 48; [[fallthrough]];
    case 6: __h ^= static_cast<std::uint64_t>(__data[5]) << 40; [[fallthrough]];
    case 5: __h ^= static_cast<std::uint64_t>(__data[4]) << 32; [[fallthrough]];
    case 4: __h ^= static_cast<std::uint64_t>(__data[3]) << 24; [[fallthrough]];
    case 3: __h ^= static_cast<std::uint64_t>(__data[2]) << 16; [[fallthrough]];
    case 2: __h ^= static_cast<std::uint64_t>(__data[1]) << 8;  [[fallthrough]];
    case 1: __h ^= static_cast<std::uint64_t>(__data[0]);
            __h *= __m;
    }
    __h ^= __h >> __r;
    __h *= __m;
    __h ^= __h >> __r;
    return static_cast<std::size_t>(__h);
#endif
}

} // namespace __2
} // namespace std

#endif // __EMSCRIPTEN__
