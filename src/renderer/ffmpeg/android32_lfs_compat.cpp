// 32-bit Android (< API 24) compatibility for the prebuilt FFmpeg/x264
// armeabi-v7a binaries.
//
// Those static libraries were built with 64-bit file offsets
// (_FILE_OFFSET_BITS=64), so libx264 references ftello64/fseeko64. Bionic on
// 32-bit Android only exports those symbols from API 24 onwards, while White
// Bot targets API 23 (Geode's minimum), so the linker would otherwise fail
// with "undefined symbol: ftello64/fseeko64".
//
// White Bot itself is built without _FILE_OFFSET_BITS=64 (see CMakeLists.txt),
// so off_t is 32-bit here. These wrappers keep the build working on API 23
// while remaining correct for the file sizes a 32-bit process can actually
// address: offsets beyond the 32-bit off_t range are rejected with EOVERFLOW
// instead of silently wrapping.
//
// Consequence (documented in BUILD_AND_TEST_REPORT.md): rendering an output
// file larger than 2 GiB on armeabi-v7a is not supported. Such a buffer
// cannot be materialised in a 32-bit address space in the first place.
#if defined(__ANDROID__) && defined(__arm__) && defined(__ANDROID_API__) && __ANDROID_API__ < 24

#include <cerrno>
#include <climits>
#include <cstdio>
#include <sys/types.h>

extern "C" {

long long ftello64(FILE* stream) {
    off_t pos = ftello(stream);
    if (pos == static_cast<off_t>(-1)) {
        return -1;
    }
    return static_cast<long long>(pos);
}

int fseeko64(FILE* stream, long long offset, int whence) {
    if (offset > static_cast<long long>(INT_MAX) ||
        offset < static_cast<long long>(INT_MIN)) {
        errno = EOVERFLOW;
        return -1;
    }
    return fseeko(stream, static_cast<off_t>(offset), whence);
}

} // extern "C"

#endif
