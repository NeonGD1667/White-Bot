// gdr.cpp - implementation of the unified replay library. See gdr.hpp for the public API.
//
// Layout of this file:
//   1. byte utilities (Reader / Writer / varint / CRC32 / UTF-8)
//   2. model helpers, legacy White Bot / DLL Bot compatibility rules
//   3. shared section encoders (used by WBR and by the White Bot extension block "WBX1" inside GDR2)
//   4. codecs: WBR, GDR2, GDR1 (MessagePack + JSON)
//   5. codec registry, detection, public API, file I/O
#include "gdr.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <string_view>

#include "json.hpp"   // vendored nlohmann 3.11.3, same file White Bot already ships

namespace gdr {

// =============================================================================================
// 1. byte utilities
// =============================================================================================
namespace {

using Bytes = std::vector<std::uint8_t>;
using CSpan = std::span<const std::uint8_t>;
using json = nlohmann::json;

Error makeError(ErrorCode code, Format fmt, std::string msg, std::optional<std::size_t> off = std::nullopt) {
    Error e;
    e.code = code;
    e.format = fmt;
    e.message = std::move(msg);
    e.offset = off;
    return e;
}

/// Sticky-failure reader: after the first failure every read returns 0 / empty, so parsers can check failed() at
/// checkpoints. Never reads outside the span, never allocates based on file-supplied numbers.
class Reader {
public:
    Reader(CSpan data, Format fmt, std::size_t baseOffset = 0) : d_(data), fmt_(fmt), base_(baseOffset) {}

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] const Error& error() const noexcept { return err_; }
    [[nodiscard]] std::size_t pos() const noexcept { return pos_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return d_.size() - pos_; }
    [[nodiscard]] bool atEnd() const noexcept { return pos_ >= d_.size(); }
    [[nodiscard]] Format format() const noexcept { return fmt_; }

    void fail(ErrorCode c, std::string msg) {
        if (failed_) return;
        failed_ = true;
        err_ = makeError(c, fmt_, std::move(msg), base_ + pos_);
    }

    std::uint8_t u8() {
        if (!need(1)) return 0;
        return d_[pos_++];
    }
    std::uint32_t u32le() { return static_cast<std::uint32_t>(fixedLE(4)); }
    std::uint64_t u64le() { return fixedLE(8); }
    float f32le() { return std::bit_cast<float>(u32le()); }
    double f64le() { return std::bit_cast<double>(u64le()); }
    float f32be() { return std::bit_cast<float>(static_cast<std::uint32_t>(fixedBE(4))); }
    double f64be() { return std::bit_cast<double>(fixedBE(8)); }

    /// LEB128, at most 10 bytes, bit 64 overflow rejected. `strict` additionally rejects non-canonical (overlong)
    /// encodings (WBR). GDR2 files in the wild contain sign-extended negative ints (10 bytes) and are accepted.
    std::uint64_t varint(bool strict = false) {
        std::uint64_t v = 0;
        for (int i = 0; i < 10; ++i) {
            if (!need(1)) return 0;
            const std::uint8_t b = d_[pos_++];
            if (i == 9 && b > 1) {
                fail(ErrorCode::Corrupt, "varint does not fit in 64 bits");
                return 0;
            }
            v |= static_cast<std::uint64_t>(b & 0x7F) << (7 * i);
            if ((b & 0x80) == 0) {
                if (strict && i > 0 && b == 0) {
                    fail(ErrorCode::Corrupt, "non-canonical (overlong) varint");
                    return 0;
                }
                return v;
            }
        }
        fail(ErrorCode::Corrupt, "varint longer than 10 bytes");
        return 0;
    }

    std::int64_t svarint() {
        const std::uint64_t z = varint(true);
        return static_cast<std::int64_t>((z >> 1) ^ (~(z & 1) + 1));
    }

    /// varint length + bytes. `max` is checked before anything is copied.
    std::string str(std::size_t max, bool strictVarint = false) {
        const std::uint64_t len = varint(strictVarint);
        if (failed_) return {};
        if (len > max) {
            fail(ErrorCode::LimitExceeded, "string of " + std::to_string(len) + " bytes exceeds limit " +
                                               std::to_string(max));
            return {};
        }
        if (len > remaining()) {
            fail(ErrorCode::Truncated, "string runs past end of data");
            return {};
        }
        std::string s(reinterpret_cast<const char*>(d_.data() + pos_), static_cast<std::size_t>(len));
        pos_ += static_cast<std::size_t>(len);
        return s;
    }

    CSpan bytes(std::uint64_t n) {
        if (n > remaining()) {
            fail(ErrorCode::Truncated, "need " + std::to_string(n) + " bytes, " + std::to_string(remaining()) +
                                           " left");
            return {};
        }
        CSpan s = d_.subspan(pos_, static_cast<std::size_t>(n));
        pos_ += static_cast<std::size_t>(n);
        return s;
    }

    /// Sub-reader over the next n bytes (consumes them here).
    Reader sub(std::uint64_t n) {
        const std::size_t off = base_ + pos_;
        CSpan s = bytes(n);
        return Reader(s, fmt_, off);
    }

private:
    bool need(std::size_t n) {
        if (failed_) return false;
        if (n > remaining()) {
            fail(ErrorCode::Truncated, "unexpected end of data");
            return false;
        }
        return true;
    }
    std::uint64_t fixedLE(int n) {
        if (!need(static_cast<std::size_t>(n))) return 0;
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(d_[pos_ + static_cast<std::size_t>(i)]) << (8 * i);
        pos_ += static_cast<std::size_t>(n);
        return v;
    }
    std::uint64_t fixedBE(int n) {
        if (!need(static_cast<std::size_t>(n))) return 0;
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) v = (v << 8) | d_[pos_ + static_cast<std::size_t>(i)];
        pos_ += static_cast<std::size_t>(n);
        return v;
    }

    CSpan d_;
    Format fmt_;
    std::size_t base_ = 0;
    std::size_t pos_ = 0;
    bool failed_ = false;
    Error err_;
};

class Writer {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }
    void u32le(std::uint32_t v) { fixedLE(v, 4); }
    void u64le(std::uint64_t v) { fixedLE(v, 8); }
    void f32le(float v) { u32le(std::bit_cast<std::uint32_t>(v)); }
    void f64le(double v) { u64le(std::bit_cast<std::uint64_t>(v)); }
    void f32be(float v) { fixedBE(std::bit_cast<std::uint32_t>(v), 4); }
    void f64be(double v) { fixedBE(std::bit_cast<std::uint64_t>(v), 8); }
    void varint(std::uint64_t v) {
        while (v >= 0x80) {
            buf_.push_back(static_cast<std::uint8_t>(v | 0x80));
            v >>= 7;
        }
        buf_.push_back(static_cast<std::uint8_t>(v));
    }
    void svarint(std::int64_t v) {
        varint((static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63));
    }
    void str(std::string_view s) {
        varint(s.size());
        bytes({reinterpret_cast<const std::uint8_t*>(s.data()), s.size()});
    }
    void bytes(CSpan s) { buf_.insert(buf_.end(), s.begin(), s.end()); }
    void bytes(const Bytes& s) { buf_.insert(buf_.end(), s.begin(), s.end()); }
    void reserve(std::size_t n) { buf_.reserve(n); }
    [[nodiscard]] std::size_t size() const noexcept { return buf_.size(); }
    [[nodiscard]] const Bytes& data() const noexcept { return buf_; }
    Bytes release() noexcept { return std::move(buf_); }

private:
    void fixedLE(std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void fixedBE(std::uint64_t v, int n) {
        for (int i = n - 1; i >= 0; --i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    Bytes buf_;
};

constexpr std::array<std::uint32_t, 256> makeCrcTable() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        t[i] = c;
    }
    return t;
}

std::uint32_t crc32(CSpan data) noexcept {
    static constexpr auto table = makeCrcTable();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::uint8_t b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool hasPrefix(CSpan d, std::string_view magic) noexcept {
    return d.size() >= magic.size() && std::memcmp(d.data(), magic.data(), magic.size()) == 0;
}

constexpr std::uint32_t floatBits(float f) noexcept { return std::bit_cast<std::uint32_t>(f); }

}  // namespace

// =============================================================================================
// 2. small public pieces: names, Report, Replay helpers, compat rules
// =============================================================================================

std::string_view formatName(Format f) noexcept {
    switch (f) {
        case Format::Unknown: return "unknown";
        case Format::Auto: return "auto";
        case Format::Gdr1MsgPack: return "GDR1 (MessagePack)";
        case Format::Gdr1Json: return "GDR1 (JSON)";
        case Format::Gdr2: return "GDR2";
        case Format::Wbr: return "WBR";
        case Format::Hlr: return "HLR";
    }
    return "?";
}

std::string_view formatExtension(Format f) noexcept {
    switch (f) {
        case Format::Gdr1MsgPack: return ".gdr";
        case Format::Gdr1Json: return ".gdr.json";
        case Format::Gdr2: return ".gdr2";
        case Format::Wbr: return ".wbr";
        case Format::Hlr: return ".hlr";
        default: return "";
    }
}

std::string_view errorCodeName(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::Empty: return "Empty";
        case ErrorCode::Truncated: return "Truncated";
        case ErrorCode::Corrupt: return "Corrupt";
        case ErrorCode::ChecksumMismatch: return "ChecksumMismatch";
        case ErrorCode::InvalidValue: return "InvalidValue";
        case ErrorCode::UnknownFormat: return "UnknownFormat";
        case ErrorCode::UnsupportedVersion: return "UnsupportedVersion";
        case ErrorCode::NewerVersion: return "NewerVersion";
        case ErrorCode::UnsupportedFeature: return "UnsupportedFeature";
        case ErrorCode::NotImplemented: return "NotImplemented";
        case ErrorCode::LimitExceeded: return "LimitExceeded";
        case ErrorCode::LossyConversion: return "LossyConversion";
        case ErrorCode::Io: return "Io";
        case ErrorCode::Internal: return "Internal";
    }
    return "?";
}

std::string Error::toString() const {
    std::string s(errorCodeName(code));
    if (format != Format::Unknown) s += " [" + std::string(formatName(format)) + "]";
    s += ": " + message;
    if (offset) s += " (at byte " + std::to_string(*offset) + ")";
    return s;
}

void Report::warn(std::string code, std::string message) {
    items.push_back({Severity::Warning, std::move(code), std::move(message)});
}
void Report::info(std::string code, std::string message) {
    items.push_back({Severity::Info, std::move(code), std::move(message)});
}
bool Report::hasWarnings() const noexcept {
    return std::any_of(items.begin(), items.end(), [](const Diagnostic& d) { return d.severity == Severity::Warning; });
}
bool Report::has(std::string_view code) const noexcept {
    return std::any_of(items.begin(), items.end(), [&](const Diagnostic& d) { return d.code == code; });
}

void Replay::sortInputs() {
    std::stable_sort(inputs.begin(), inputs.end(),
                     [](const Input& a, const Input& b) { return a.frame < b.frame; });
}

std::span<const std::uint8_t> Replay::inputExtension(const Input& in) const noexcept {
    if (in.extIndex == 0 || in.extIndex > inputExt.spans.size()) return {};
    const auto& s = inputExt.spans[in.extIndex - 1];
    if (s.offset > inputExt.arena.size() || s.size > inputExt.arena.size() - s.offset) return {};
    return {inputExt.arena.data() + s.offset, s.size};
}

bool Replay::setInputExtension(Input& in, ExtensionKind kind, std::string_view tag, CSpan bytes) {
    if (kind == ExtensionKind::None) return false;
    if (inputExt.kind == ExtensionKind::None && inputExt.spans.empty()) {
        inputExt.kind = kind;
        inputExt.tag = std::string(tag);
    } else if (inputExt.kind != kind || inputExt.tag != tag) {
        return false;
    }
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
        inputExt.spans.size() >= std::numeric_limits<std::uint32_t>::max())
        return false;
    inputExt.spans.push_back({inputExt.arena.size(), static_cast<std::uint32_t>(bytes.size())});
    inputExt.arena.insert(inputExt.arena.end(), bytes.begin(), bytes.end());
    in.extIndex = static_cast<std::uint32_t>(inputExt.spans.size());
    return true;
}

std::optional<Error> Replay::validate() const {
    bool anyExt = false;
    for (const Input& in : inputs) {
        if (in.extIndex == 0) continue;
        anyExt = true;
        if (in.extIndex > inputExt.spans.size())
            return makeError(ErrorCode::InvalidValue, Format::Unknown, "input extension index out of range");
    }
    for (const auto& s : inputExt.spans)
        if (s.offset > inputExt.arena.size() || s.size > inputExt.arena.size() - s.offset)
            return makeError(ErrorCode::InvalidValue, Format::Unknown, "input extension span outside arena");
    if (anyExt && inputExt.kind == ExtensionKind::None)
        return makeError(ErrorCode::InvalidValue, Format::Unknown, "input extensions present but kind is None");
    return std::nullopt;
}

Detection ICodec::inspect(CSpan data) const {
    Detection d;
    if (sniff(data)) {
        d.format = format();
        d.confidence = Detection::Confidence::MagicOnly;
        d.note = "first bytes match " + std::string(name());
    }
    return d;
}

// ---- legacy White Bot / DLL Bot compatibility rules (literal ports, see gdr_legacy notes) --------------
namespace {

/// Geode: geode::utils::numFromString<int>(s).unwrapOr(0). The Geode implementation returns Ok as soon as
/// fast_float parsed a number, even with trailing characters ("6-beta" -> 6); no digits / out of range -> Err -> 0.
int geodeNumFromStringOr0(std::string_view s) noexcept {
    std::size_t i = 0;
    bool neg = false;
    if (i < s.size() && s[i] == '-') {
        neg = true;
        ++i;
    }
    const std::size_t digitsStart = i;
    std::uint64_t v = 0;
    bool overflow = false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        const unsigned d = static_cast<unsigned>(s[i] - '0');
        if (v > (std::numeric_limits<std::uint64_t>::max() - d) / 10) overflow = true;
        else v = v * 10 + d;
        ++i;
    }
    if (i == digitsStart || overflow) return 0;
    if (!neg) {
        if (v > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return 0;
        return static_cast<int>(v);
    }
    if (v > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1) return 0;
    return static_cast<int>(-static_cast<std::int64_t>(v));
}

std::vector<std::string_view> splitByChar(std::string_view s, char c) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t e = s.find(c, start);
        if (e == std::string_view::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, e - start));
        start = e + 1;
    }
}

struct Ver3 {
    std::size_t major, minor, patch;   // Geode converts the int result to size_t; negative -> huge (kept literally)
    bool operator>=(const Ver3& o) const noexcept {
        if (major != o.major) return major > o.major;
        if (minor != o.minor) return minor > o.minor;
        return patch >= o.patch;
    }
};

Ver3 geodeGetVersion(const std::vector<std::string_view>& nums) {
    if (nums.empty()) return {0, 0, 0};
    const auto conv = [](std::string_view s) { return static_cast<std::size_t>(geodeNumFromStringOr0(s)); };
    return {conv(nums[0]), nums.size() > 1 ? conv(nums[1]) : 0, nums.size() > 2 ? conv(nums[2]) : 0};
}

bool isDllBot(const Bot& b) noexcept { return b.name == "Dll Bot"; }

}  // namespace

int legacyDllBotFrameOffset(const Bot& bot) noexcept {
    if (!isDllBot(bot)) return 0;
    std::string_view ver = bot.version;
    if (!ver.empty() && ver.front() == 'v') ver.remove_prefix(1);
    const auto parts = splitByChar(ver, '.');
    if (parts.size() <= 3) {
        if (geodeGetVersion(parts) >= Ver3{2, 3, 6}) return 0;
    }
    return 1;
}

bool legacyRotationAllowed(const Bot& bot) noexcept {
    // White Bot: bool rotation = ver has no "beta."/"alpha."; Dll Bot v3.0.1 forces true.
    // In the "p1"/"p2" frame-fix branch it is then forced to false for every bot that is not "Dll Bot".
    if (!isDllBot(bot)) return false;
    const std::string_view ver = bot.version;
    bool rotation = ver.find("beta.") == std::string_view::npos && ver.find("alpha.") == std::string_view::npos;
    if (ver == "v3.0.1") rotation = true;
    return rotation;
}

// =============================================================================================
// 3. shared section encoders (WBR sections, and the "WBX1" block inside GDR2)
// =============================================================================================
namespace {

constexpr std::uint64_t kSignedDeltaLimit = std::uint64_t{1} << 62;

constexpr std::uint64_t zigzag(std::int64_t v) noexcept {
    return (static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63);
}
constexpr std::int64_t unzigzag(std::uint64_t z) noexcept {
    return static_cast<std::int64_t>(z >> 1) ^ -static_cast<std::int64_t>(z & 1);
}

// ---- frame delta helpers (monotonic -> plain delta, otherwise zigzag) -----------------------------
template <typename Get>
bool isNonDecreasing(std::size_t n, Get get) {
    for (std::size_t i = 1; i < n; ++i)
        if (get(i) < get(i - 1)) return false;
    return true;
}

template <typename Get>
bool framesFitSigned(std::size_t n, Get get) {
    for (std::size_t i = 0; i < n; ++i)
        if (get(i) > kSignedDeltaLimit) return false;
    return true;
}

/// Reads one frame delta; updates `prev`. Overflow / negative results are Corrupt.
std::uint64_t readFrame(Reader& r, std::uint64_t prev, bool isSigned) {
    const std::uint64_t raw = r.varint(true);
    if (r.failed()) return 0;
    if (!isSigned) {
        if (raw > std::numeric_limits<std::uint64_t>::max() - prev) {
            r.fail(ErrorCode::Corrupt, "frame number overflows 64 bits");
            return 0;
        }
        return prev + raw;
    }
    const std::int64_t d = unzigzag(raw);
    const std::int64_t f = static_cast<std::int64_t>(prev) + d;   // prev <= 2^62 and |d| < 2^63: no overflow
    if (f < 0 || static_cast<std::uint64_t>(f) > kSignedDeltaLimit) {
        r.fail(ErrorCode::Corrupt, "frame number out of range");
        return 0;
    }
    return static_cast<std::uint64_t>(f);
}

// ---- INPUTS -------------------------------------------------------------------------------------
// varint count, u8 flags {bits0-1 buttonMode(0 implicit 1 / 1 two-bit / 2 explicit varint), bit2 signedDeltas,
// bit3 hasP2}, then per input one varint: (delta << shift) | [button2 << ..] | [p2 << 1] | down
std::optional<Error> encodeInputs(const Replay& rp, Writer& w) {
    const auto& in = rp.inputs;
    bool hasP2 = false;
    unsigned btn = 0;
    for (const Input& x : in) {
        hasP2 |= x.player2;
        if (x.button != 1) btn = std::max(btn, x.button <= 3 ? 1u : 2u);
    }
    const auto frameOf = [&](std::size_t i) { return in[i].frame; };
    const bool sorted = isNonDecreasing(in.size(), frameOf);
    if (!sorted && !framesFitSigned(in.size(), frameOf))
        return makeError(ErrorCode::LimitExceeded, Format::Wbr, "unsorted inputs with frame > 2^62 cannot be encoded");

    const unsigned shift = 1 + (hasP2 ? 1u : 0u) + (btn == 1 ? 2u : 0u);
    w.varint(in.size());
    w.u8(static_cast<std::uint8_t>(btn | (sorted ? 0u : 4u) | (hasP2 ? 8u : 0u)));
    std::uint64_t prev = 0;
    for (const Input& x : in) {
        const std::uint64_t d = sorted ? x.frame - prev
                                       : zigzag(static_cast<std::int64_t>(x.frame) - static_cast<std::int64_t>(prev));
        if (d > (std::numeric_limits<std::uint64_t>::max() >> shift))
            return makeError(ErrorCode::LimitExceeded, Format::Wbr, "frame delta too large to pack");
        std::uint64_t packed = (d << shift) | (x.down ? 1u : 0u);
        unsigned bit = 1;
        if (hasP2) packed |= static_cast<std::uint64_t>(x.player2) << bit++;
        if (btn == 1) packed |= static_cast<std::uint64_t>(x.button & 3u) << bit;
        w.varint(packed);
        if (btn == 2) w.varint(x.button);
        prev = x.frame;
    }
    return std::nullopt;
}

void decodeInputs(Reader& r, Replay& out, const Limits& lim) {
    const std::uint64_t n = r.varint(true);
    const std::uint8_t flags = r.u8();
    if (r.failed()) return;
    if ((flags & ~0x0Fu) != 0 || (flags & 3u) == 3u) {
        r.fail(ErrorCode::UnsupportedFeature, "unknown input-section flags");
        return;
    }
    if (n > lim.maxInputs) {
        r.fail(ErrorCode::LimitExceeded, "input count " + std::to_string(n) + " exceeds limit");
        return;
    }
    if (n > r.remaining()) {   // every input needs >= 1 byte
        r.fail(ErrorCode::Corrupt, "input count larger than remaining data");
        return;
    }
    const unsigned btn = flags & 3u;
    const bool isSigned = (flags & 4u) != 0;
    const bool hasP2 = (flags & 8u) != 0;
    const unsigned shift = 1 + (hasP2 ? 1u : 0u) + (btn == 1 ? 2u : 0u);
    out.inputs.clear();
    out.inputs.reserve(static_cast<std::size_t>(n));
    std::uint64_t prev = 0;
    for (std::uint64_t i = 0; i < n && !r.failed(); ++i) {
        std::uint64_t packed = r.varint(true);
        Input x;
        x.down = (packed & 1) != 0;
        unsigned bit = 1;
        if (hasP2) x.player2 = ((packed >> bit++) & 1) != 0;
        x.button = 1;
        if (btn == 1) x.button = static_cast<std::uint8_t>((packed >> bit) & 3u);
        const std::uint64_t deltaField = packed >> shift;
        // re-use readFrame's arithmetic on the already-extracted delta
        if (!isSigned) {
            if (deltaField > std::numeric_limits<std::uint64_t>::max() - prev) {
                r.fail(ErrorCode::Corrupt, "frame number overflows 64 bits");
                break;
            }
            x.frame = prev + deltaField;
        } else {
            const std::int64_t f = static_cast<std::int64_t>(prev) + unzigzag(deltaField);
            if (f < 0 || static_cast<std::uint64_t>(f) > kSignedDeltaLimit) {
                r.fail(ErrorCode::Corrupt, "frame number out of range");
                break;
            }
            x.frame = static_cast<std::uint64_t>(f);
        }
        if (btn == 2) {
            const std::uint64_t b = r.varint(true);
            if (b > 255) {
                r.fail(ErrorCode::Corrupt, "button id > 255");
                break;
            }
            x.button = static_cast<std::uint8_t>(b);
        }
        prev = x.frame;
        if (!r.failed()) out.inputs.push_back(x);
    }
}

// ---- float columns: XOR with previous value, drop leading zero bytes (lossless, bit-exact) -----------
template <typename Get>
void encodeFloatColumn(Writer& w, std::size_t n, Get getBits) {
    Bytes ctrl((n + 1) / 2, 0);
    Bytes data;
    data.reserve(n * 3);
    std::uint32_t prev = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t bits = getBits(i);
        const std::uint32_t x = bits ^ prev;
        prev = bits;
        const unsigned lead = x == 0 ? 4u : static_cast<unsigned>(std::countl_zero(x)) / 8u;
        ctrl[i / 2] |= static_cast<std::uint8_t>(lead << ((i & 1) * 4));
        for (int b = 3 - static_cast<int>(lead); b >= 0; --b) data.push_back(static_cast<std::uint8_t>(x >> (8 * b)));
    }
    w.bytes(ctrl);
    w.bytes(data);
}

template <typename Set>
void decodeFloatColumn(Reader& r, std::size_t n, Set setBits) {
    const CSpan ctrl = r.bytes((n + 1) / 2);
    if (r.failed()) return;
    std::uint32_t prev = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned lead = (ctrl[i / 2] >> ((i & 1) * 4)) & 0xFu;
        if (lead > 4) {
            r.fail(ErrorCode::Corrupt, "invalid float-column control nibble");
            return;
        }
        const CSpan b = r.bytes(4 - lead);
        if (r.failed()) return;
        std::uint32_t x = 0;
        for (std::uint8_t byte : b) x = (x << 8) | byte;
        prev ^= x;
        setBits(i, prev);
    }
}

// ---- FRAMEFIXES ---------------------------------------------------------------------------------
// varint count, u8 flags {bit0 signedDeltas, bit1 hasP2 columns, bit2 hasNoRotate bitmap},
// count frame varints, [bitmap: 2 bits/fix = (!p1.rotate, !p2.rotate)], float columns p1.x p1.y p1.r [p2.x p2.y p2.r]
std::optional<Error> encodeFrameFixes(const Replay& rp, Writer& w) {
    const auto& f = rp.frameFixes;
    bool hasP2 = false, noRot = false;
    for (const FrameFix& x : f) {
        hasP2 |= floatBits(x.p2.pos.x) != 0 || floatBits(x.p2.pos.y) != 0 || floatBits(x.p2.rotation) != 0;
        noRot |= !x.p1.rotate || !x.p2.rotate;
    }
    const auto frameOf = [&](std::size_t i) { return f[i].frame; };
    const bool sorted = isNonDecreasing(f.size(), frameOf);
    if (!sorted && !framesFitSigned(f.size(), frameOf))
        return makeError(ErrorCode::LimitExceeded, Format::Wbr, "unsorted frame fixes with frame > 2^62");

    w.varint(f.size());
    w.u8(static_cast<std::uint8_t>((sorted ? 0u : 1u) | (hasP2 ? 2u : 0u) | (noRot ? 4u : 0u)));
    std::uint64_t prev = 0;
    for (const FrameFix& x : f) {
        w.varint(sorted ? x.frame - prev
                        : zigzag(static_cast<std::int64_t>(x.frame) - static_cast<std::int64_t>(prev)));
        prev = x.frame;
    }
    if (noRot) {
        Bytes bm((f.size() * 2 + 7) / 8, 0);
        for (std::size_t i = 0; i < f.size(); ++i) {
            if (!f[i].p1.rotate) bm[(i * 2) / 8] |= static_cast<std::uint8_t>(1u << ((i * 2) % 8));
            if (!f[i].p2.rotate) bm[(i * 2 + 1) / 8] |= static_cast<std::uint8_t>(1u << ((i * 2 + 1) % 8));
        }
        w.bytes(bm);
    }
    const std::size_t n = f.size();
    encodeFloatColumn(w, n, [&](std::size_t i) { return floatBits(f[i].p1.pos.x); });
    encodeFloatColumn(w, n, [&](std::size_t i) { return floatBits(f[i].p1.pos.y); });
    encodeFloatColumn(w, n, [&](std::size_t i) { return floatBits(f[i].p1.rotation); });
    if (hasP2) {
        encodeFloatColumn(w, n, [&](std::size_t i) { return floatBits(f[i].p2.pos.x); });
        encodeFloatColumn(w, n, [&](std::size_t i) { return floatBits(f[i].p2.pos.y); });
        encodeFloatColumn(w, n, [&](std::size_t i) { return floatBits(f[i].p2.rotation); });
    }
    return std::nullopt;
}

void decodeFrameFixes(Reader& r, Replay& out, const Limits& lim) {
    const std::uint64_t n = r.varint(true);
    const std::uint8_t flags = r.u8();
    if (r.failed()) return;
    if ((flags & ~0x07u) != 0) {
        r.fail(ErrorCode::UnsupportedFeature, "unknown frame-fix flags");
        return;
    }
    if (n > lim.maxFrameFixes) {
        r.fail(ErrorCode::LimitExceeded, "frame-fix count exceeds limit");
        return;
    }
    if (n > r.remaining()) {
        r.fail(ErrorCode::Corrupt, "frame-fix count larger than remaining data");
        return;
    }
    const bool isSigned = (flags & 1u) != 0, hasP2 = (flags & 2u) != 0, noRot = (flags & 4u) != 0;
    const std::size_t cnt = static_cast<std::size_t>(n);
    out.frameFixes.assign(cnt, FrameFix{});
    std::uint64_t prev = 0;
    for (std::size_t i = 0; i < cnt && !r.failed(); ++i) {
        prev = readFrame(r, prev, isSigned);
        out.frameFixes[i].frame = prev;
    }
    if (r.failed()) return;
    if (noRot) {
        const CSpan bm = r.bytes((cnt * 2 + 7) / 8);
        if (r.failed()) return;
        for (std::size_t i = 0; i < cnt; ++i) {
            out.frameFixes[i].p1.rotate = ((bm[(i * 2) / 8] >> ((i * 2) % 8)) & 1u) == 0;
            out.frameFixes[i].p2.rotate = ((bm[(i * 2 + 1) / 8] >> ((i * 2 + 1) % 8)) & 1u) == 0;
        }
    }
    auto& f = out.frameFixes;
    decodeFloatColumn(r, cnt, [&](std::size_t i, std::uint32_t b) { f[i].p1.pos.x = std::bit_cast<float>(b); });
    decodeFloatColumn(r, cnt, [&](std::size_t i, std::uint32_t b) { f[i].p1.pos.y = std::bit_cast<float>(b); });
    decodeFloatColumn(r, cnt, [&](std::size_t i, std::uint32_t b) { f[i].p1.rotation = std::bit_cast<float>(b); });
    if (hasP2) {
        decodeFloatColumn(r, cnt, [&](std::size_t i, std::uint32_t b) { f[i].p2.pos.x = std::bit_cast<float>(b); });
        decodeFloatColumn(r, cnt, [&](std::size_t i, std::uint32_t b) { f[i].p2.pos.y = std::bit_cast<float>(b); });
        decodeFloatColumn(r, cnt, [&](std::size_t i, std::uint32_t b) { f[i].p2.rotation = std::bit_cast<float>(b); });
    }
}

// ---- DEATHS -------------------------------------------------------------------------------------
void encodeDeaths(const Replay& rp, Writer& w) {
    const auto& d = rp.deaths;
    const auto get = [&](std::size_t i) { return d[i]; };
    const bool sorted = isNonDecreasing(d.size(), get) ;
    const bool useSigned = !sorted;
    w.varint(d.size());
    w.u8(useSigned ? 1 : 0);
    std::uint64_t prev = 0;
    for (std::uint64_t v : d) {
        // unsorted + huge values are written as raw zigzag of the wrapped difference; decoder mirrors it
        w.varint(useSigned ? zigzag(static_cast<std::int64_t>(v - prev)) : v - prev);
        prev = v;
    }
}

void decodeDeaths(Reader& r, Replay& out, const Limits& lim) {
    const std::uint64_t n = r.varint(true);
    const std::uint8_t flags = r.u8();
    if (r.failed()) return;
    if ((flags & ~1u) != 0) {
        r.fail(ErrorCode::UnsupportedFeature, "unknown death-section flags");
        return;
    }
    if (n > lim.maxDeaths) {
        r.fail(ErrorCode::LimitExceeded, "death count exceeds limit");
        return;
    }
    if (n > r.remaining()) {
        r.fail(ErrorCode::Corrupt, "death count larger than remaining data");
        return;
    }
    const bool isSigned = (flags & 1u) != 0;
    out.deaths.clear();
    out.deaths.reserve(static_cast<std::size_t>(n));
    std::uint64_t prev = 0;
    for (std::uint64_t i = 0; i < n && !r.failed(); ++i) {
        const std::uint64_t raw = r.varint(true);
        prev = isSigned ? prev + static_cast<std::uint64_t>(unzigzag(raw)) : prev + raw;   // wrap == encoder wrap
        out.deaths.push_back(prev);
    }
}

// ---- replay-level / input-level extensions ----------------------------------------------------------
void encodeReplayExt(const ExtensionBlob& e, Writer& w) {
    w.u8(static_cast<std::uint8_t>(e.kind));
    w.str(e.tag);
    w.varint(e.data.size());
    w.bytes(e.data);
}

void decodeReplayExt(Reader& r, Replay& out, const Limits& lim) {
    const std::uint8_t kind = r.u8();
    std::string tag = r.str(lim.maxStringBytes, true);
    const std::uint64_t len = r.varint(true);
    if (r.failed()) return;
    if (kind != 1 && kind != 2) {
        r.fail(ErrorCode::UnsupportedFeature, "unknown replay-extension kind");
        return;
    }
    if (len > lim.maxExtensionBytes) {
        r.fail(ErrorCode::LimitExceeded, "replay extension exceeds limit");
        return;
    }
    const CSpan b = r.bytes(len);
    if (r.failed()) return;
    out.replayExt.kind = static_cast<ExtensionKind>(kind);
    out.replayExt.tag = std::move(tag);
    out.replayExt.data.assign(b.begin(), b.end());
}

void encodeInputExt(const Replay& rp, Writer& w) {
    w.u8(static_cast<std::uint8_t>(rp.inputExt.kind));
    w.str(rp.inputExt.tag);
    std::uint64_t present = 0;
    for (const Input& in : rp.inputs) present += rp.inputExtension(in).empty() ? 0 : 1;
    w.varint(present);
    std::uint64_t prevIdx = 0;
    for (std::size_t i = 0; i < rp.inputs.size(); ++i) {
        const CSpan e = rp.inputExtension(rp.inputs[i]);
        if (e.empty()) continue;
        w.varint(i - prevIdx);
        w.varint(e.size());
        w.bytes(e);
        prevIdx = i;
    }
}

void decodeInputExt(Reader& r, Replay& out, const Limits& lim) {
    const std::uint8_t kind = r.u8();
    std::string tag = r.str(lim.maxStringBytes, true);
    const std::uint64_t present = r.varint(true);
    if (r.failed()) return;
    if (kind != 1 && kind != 2) {
        r.fail(ErrorCode::UnsupportedFeature, "unknown input-extension kind");
        return;
    }
    if (present > out.inputs.size() || present > r.remaining()) {
        r.fail(ErrorCode::Corrupt, "input-extension count larger than input count");
        return;
    }
    out.inputExt = {};
    out.inputExt.kind = static_cast<ExtensionKind>(kind);
    out.inputExt.tag = std::move(tag);
    std::uint64_t idx = 0, total = 0;
    for (std::uint64_t k = 0; k < present && !r.failed(); ++k) {
        const std::uint64_t d = r.varint(true);
        const std::uint64_t len = r.varint(true);
        if (r.failed()) return;
        if (k > 0 && d == 0) {
            r.fail(ErrorCode::Corrupt, "duplicate input-extension index");
            return;
        }
        if (d > out.inputs.size() || idx + d >= out.inputs.size()) {
            r.fail(ErrorCode::Corrupt, "input-extension index out of range");
            return;
        }
        idx += d;
        if (len > std::numeric_limits<std::uint32_t>::max() || len > lim.maxExtensionBytes - std::min<std::uint64_t>(total, lim.maxExtensionBytes)) {
            r.fail(ErrorCode::LimitExceeded, "input extensions exceed limit");
            return;
        }
        const CSpan b = r.bytes(len);
        if (r.failed()) return;
        total += len;
        out.inputExt.spans.push_back({out.inputExt.arena.size(), static_cast<std::uint32_t>(len)});
        out.inputExt.arena.insert(out.inputExt.arena.end(), b.begin(), b.end());
        out.inputs[static_cast<std::size_t>(idx)].extIndex = static_cast<std::uint32_t>(out.inputExt.spans.size());
    }
}

}  // namespace
// =============================================================================================
// 4a. WBR codec
// =============================================================================================
//
//  offset size field
//   0     4    magic            57 42 52 1A  ("WBR" + Ctrl-Z)
//   4     1    format major     1   (reader: >1 -> NewerVersion, 0 -> UnsupportedVersion)
//   5     1    format minor     0   (any minor of a supported major is read; unknown optional sections are skipped)
//   6     1    compression id   0 = none (any other id -> UnsupportedFeature)
//   7     1    reserved         0
//   8     4    flags (u32 LE)   low 16 bits: informational (1 frame fixes, 2 deaths, 4 input ext, 8 replay ext)
//                               high 16 bits: REQUIRED features; any set bit this reader does not know -> UnsupportedFeature
//  12     4    rawSize   (u32 LE) payload size after decompression
//  16     4    storedSize(u32 LE) payload size as stored (== rawSize while compression == 0)
//  20     4    CRC-32 (IEEE) of the raw payload (u32 LE)
//  24     ...  payload = sequence of sections: u8 id, varint length, bytes.
//               id < 0x80: critical (unknown -> UnsupportedFeature); id >= 0x80: optional (unknown -> skipped)
//
//  Everything fixed-width is little-endian. Floats are raw IEEE-754 bit patterns (never quantized).
//  Varints are canonical unsigned LEB128 (<= 10 bytes); signed values are zigzag varints.
namespace {

constexpr std::string_view kWbrMagic{"WBR\x1A", 4};
constexpr std::size_t kWbrHeaderSize = 24;
constexpr std::uint8_t kWbrMajor = 1;
constexpr std::uint8_t kWbrMinor = 0;

enum : std::uint8_t {
    kSecMeta = 0x01,
    kSecInputs = 0x02,
    kSecFrameFixes = 0x03,
    kSecDeaths = 0x04,
    kSecReplayExt = 0x81,
    kSecInputExt = 0x82,
};

std::optional<Error> toInt32(std::int64_t v, const char* what, Format fmt) {
    if (v < std::numeric_limits<std::int32_t>::min() || v > std::numeric_limits<std::int32_t>::max())
        return makeError(ErrorCode::InvalidValue, fmt, std::string(what) + " does not fit in 32 bits");
    return std::nullopt;
}

void encodeMeta(const Replay& rp, Writer& w) {
    const std::uint8_t bits = static_cast<std::uint8_t>((rp.ldm ? 1u : 0u) | (rp.platformer ? 2u : 0u) |
                                                         (rp.bot.versionNumber ? 4u : 0u) |
                                                         (rp.lastRecordedFrame != 0 ? 8u : 0u));
    w.u8(bits);
    w.f32le(rp.duration);
    w.f64le(rp.framerate);
    w.svarint(rp.gameVersion);
    w.svarint(rp.seed);
    w.svarint(rp.coins);
    w.varint(rp.level.id);
    w.str(rp.author);
    w.str(rp.description);
    w.str(rp.level.name);
    w.str(rp.bot.name);
    w.str(rp.bot.version);
    if (rp.bot.versionNumber) w.svarint(*rp.bot.versionNumber);
    if (rp.lastRecordedFrame != 0) w.varint(rp.lastRecordedFrame);
}

void decodeMeta(Reader& r, Replay& out, const Limits& lim) {
    const std::uint8_t bits = r.u8();
    if (r.failed()) return;
    if ((bits & ~0x0Fu) != 0) {
        r.fail(ErrorCode::UnsupportedFeature, "unknown metadata flags");
        return;
    }
    out.ldm = (bits & 1) != 0;
    out.platformer = (bits & 2) != 0;
    out.duration = r.f32le();
    out.framerate = r.f64le();
    const std::int64_t gv = r.svarint(), seed = r.svarint(), coins = r.svarint();
    const std::uint64_t levelId = r.varint(true);
    if (r.failed()) return;
    for (const auto& [v, what] : {std::pair{gv, "gameVersion"}, std::pair{seed, "seed"}, std::pair{coins, "coins"}})
        if (auto e = toInt32(v, what, Format::Wbr)) {
            r.fail(e->code, e->message);
            return;
        }
    if (levelId > std::numeric_limits<std::uint32_t>::max()) {
        r.fail(ErrorCode::InvalidValue, "level id does not fit in 32 bits");
        return;
    }
    out.gameVersion = static_cast<std::int32_t>(gv);
    out.seed = static_cast<std::int32_t>(seed);
    out.coins = static_cast<std::int32_t>(coins);
    out.level.id = static_cast<std::uint32_t>(levelId);
    out.author = r.str(lim.maxStringBytes, true);
    out.description = r.str(lim.maxStringBytes, true);
    out.level.name = r.str(lim.maxStringBytes, true);
    out.bot.name = r.str(lim.maxStringBytes, true);
    out.bot.version = r.str(lim.maxStringBytes, true);
    if (bits & 4) out.bot.versionNumber = r.svarint();
    if (bits & 8) {
        const std::uint64_t lrf = r.varint(true);
        if (lrf > std::numeric_limits<std::uint32_t>::max()) {
            r.fail(ErrorCode::InvalidValue, "lastRecordedFrame does not fit in 32 bits");
            return;
        }
        out.lastRecordedFrame = static_cast<std::uint32_t>(lrf);
    }
}

class WbrCodec final : public ICodec {
public:
    Format format() const noexcept override { return Format::Wbr; }
    std::string_view name() const noexcept override { return "WBR"; }
    bool sniff(CSpan d) const noexcept override { return hasPrefix(d, kWbrMagic); }

    Detection inspect(CSpan d) const override {
        Detection det;
        if (!sniff(d)) return det;
        det.format = Format::Wbr;
        det.confidence = Detection::Confidence::MagicOnly;
        if (d.size() < kWbrHeaderSize) {
            det.note = "WBR magic but header is truncated";
            return det;
        }
        det.note = "WBR " + std::to_string(d[4]) + "." + std::to_string(d[5]);
        if (d[4] == kWbrMajor) det.confidence = Detection::Confidence::Structure;
        else det.note += " (unsupported major version)";
        return det;
    }

    Result<Bytes> encode(const Replay& rp, const ExportOptions& opt, Report& rep) const override {
        if (auto e = rp.validate()) {
            e->format = Format::Wbr;
            return *e;
        }
        if (opt.wbr.compression != WbrCompression::None)
            return makeError(ErrorCode::UnsupportedFeature, Format::Wbr, "unknown WBR compression id");

        Writer payload;
        const auto section = [&](std::uint8_t id, const Writer& body) {
            payload.u8(id);
            payload.varint(body.size());
            payload.bytes(body.data());
        };
        std::uint32_t flags = 0;
        {
            Writer s;
            encodeMeta(rp, s);
            section(kSecMeta, s);
        }
        if (!rp.inputs.empty()) {
            Writer s;
            if (auto e = encodeInputs(rp, s)) return *e;
            section(kSecInputs, s);
        }
        if (!rp.frameFixes.empty()) {
            Writer s;
            if (auto e = encodeFrameFixes(rp, s)) return *e;
            section(kSecFrameFixes, s);
            flags |= 1;
        }
        if (!rp.deaths.empty()) {
            Writer s;
            encodeDeaths(rp, s);
            section(kSecDeaths, s);
            flags |= 2;
        }
        if (rp.replayExt.kind != ExtensionKind::None) {
            Writer s;
            encodeReplayExt(rp.replayExt, s);
            section(kSecReplayExt, s);
            flags |= 8;
        }
        if (rp.inputExt.kind != ExtensionKind::None) {
            Writer s;
            encodeInputExt(rp, s);
            section(kSecInputExt, s);
            flags |= 4;
        }
        if (payload.size() > std::numeric_limits<std::uint32_t>::max())
            return makeError(ErrorCode::LimitExceeded, Format::Wbr, "payload larger than 4 GiB");
        (void)rep;

        Writer out;
        out.reserve(kWbrHeaderSize + payload.size());
        out.bytes({reinterpret_cast<const std::uint8_t*>(kWbrMagic.data()), kWbrMagic.size()});
        out.u8(kWbrMajor);
        out.u8(kWbrMinor);
        out.u8(static_cast<std::uint8_t>(opt.wbr.compression));
        out.u8(0);
        out.u32le(flags);
        out.u32le(static_cast<std::uint32_t>(payload.size()));
        out.u32le(static_cast<std::uint32_t>(payload.size()));
        out.u32le(crc32(payload.data()));
        out.bytes(payload.data());
        return out.release();
    }

    Result<Replay> decode(CSpan data, const ImportOptions& opt, Report& rep) const override {
        const Limits& lim = opt.limits;
        if (data.empty()) return makeError(ErrorCode::Empty, Format::Wbr, "empty input");
        if (!sniff(data)) {
            if (data.size() < kWbrMagic.size() && std::memcmp(data.data(), kWbrMagic.data(), data.size()) == 0)
                return makeError(ErrorCode::Truncated, Format::Wbr, "file shorter than the WBR magic");
            return makeError(ErrorCode::UnknownFormat, Format::Wbr, "not a WBR file (bad magic)", 0);
        }
        if (data.size() < kWbrHeaderSize)
            return makeError(ErrorCode::Truncated, Format::Wbr, "header is truncated", data.size());
        if (data.size() > lim.maxFileBytes)
            return makeError(ErrorCode::LimitExceeded, Format::Wbr, "file larger than Limits::maxFileBytes");

        Reader h(data.subspan(4, kWbrHeaderSize - 4), Format::Wbr, 4);
        const std::uint8_t major = h.u8(), minor = h.u8(), comp = h.u8(), reserved = h.u8();
        const std::uint32_t flags = h.u32le(), rawSize = h.u32le(), storedSize = h.u32le(), crc = h.u32le();

        if (major > kWbrMajor)
            return makeError(ErrorCode::NewerVersion, Format::Wbr,
                             "file is WBR " + std::to_string(major) + "." + std::to_string(minor) +
                                 ", this build reads WBR " + std::to_string(kWbrMajor) + ".x",
                             4);
        if (major < kWbrMajor)
            return makeError(ErrorCode::UnsupportedVersion, Format::Wbr,
                             "WBR major version " + std::to_string(major) + " never existed", 4);
        if (reserved != 0)
            return makeError(ErrorCode::UnsupportedFeature, Format::Wbr, "reserved header byte is not 0", 7);
        if ((flags & 0xFFFF0000u) != 0)
            return makeError(ErrorCode::UnsupportedFeature, Format::Wbr,
                             "file requires features this build does not know (flags 0x" +
                                 std::to_string(flags >> 16) + ")",
                             8);
        if (comp != 0)
            return makeError(ErrorCode::UnsupportedFeature, Format::Wbr,
                             "unknown compression id " + std::to_string(comp), 6);
        if (rawSize != storedSize)
            return makeError(ErrorCode::Corrupt, Format::Wbr, "rawSize != storedSize for uncompressed payload", 12);
        if (rawSize > lim.maxFileBytes)
            return makeError(ErrorCode::LimitExceeded, Format::Wbr, "payload larger than Limits::maxFileBytes", 12);
        const std::size_t avail = data.size() - kWbrHeaderSize;
        if (avail < storedSize)
            return makeError(ErrorCode::Truncated, Format::Wbr,
                             "payload is " + std::to_string(avail) + " bytes, header says " +
                                 std::to_string(storedSize),
                             data.size());
        if (avail > storedSize)
            return makeError(ErrorCode::Corrupt, Format::Wbr, "trailing bytes after payload", kWbrHeaderSize + storedSize);
        const CSpan payload = data.subspan(kWbrHeaderSize, storedSize);
        if (crc32(payload) != crc)
            return makeError(ErrorCode::ChecksumMismatch, Format::Wbr, "payload CRC-32 mismatch", 20);

        // pass 1: split into sections (bounds-checked), reject duplicates / unknown critical ids
        struct Sec {
            CSpan body;
            std::size_t offset = 0;
            bool present = false;
        };
        std::array<Sec, 6> secs{};
        const auto slot = [](std::uint8_t id) -> int {
            switch (id) {
                case kSecMeta: return 0;
                case kSecInputs: return 1;
                case kSecFrameFixes: return 2;
                case kSecDeaths: return 3;
                case kSecReplayExt: return 4;
                case kSecInputExt: return 5;
                default: return -1;
            }
        };
        Reader pr(payload, Format::Wbr, kWbrHeaderSize);
        while (!pr.atEnd()) {
            const std::uint8_t id = pr.u8();
            const std::uint64_t len = pr.varint(true);
            const std::size_t off = kWbrHeaderSize + pr.pos();
            const CSpan body = pr.bytes(len);
            if (pr.failed()) return pr.error();
            const int s = slot(id);
            if (s < 0) {
                if (id & 0x80) {
                    rep.info("wbr.skipped-section", "skipped unknown optional section 0x" + std::to_string(id));
                    continue;
                }
                return makeError(ErrorCode::UnsupportedFeature, Format::Wbr,
                                 "unknown critical section id " + std::to_string(id), off);
            }
            if (secs[static_cast<std::size_t>(s)].present)
                return makeError(ErrorCode::Corrupt, Format::Wbr, "duplicate section " + std::to_string(id), off);
            secs[static_cast<std::size_t>(s)] = {body, off, true};
        }
        if (!secs[0].present) return makeError(ErrorCode::Corrupt, Format::Wbr, "missing metadata section");

        Replay out;
        out.source.format = Format::Wbr;
        out.source.formatVersion = std::to_string(major) + "." + std::to_string(minor);
        const bool newerMinor = minor > kWbrMinor;

        const auto run = [&](int idx, auto&& fn) -> std::optional<Error> {
            Reader sr(secs[static_cast<std::size_t>(idx)].body, Format::Wbr, secs[static_cast<std::size_t>(idx)].offset);
            fn(sr);
            if (sr.failed()) return sr.error();
            if (!sr.atEnd() && !newerMinor)
                return makeError(ErrorCode::Corrupt, Format::Wbr, "trailing bytes inside section",
                                 secs[static_cast<std::size_t>(idx)].offset + sr.pos());
            return std::nullopt;
        };
        if (auto e = run(0, [&](Reader& r) { decodeMeta(r, out, lim); })) return *e;
        if (!opt.metadataOnly) {
            if (secs[1].present)
                if (auto e = run(1, [&](Reader& r) { decodeInputs(r, out, lim); })) return *e;
            if (secs[2].present)
                if (auto e = run(2, [&](Reader& r) { decodeFrameFixes(r, out, lim); })) return *e;
            if (secs[3].present)
                if (auto e = run(3, [&](Reader& r) { decodeDeaths(r, out, lim); })) return *e;
            if (secs[4].present)
                if (auto e = run(4, [&](Reader& r) { decodeReplayExt(r, out, lim); })) return *e;
            if (secs[5].present)
                if (auto e = run(5, [&](Reader& r) { decodeInputExt(r, out, lim); })) return *e;
        }
        return out;
    }
};

}  // namespace

// =============================================================================================
// 4b. GDR2 codec  (wire format taken from maxnut/GDReplayFormat branch gdr2, include/gdr/gdr.hpp)
// =============================================================================================
//
//  "GDR" | varint version(=2) | str inputTag | str author | str description | f32 BE duration(seconds)
//  | varint gameVersion | f64 BE framerate | varint seed | varint coins | varint ldm | varint platformer
//  | str botName | varint botVersion | varint levelId | str levelName | varint extSize | ext bytes
//  | varint deathCount | deathCount x varint delta | varint inputCount | varint p1InputCount
//  | p1 inputs then p2 inputs: varint packed [| varint extSize | ext bytes  if inputTag != ""]
//      packed = (frameDelta << 1) | down                       (non-platformer, button is implicitly 1)
//      packed = (frameDelta << 3) | (button << 1) | down       (platformer, button in 0..3)
//  str = varint length + bytes (NOT null-terminated). Negative ints are written sign-extended to 64 bits.
//  NOTE: the project README says "duration: varint" and "string: null terminated"; the code (which every GDR2
//  bot is built from) does what is described above, and so does this codec.
namespace {

constexpr std::string_view kGdrMagic{"GDR", 3};
constexpr std::string_view kWbxMagic{"WBX1", 4};
constexpr std::string_view kJsonInputTag{"WBJ1"};   // GDR2 input tag used for JSON-object input extensions

bool isDecimal(std::string_view s) noexcept {
    return !s.empty() && s.size() <= 18 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::int64_t decimalValue(std::string_view s) noexcept {
    std::int64_t v = 0;
    for (char c : s) v = v * 10 + (c - '0');
    return v;
}

/// Numeric stand-in for a bot version string ("v3.1.0-Neon" -> 3001000). Only a convenience for readers that
/// understand nothing but the integer; the exact string is carried in the WBX1 block.
std::int64_t packSemver(std::string_view v) {
    if (!v.empty() && v.front() == 'v') v.remove_prefix(1);
    const auto parts = splitByChar(v, '.');
    const auto part = [&](std::size_t i) -> std::int64_t {
        return i < parts.size() ? std::clamp(geodeNumFromStringOr0(parts[i]), 0, 999) : 0;
    };
    const std::int64_t major = std::max(0, geodeNumFromStringOr0(parts.empty() ? std::string_view{} : parts[0]));
    return major * 1'000'000 + part(1) * 1'000 + part(2);
}

std::int64_t gdr2BotVersionNumber(const Bot& b) {
    if (b.versionNumber) return *b.versionNumber;
    if (isDecimal(b.version)) return decimalValue(b.version);
    return packSemver(b.version);
}

bool botUsesExtension(const Replay& rp, const Gdr2Options& o) {
    return std::find(o.extensionBotNames.begin(), o.extensionBotNames.end(), rp.bot.name) != o.extensionBotNames.end();
}

// "WBX1" block: magic + entries (u8 id, varint len, bytes). ids >= 0x80 are optional.
//   1 frame fixes (same payload as the WBR section)   2 lastRecordedFrame (varint)   3 bot version string
//   4 JSON-object replay extension (MessagePack)       5 opaque inner extension (str tag + bytes)
std::optional<Error> buildWbx(const Replay& rp, Writer& w, bool needVersionString, bool needLrf) {
    w.bytes({reinterpret_cast<const std::uint8_t*>(kWbxMagic.data()), kWbxMagic.size()});
    const auto entry = [&](std::uint8_t id, const Writer& body) {
        w.u8(id);
        w.varint(body.size());
        w.bytes(body.data());
    };
    if (!rp.frameFixes.empty()) {
        Writer s;
        if (auto e = encodeFrameFixes(rp, s)) return e;
        entry(1, s);
    }
    if (needLrf) {
        Writer s;
        s.varint(rp.lastRecordedFrame);
        entry(2, s);
    }
    if (needVersionString) {
        Writer s;
        s.bytes(Bytes(rp.bot.version.begin(), rp.bot.version.end()));
        entry(3, s);
    }
    if (rp.replayExt.kind == ExtensionKind::JsonObject) {
        Writer s;
        s.bytes(rp.replayExt.data);
        entry(4, s);
    } else if (rp.replayExt.kind == ExtensionKind::GdrBinary) {
        Writer s;
        s.str(rp.replayExt.tag);
        s.bytes(rp.replayExt.data);
        entry(5, s);
    }
    return std::nullopt;
}

std::optional<Error> parseWbx(CSpan data, Replay& out, const Limits& lim, bool metadataOnly) {
    Reader r(data.subspan(kWbxMagic.size()), Format::Gdr2, kWbxMagic.size());
    bool seen[6] = {};
    while (!r.atEnd()) {
        const std::uint8_t id = r.u8();
        const std::uint64_t len = r.varint(true);
        Reader body = r.sub(len);
        if (r.failed()) return r.error();
        if (id >= 1 && id <= 5) {
            if (seen[id]) return makeError(ErrorCode::Corrupt, Format::Gdr2, "duplicate WBX1 entry");
            seen[id] = true;
        }
        switch (id) {
            case 1:
                if (!metadataOnly) decodeFrameFixes(body, out, lim);
                break;
            case 2: {
                const std::uint64_t v = body.varint(true);
                if (v > 0xFFFFFFFFu) return makeError(ErrorCode::InvalidValue, Format::Gdr2, "WBX1 lastRecordedFrame range");
                out.lastRecordedFrame = static_cast<std::uint32_t>(v);
                break;
            }
            case 3: {
                const CSpan s = body.bytes(body.remaining());
                if (s.size() > lim.maxStringBytes) return makeError(ErrorCode::LimitExceeded, Format::Gdr2, "WBX1 string");
                out.bot.version.assign(reinterpret_cast<const char*>(s.data()), s.size());
                break;
            }
            case 4:
            case 5: {
                if (metadataOnly) break;
                if (id == 5) out.replayExt.tag = body.str(lim.maxStringBytes, true);
                const CSpan s = body.bytes(body.remaining());
                if (s.size() > lim.maxExtensionBytes) return makeError(ErrorCode::LimitExceeded, Format::Gdr2, "WBX1 ext");
                out.replayExt.kind = id == 4 ? ExtensionKind::JsonObject : ExtensionKind::GdrBinary;
                out.replayExt.data.assign(s.begin(), s.end());
                break;
            }
            default:
                if (id < 0x80) return makeError(ErrorCode::UnsupportedFeature, Format::Gdr2, "unknown WBX1 entry");
                continue;
        }
        if (body.failed()) return body.error();
    }
    return std::nullopt;
}

std::optional<Error> narrowSigned32(std::uint64_t raw, const char* what, std::int32_t& dst, Reader& r) {
    // Accept the bit pattern of a 32-bit int (written zero-extended) or a sign-extended negative int.
    const std::int64_t s = static_cast<std::int64_t>(raw);
    if (raw <= 0xFFFFFFFFull) {
        dst = static_cast<std::int32_t>(static_cast<std::uint32_t>(raw));
        return std::nullopt;
    }
    if (s < 0 && s >= std::numeric_limits<std::int32_t>::min()) {
        dst = static_cast<std::int32_t>(s);
        return std::nullopt;
    }
    return makeError(ErrorCode::InvalidValue, Format::Gdr2, std::string(what) + " does not fit in 32 bits", r.pos());
}

class Gdr2Codec final : public ICodec {
public:
    Format format() const noexcept override { return Format::Gdr2; }
    std::string_view name() const noexcept override { return "GDR2"; }
    bool sniff(CSpan d) const noexcept override { return hasPrefix(d, kGdrMagic); }

    Detection inspect(CSpan d) const override {
        Detection det;
        if (!sniff(d)) return det;
        det.format = Format::Gdr2;
        det.confidence = Detection::Confidence::MagicOnly;
        Reader r(d.subspan(3), Format::Gdr2, 3);
        const std::uint64_t version = r.varint();
        if (r.failed()) {
            det.note = "GDR magic but version is truncated";
            return det;
        }
        if (version != 2) {
            det.note = "GDR magic with unsupported version " + std::to_string(version);
            return det;
        }
        // validate the fixed part of the header without allocating from file-supplied lengths
        (void)r.str(256);
        (void)r.str(1 << 20); (void)r.str(1 << 20);
        (void)r.f32be(); (void)r.varint(); (void)r.f64be(); (void)r.varint(); (void)r.varint(); (void)r.varint();
        (void)r.varint();
        (void)r.str(1 << 20); (void)r.varint(); (void)r.varint(); (void)r.str(1 << 20);
        const std::uint64_t ext = r.varint();
        if (!r.failed() && ext <= r.remaining()) {
            det.confidence = Detection::Confidence::Structure;
            det.note = "GDR2";
        } else {
            det.note = "GDR2 magic but header is invalid or truncated";
        }
        return det;
    }

    Result<Bytes> encode(const Replay& rp, const ExportOptions& opt, Report& rep) const override;
    Result<Replay> decode(CSpan data, const ImportOptions& opt, Report& rep) const override;
};

Result<Bytes> Gdr2Codec::encode(const Replay& rp, const ExportOptions& opt, Report& rep) const {
    if (auto e = rp.validate()) {
        e->format = Format::Gdr2;
        return *e;
    }
    std::vector<Diagnostic> losses;
    const auto loss = [&](const char* code, std::string msg) { losses.push_back({Severity::Warning, code, std::move(msg)}); };

    // ---- inputs: split per player, stable by frame --------------------------------------------------
    std::vector<const Input*> g[2];
    for (const Input& x : rp.inputs) g[x.player2 ? 1 : 0].push_back(&x);
    bool reordered = false;
    for (auto& grp : g) {
        if (!std::is_sorted(grp.begin(), grp.end(), [](const Input* a, const Input* b) { return a->frame < b->frame; })) {
            std::stable_sort(grp.begin(), grp.end(), [](const Input* a, const Input* b) { return a->frame < b->frame; });
            reordered = true;
        }
    }
    if (reordered) rep.info("gdr2.inputs-reordered", "inputs were not in frame order per player; stable-sorted");

    bool anyButtonNot1 = false, anyButtonOver3 = false;
    for (const Input& x : rp.inputs) {
        anyButtonNot1 |= x.button != 1;
        anyButtonOver3 |= x.button > 3;
    }
    if (!rp.platformer && anyButtonNot1)
        loss("gdr2.button-dropped", "non-platformer GDR2 stores no button id; inputs with button != 1 lose it");
    if (rp.platformer && anyButtonOver3)
        loss("gdr2.button-truncated", "platformer GDR2 holds button ids 0..3 only; larger ids are masked");

    // ---- input extension tag --------------------------------------------------------------------
    std::string inputTag;
    switch (rp.inputExt.kind) {
        case ExtensionKind::None: break;
        case ExtensionKind::GdrBinary: inputTag = rp.inputExt.tag; break;
        case ExtensionKind::JsonObject: inputTag = std::string(kJsonInputTag); break;
    }
    if (rp.inputExt.kind != ExtensionKind::None && inputTag.empty())
        return makeError(ErrorCode::InvalidValue, Format::Gdr2, "input extensions need a non-empty tag in GDR2");

    // ---- replay extension / White Bot extension block --------------------------------------------
    const bool extBot = botUsesExtension(rp, opt.gdr2);
    const bool versionIsDecimal = isDecimal(rp.bot.version);
    const bool needVersionString = !versionIsDecimal && !(rp.bot.version.empty());
    const bool needLrf = rp.lastRecordedFrame != 0;
    const bool wbData = !rp.frameFixes.empty() || needVersionString || needLrf ||
                        rp.replayExt.kind == ExtensionKind::JsonObject;
    Bytes extBytes;
    if (wbData) {
        if (opt.gdr2.emitWhiteBotExtension && extBot) {
            Writer w;
            if (auto e = buildWbx(rp, w, needVersionString, needLrf)) return *e;
            extBytes = w.release();
        } else {
            if (!rp.frameFixes.empty())
                loss("gdr2.framefix-dropped", "GDR2 has no frame-fix field and the White Bot extension block is " +
                                                  std::string(extBot ? "disabled" : "not allowed for bot '" + rp.bot.name + "'"));
            if (needVersionString)
                loss("gdr2.bot-version-string", "GDR2 stores only an integer bot version; string '" + rp.bot.version + "' is lost");
            if (needLrf) loss("gdr2.last-recorded-frame", "lastRecordedFrame has no GDR2 field");
            if (rp.replayExt.kind == ExtensionKind::JsonObject)
                loss("gdr2.json-extension-dropped", "JSON replay extension has no GDR2 representation for this bot");
            if (rp.replayExt.kind == ExtensionKind::GdrBinary) extBytes = rp.replayExt.data;
        }
    } else if (rp.replayExt.kind == ExtensionKind::GdrBinary) {
        extBytes = rp.replayExt.data;
    }

    if (!losses.empty()) {
        if (opt.lossPolicy == LossPolicy::Error)
            return makeError(ErrorCode::LossyConversion, Format::Gdr2,
                             losses.front().message + (losses.size() > 1 ? " (+" + std::to_string(losses.size() - 1) + " more)" : ""));
        for (auto& d : losses) rep.items.push_back(std::move(d));
    }

    // ---- write ----------------------------------------------------------------------------------
    Writer w;
    w.reserve(128 + rp.inputs.size() * 2);
    w.bytes({reinterpret_cast<const std::uint8_t*>(kGdrMagic.data()), kGdrMagic.size()});
    w.varint(2);
    w.str(inputTag);
    w.str(rp.author);
    w.str(rp.description);
    w.f32be(rp.duration);
    w.varint(static_cast<std::uint64_t>(static_cast<std::int64_t>(rp.gameVersion)));   // sign-extended like the reference
    w.f64be(rp.framerate);
    w.varint(static_cast<std::uint64_t>(static_cast<std::int64_t>(rp.seed)));
    w.varint(static_cast<std::uint64_t>(static_cast<std::int64_t>(rp.coins)));
    w.varint(rp.ldm ? 1 : 0);
    w.varint(rp.platformer ? 1 : 0);
    w.str(rp.bot.name);
    w.varint(static_cast<std::uint64_t>(gdr2BotVersionNumber(rp.bot)));
    w.varint(rp.level.id);
    w.str(rp.level.name);
    w.varint(extBytes.size());
    w.bytes(extBytes);

    w.varint(rp.deaths.size());
    std::uint64_t prev = 0;
    {
        // The reference stores death deltas as uint64 differences; unsorted deaths wrap and round-trip anyway.
        for (std::uint64_t d : rp.deaths) {
            w.varint(d - prev);
            prev = d;
        }
    }
    w.varint(rp.inputs.size());
    w.varint(g[0].size());
    for (int p = 0; p < 2; ++p) {
        prev = 0;
        for (const Input* x : g[p]) {
            const std::uint64_t delta = x->frame - prev;
            const unsigned shift = rp.platformer ? 3 : 1;
            if (delta > (std::numeric_limits<std::uint64_t>::max() >> shift))
                return makeError(ErrorCode::LimitExceeded, Format::Gdr2, "frame delta too large to pack");
            std::uint64_t packed = (delta << shift) | (x->down ? 1u : 0u);
            if (rp.platformer) packed |= static_cast<std::uint64_t>(x->button & 3u) << 1;
            w.varint(packed);
            if (!inputTag.empty()) {
                const CSpan e = rp.inputExtension(*x);
                w.varint(e.size());
                w.bytes(e);
            }
            prev = x->frame;
        }
    }
    return w.release();
}

Result<Replay> Gdr2Codec::decode(CSpan data, const ImportOptions& opt, Report& rep) const {
    const Limits& lim = opt.limits;
    if (data.empty()) return makeError(ErrorCode::Empty, Format::Gdr2, "empty input");
    if (!sniff(data)) {
        if (data.size() < 3 && std::memcmp(data.data(), kGdrMagic.data(), data.size()) == 0)
            return makeError(ErrorCode::Truncated, Format::Gdr2, "file shorter than the GDR magic");
        return makeError(ErrorCode::UnknownFormat, Format::Gdr2, "not a GDR2 file (bad magic)", 0);
    }
    if (data.size() > lim.maxFileBytes)
        return makeError(ErrorCode::LimitExceeded, Format::Gdr2, "file larger than Limits::maxFileBytes");

    Reader r(data.subspan(3), Format::Gdr2, 3);
    const std::uint64_t version = r.varint();
    if (r.failed()) return r.error();
    if (version > 2)
        return makeError(ErrorCode::NewerVersion, Format::Gdr2, "GDR format version " + std::to_string(version) + " is newer than 2", 3);
    if (version < 2)
        return makeError(ErrorCode::UnsupportedVersion, Format::Gdr2, "GDR format version " + std::to_string(version) + " is not GDR2", 3);

    Replay out;
    out.source.format = Format::Gdr2;
    out.source.formatVersion = "2";
    const std::string inputTag = r.str(lim.maxStringBytes);
    out.author = r.str(lim.maxStringBytes);
    out.description = r.str(lim.maxStringBytes);
    out.duration = r.f32be();
    const std::uint64_t gv = r.varint();
    out.framerate = r.f64be();
    const std::uint64_t seed = r.varint(), coins = r.varint(), ldm = r.varint(), plat = r.varint();
    out.bot.name = r.str(lim.maxStringBytes);
    const std::uint64_t botVersion = r.varint(), levelId = r.varint();
    out.level.name = r.str(lim.maxStringBytes);
    const std::uint64_t extSize = r.varint();
    if (r.failed()) return r.error();
    if (auto e = narrowSigned32(gv, "gameVersion", out.gameVersion, r)) return *e;
    if (auto e = narrowSigned32(seed, "seed", out.seed, r)) return *e;
    if (auto e = narrowSigned32(coins, "coins", out.coins, r)) return *e;
    if (levelId > 0xFFFFFFFFull) return makeError(ErrorCode::InvalidValue, Format::Gdr2, "level id does not fit in 32 bits");
    out.level.id = static_cast<std::uint32_t>(levelId);
    out.ldm = ldm != 0;
    out.platformer = plat != 0;
    out.bot.versionNumber = static_cast<std::int64_t>(botVersion);
    out.bot.version = std::to_string(static_cast<std::int64_t>(botVersion));

    if (extSize > lim.maxExtensionBytes)
        return makeError(ErrorCode::LimitExceeded, Format::Gdr2, "extension block exceeds Limits::maxExtensionBytes", r.pos());
    const CSpan ext = r.bytes(extSize);
    if (r.failed()) return r.error();
    if (!ext.empty()) {
        const bool wbx = hasPrefix(ext, kWbxMagic) &&
                         std::find(opt.gdr2ExtensionBotNames.begin(), opt.gdr2ExtensionBotNames.end(),
                                   out.bot.name) != opt.gdr2ExtensionBotNames.end();
        bool handled = false;
        if (wbx) {
            Replay tmp = out;
            if (auto e = parseWbx(ext, tmp, lim, opt.metadataOnly)) {
                rep.warn("gdr2.wbx-invalid", "White Bot extension block is invalid and was kept opaque: " + e->message);
            } else {
                out = std::move(tmp);
                handled = true;
            }
        }
        if (!handled && !opt.metadataOnly) {
            out.replayExt.kind = ExtensionKind::GdrBinary;
            out.replayExt.data.assign(ext.begin(), ext.end());
        }
    }

    const std::uint64_t deathCount = r.varint();
    if (r.failed()) return r.error();
    if (deathCount > lim.maxDeaths) return makeError(ErrorCode::LimitExceeded, Format::Gdr2, "death count exceeds limit", r.pos());
    if (deathCount > r.remaining()) return makeError(ErrorCode::Truncated, Format::Gdr2, "death count larger than remaining data", r.pos());
    std::vector<std::uint64_t> deaths;
    deaths.reserve(static_cast<std::size_t>(deathCount));
    std::uint64_t acc = 0;
    for (std::uint64_t i = 0; i < deathCount && !r.failed(); ++i) {
        acc += r.varint();   // wraps like the writer does; sorted files never wrap
        deaths.push_back(acc);
    }
    const std::uint64_t inputCount = r.varint();
    const std::uint64_t p1Count = r.varint();
    if (r.failed()) return r.error();
    if (!opt.metadataOnly) out.deaths = std::move(deaths);

    if (inputCount > lim.maxInputs) return makeError(ErrorCode::LimitExceeded, Format::Gdr2, "input count exceeds limit", r.pos());
    if (p1Count > inputCount) return makeError(ErrorCode::Corrupt, Format::Gdr2, "p1 input count larger than input count", r.pos());
    if (inputCount > r.remaining()) return makeError(ErrorCode::Truncated, Format::Gdr2, "input count larger than remaining data", r.pos());
    if (opt.metadataOnly) return out;

    const bool hasInputExt = !inputTag.empty();
    out.inputs.reserve(static_cast<std::size_t>(inputCount));
    if (hasInputExt) {
        out.inputExt.kind = inputTag == kJsonInputTag ? ExtensionKind::JsonObject : ExtensionKind::GdrBinary;
        out.inputExt.tag = inputTag;
    }
    std::uint64_t base = 0, extTotal = 0;
    for (std::uint64_t i = 0; i < inputCount && !r.failed(); ++i) {
        if (i == p1Count) base = 0;
        const std::uint64_t packed = r.varint();
        Input x;
        x.player2 = i >= p1Count;
        x.down = (packed & 1) != 0;
        const std::uint64_t delta = out.platformer ? packed >> 3 : packed >> 1;
        x.button = out.platformer ? static_cast<std::uint8_t>((packed >> 1) & 3u) : 1;
        if (delta > std::numeric_limits<std::uint64_t>::max() - base) {
            r.fail(ErrorCode::Corrupt, "frame number overflows 64 bits");
            break;
        }
        base += delta;
        x.frame = base;
        if (hasInputExt) {
            const std::uint64_t len = r.varint();
            if (r.failed()) break;
            if (len > r.remaining()) {
                r.fail(ErrorCode::Truncated, "input extension runs past end of data");
                break;
            }
            if (len > std::numeric_limits<std::uint32_t>::max() ||
                len > lim.maxExtensionBytes - std::min<std::uint64_t>(extTotal, lim.maxExtensionBytes)) {
                r.fail(ErrorCode::LimitExceeded, "input extensions exceed Limits::maxExtensionBytes");
                break;
            }
            const CSpan b = r.bytes(len);
            extTotal += len;
            if (len > 0) {
                out.inputExt.spans.push_back({out.inputExt.arena.size(), static_cast<std::uint32_t>(len)});
                out.inputExt.arena.insert(out.inputExt.arena.end(), b.begin(), b.end());
                x.extIndex = static_cast<std::uint32_t>(out.inputExt.spans.size());
            }
        }
        out.inputs.push_back(x);
    }
    if (r.failed()) return r.error();
    if (!r.atEnd())
        rep.warn("gdr2.trailing-bytes", std::to_string(r.remaining()) + " unused bytes after the last input were ignored");
    out.sortInputs();   // p1 block then p2 block -> chronological, p1 first on ties (same as the reference)
    return out;
}

}  // namespace
// =============================================================================================
// 4c. GDR1 codec (MessagePack + JSON): the schema of White Bot's src/gdr/gdr.hpp
// =============================================================================================
//
// Top level keys: gameVersion(float, 2.2081) description version(float, 1.0) duration(float s) author seed coins
// ldm framerate lastRecordedFrame, bot{name,version(string)}, level{id,name},
// inputs[{frame, btn, 2p, down, ...extension keys}], frameFixes[...]
// Frame fixes are read in the four historical encodings (legacy "player1": "n,x,y" strings, "player1X/Y", and
// "p1"/"p2" objects {x,y,r}) and always written as "p1"/"p2".
//
// Additive keys written by this library (legacy readers ignore unknown keys): "platformer", "deaths", and
// "rotate":false inside a p1/p2 frame-fix object. Unknown keys are preserved in Replay::replayExt / inputExt
// (as a MessagePack-encoded object) and written back.
namespace {

struct Gdr1Fail {
    Error error;
};

[[noreturn]] void fail1(Format f, ErrorCode c, std::string msg) { throw Gdr1Fail{makeError(c, f, std::move(msg))}; }

class J {
public:
    J(Format f, const json& o, std::string where) : f_(f), o_(&o), where_(std::move(where)) {}

    [[nodiscard]] bool has(const char* k) const { return o_->is_object() && o_->contains(k) && !(*o_)[k].is_null(); }
    [[nodiscard]] bool hasRaw(const char* k) const { return o_->is_object() && o_->contains(k); }
    [[nodiscard]] const json& at(const char* k) const { return (*o_).at(k); }

    double number(const char* k) const {
        const json& v = at(k);
        if (!v.is_number()) fail1(f_, ErrorCode::InvalidValue, where_ + "." + k + " is not a number");
        return v.get<double>();
    }
    std::int64_t integer(const char* k, std::int64_t lo, std::int64_t hi) const {
        const json& v = at(k);
        if (!v.is_number()) fail1(f_, ErrorCode::InvalidValue, where_ + "." + k + " is not a number");
        std::int64_t i;
        if (v.is_number_unsigned()) {
            const auto u = v.get<std::uint64_t>();
            if (u > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                fail1(f_, ErrorCode::InvalidValue, where_ + "." + k + " out of range");
            i = static_cast<std::int64_t>(u);
        } else if (v.is_number_integer()) {
            i = v.get<std::int64_t>();
        } else {
            const double d = v.get<double>();   // legacy get<int>() truncates floating point
            if (!(d > -9.2e18 && d < 9.2e18)) fail1(f_, ErrorCode::InvalidValue, where_ + "." + k + " out of range");
            i = static_cast<std::int64_t>(d);
        }
        if (i < lo || i > hi) fail1(f_, ErrorCode::InvalidValue, where_ + "." + k + " out of range");
        return i;
    }
    bool boolean(const char* k) const {
        const json& v = at(k);
        if (!v.is_boolean()) fail1(f_, ErrorCode::InvalidValue, where_ + "." + k + " is not a boolean");
        return v.get<bool>();
    }
    std::string string(const char* k, std::size_t max, bool allowNumber = false) const {
        const json& v = at(k);
        if (allowNumber && v.is_number()) return v.dump();
        if (!v.is_string()) fail1(f_, ErrorCode::InvalidValue, where_ + "." + k + " is not a string");
        const auto& s = v.get_ref<const std::string&>();
        if (s.size() > max) fail1(f_, ErrorCode::LimitExceeded, where_ + "." + k + " exceeds string limit");
        return s;
    }

private:
    Format f_;
    const json* o_;
    std::string where_;
};

Bytes toMsgPack(const json& j) { return json::to_msgpack(j); }

void parseSimpleFrameString(Format f, const json& v, Vec2& out) {
    if (!v.is_string()) fail1(f, ErrorCode::InvalidValue, "frame fix position string expected");
    // literal port of dataFromString(): "a,x,y" -> item 1 is x, item 2 is y, unparsable -> 0
    const std::string& s = v.get_ref<const std::string&>();
    std::size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        if (start > s.size()) break;
        std::size_t e = s.find(',', start);
        const std::string item = s.substr(start, e == std::string::npos ? std::string::npos : e - start);
        if (i >= 1) {
            float val = 0.f;
            try {
                std::size_t used = 0;
                val = std::stof(item, &used);
            } catch (...) {
                val = 0.f;
            }
            (i == 1 ? out.x : out.y) = val;
        }
        if (e == std::string::npos) break;
        start = e + 1;
    }
}

constexpr std::string_view kKnownTop[] = {"gameVersion", "description", "version", "duration", "bot",       "level",
                                          "author",      "seed",        "coins",   "ldm",      "framerate", "lastRecordedFrame",
                                          "inputs",      "frameFixes",  "platformer", "deaths"};
constexpr std::string_view kKnownInput[] = {"frame", "btn", "2p", "down"};

bool contains(const std::string_view* b, const std::string_view* e, const std::string& k) {
    return std::find(b, e, k) != e;
}

Replay decodeGdr1Root(const json& root, Format fmt, const ImportOptions& opt, Report& rep) {
    const Limits& lim = opt.limits;
    if (!root.is_object()) fail1(fmt, ErrorCode::InvalidValue, "top-level value is not an object");
    const J top(fmt, root, "$");
    Replay out;
    out.source.format = fmt;
    out.source.formatVersion = "1";

    if (top.has("gameVersion")) {
        const double v = top.number("gameVersion");
        if (v == 0) out.gameVersion = 0;
        else if (v > 0 && v < 100) out.gameVersion = static_cast<std::int32_t>(std::llround(v * 10000.0));
        else if (v >= 100 && v <= 1.0e7) {
            out.gameVersion = static_cast<std::int32_t>(std::llround(v));
            rep.warn("gdr1.game-version-scaled", "gameVersion " + std::to_string(v) + " looks already scaled (x10000); kept as is");
        } else fail1(fmt, ErrorCode::InvalidValue, "$.gameVersion out of range");
    }
    if (top.has("description")) out.description = top.string("description", lim.maxStringBytes);
    if (top.has("author")) out.author = top.string("author", lim.maxStringBytes);
    if (top.has("version")) {
        out.gdr1FormatVersion = top.number("version");
        out.source.formatVersion = top.at("version").dump();
    }
    if (top.has("duration")) out.duration = static_cast<float>(top.number("duration"));
    if (top.has("framerate")) out.framerate = top.number("framerate");
    if (top.has("seed")) out.seed = static_cast<std::int32_t>(top.integer("seed", INT32_MIN, UINT32_MAX) );
    if (top.has("coins")) out.coins = static_cast<std::int32_t>(top.integer("coins", INT32_MIN, INT32_MAX));
    if (top.has("ldm")) out.ldm = top.boolean("ldm");
    if (top.has("lastRecordedFrame")) out.lastRecordedFrame = static_cast<std::uint32_t>(top.integer("lastRecordedFrame", 0, UINT32_MAX));
    if (top.has("platformer") && top.at("platformer").is_boolean()) out.platformer = top.boolean("platformer");

    if (top.has("bot")) {
        if (!top.at("bot").is_object()) fail1(fmt, ErrorCode::InvalidValue, "$.bot is not an object");
        const J b(fmt, top.at("bot"), "$.bot");
        if (b.has("name")) out.bot.name = b.string("name", lim.maxStringBytes);
        if (b.has("version")) out.bot.version = b.string("version", lim.maxStringBytes, true);
    }
    if (top.has("level")) {
        if (!top.at("level").is_object()) fail1(fmt, ErrorCode::InvalidValue, "$.level is not an object");
        const J l(fmt, top.at("level"), "$.level");
        if (l.has("id")) out.level.id = static_cast<std::uint32_t>(l.integer("id", 0, UINT32_MAX));
        if (l.has("name")) out.level.name = l.string("name", lim.maxStringBytes);
    }

    if (opt.metadataOnly) return out;

    const int offset = opt.applyLegacyDllBotFrameOffset ? legacyDllBotFrameOffset(out.bot) : 0;
    if (offset != 0) rep.info("gdr1.dll-bot-offset", "applied +1 frame offset (Dll Bot " + out.bot.version + ")");

    // ---- unknown top-level keys -> replay extension ------------------------------------------------
    {
        json extra = json::object();
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (contains(std::begin(kKnownTop), std::end(kKnownTop), it.key())) {
                // "platformer"/"deaths" with an unexpected type are not ours: keep them as extension
                if (it.key() == "platformer" && !it.value().is_boolean()) extra[it.key()] = it.value();
                else if (it.key() == "deaths" && !it.value().is_array()) extra[it.key()] = it.value();
                continue;
            }
            extra[it.key()] = it.value();
        }
        if (!extra.empty()) {
            out.replayExt.kind = ExtensionKind::JsonObject;
            out.replayExt.data = toMsgPack(extra);
        }
    }

    // ---- deaths (additive key) ---------------------------------------------------------------------
    if (top.has("deaths") && top.at("deaths").is_array()) {
        const json& d = top.at("deaths");
        if (d.size() > lim.maxDeaths) fail1(fmt, ErrorCode::LimitExceeded, "$.deaths exceeds limit");
        for (const json& v : d) {
            if (!v.is_number_unsigned() && !(v.is_number_integer() && v.get<std::int64_t>() >= 0))
                fail1(fmt, ErrorCode::InvalidValue, "$.deaths entry is not a non-negative integer");
            out.deaths.push_back(v.get<std::uint64_t>());
        }
    }

    // ---- inputs ------------------------------------------------------------------------------------
    if (top.has("inputs")) {
        const json& arr = top.at("inputs");
        if (!arr.is_array()) fail1(fmt, ErrorCode::InvalidValue, "$.inputs is not an array");
        if (arr.size() > lim.maxInputs) fail1(fmt, ErrorCode::LimitExceeded, "$.inputs exceeds Limits::maxInputs");
        out.inputs.reserve(arr.size());
        std::size_t skipped = 0, idx = 0, extTotal = 0;
        for (const json& ij : arr) {
            const std::string where = "$.inputs[" + std::to_string(idx++) + "]";
            if (!ij.is_object()) fail1(fmt, ErrorCode::InvalidValue, where + " is not an object");
            const J in(fmt, ij, where);
            if (!in.has("frame")) {   // legacy importer: `continue`
                ++skipped;
                continue;
            }
            Input x;
            const std::int64_t frame = in.integer("frame", INT32_MIN, INT32_MAX) + offset;
            if (frame < 0) fail1(fmt, ErrorCode::InvalidValue, where + ".frame is negative");
            x.frame = static_cast<std::uint64_t>(frame);
            if (!in.hasRaw("btn") || !in.hasRaw("2p") || !in.hasRaw("down"))
                fail1(fmt, ErrorCode::InvalidValue, where + " is missing btn / 2p / down");
            x.button = static_cast<std::uint8_t>(in.integer("btn", 0, 255));
            x.player2 = in.boolean("2p");
            x.down = in.boolean("down");
            if (ij.size() > 4 || !(ij.contains("frame") && ij.contains("btn") && ij.contains("2p") && ij.contains("down") && ij.size() == 4)) {
                json extra = json::object();
                for (auto it = ij.begin(); it != ij.end(); ++it)
                    if (!contains(std::begin(kKnownInput), std::end(kKnownInput), it.key())) extra[it.key()] = it.value();
                if (!extra.empty()) {
                    const Bytes mp = toMsgPack(extra);
                    extTotal += mp.size();
                    if (extTotal > lim.maxExtensionBytes) fail1(fmt, ErrorCode::LimitExceeded, "input extensions exceed limit");
                    out.setInputExtension(x, ExtensionKind::JsonObject, "", mp);
                }
            }
            out.inputs.push_back(x);
        }
        if (skipped) rep.warn("gdr1.input-skipped", std::to_string(skipped) + " input(s) without a frame were skipped (legacy behavior)");
    }

    // ---- frame fixes -------------------------------------------------------------------------------
    if (top.has("frameFixes")) {
        const json& arr = top.at("frameFixes");
        if (!arr.is_array()) fail1(fmt, ErrorCode::InvalidValue, "$.frameFixes is not an array");
        if (arr.size() > lim.maxFrameFixes) fail1(fmt, ErrorCode::LimitExceeded, "$.frameFixes exceeds limit");
        const bool rotAllowed = opt.rotation == RotationPolicy::PreserveAll || legacyRotationAllowed(out.bot);
        out.frameFixes.reserve(arr.size());
        std::size_t skipped = 0, rotDropped = 0, idx = 0;
        for (const json& fj : arr) {
            const std::string where = "$.frameFixes[" + std::to_string(idx++) + "]";
            if (!fj.is_object()) fail1(fmt, ErrorCode::InvalidValue, where + " is not an object");
            const J f(fmt, fj, where);
            if (!f.has("frame")) {
                ++skipped;
                continue;
            }
            FrameFix fix;
            const std::int64_t frame = f.integer("frame", INT32_MIN, INT32_MAX) + offset;
            if (frame < 0) fail1(fmt, ErrorCode::InvalidValue, where + ".frame is negative");
            fix.frame = static_cast<std::uint64_t>(frame);

            const auto readPlayerObj = [&](const char* key, PlayerState& ps) {
                const json& po = fj.at(key);
                if (!po.is_object()) return;   // legacy: "p1": null -> nothing to read
                const J p(fmt, po, where + "." + key);
                if (po.contains("x")) ps.pos.x = static_cast<float>(p.number("x"));
                if (po.contains("y")) ps.pos.y = static_cast<float>(p.number("y"));
                if (po.contains("r")) {
                    if (rotAllowed) ps.rotation = static_cast<float>(p.number("r"));
                    else ++rotDropped;
                }
                if (po.contains("rotate") && po["rotate"].is_boolean()) ps.rotate = po["rotate"].get<bool>();
            };

            if (fj.contains("player1")) {
                parseSimpleFrameString(fmt, fj["player1"], fix.p1.pos);
                fix.p1.rotate = false;
                if (fj.contains("player2")) {
                    parseSimpleFrameString(fmt, fj["player2"], fix.p2.pos);
                    fix.p2.rotate = false;
                }
            } else if (fj.contains("player1X")) {
                if (!fj.contains("player1Y")) fail1(fmt, ErrorCode::InvalidValue, where + " has player1X without player1Y");
                fix.p1.pos = {static_cast<float>(f.number("player1X")), static_cast<float>(f.number("player1Y"))};
                fix.p1.rotate = false;
                if (fj.contains("player2X") && fj.contains("player2Y")) {
                    fix.p2.pos = {static_cast<float>(f.number("player2X")), static_cast<float>(f.number("player2Y"))};
                    fix.p2.rotate = false;
                }
            } else if (fj.contains("p1")) {
                readPlayerObj("p1", fix.p1);
                if (fj.contains("p2")) readPlayerObj("p2", fix.p2);
            } else {
                ++skipped;
                continue;
            }
            out.frameFixes.push_back(fix);
        }
        if (skipped) rep.warn("gdr1.framefix-skipped", std::to_string(skipped) + " frame fix(es) in no known encoding were skipped (legacy behavior)");
        if (rotDropped)
            rep.warn("gdr1.rotation-dropped",
                     std::to_string(rotDropped) + " frame-fix rotation value(s) were ignored (RotationPolicy::LegacyWhiteBot, bot '" +
                         out.bot.name + " " + out.bot.version + "'); use RotationPolicy::PreserveAll to keep them");
    }
    return out;
}

std::size_t jsonDepth(CSpan d) noexcept {
    std::size_t depth = 0, maxDepth = 0;
    bool inStr = false, esc = false;
    for (std::uint8_t c : d) {
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '{' || c == '[') maxDepth = std::max(maxDepth, ++depth);
        else if ((c == '}' || c == ']') && depth > 0) --depth;
    }
    return maxDepth;
}

class Gdr1Codec final : public ICodec {
public:
    explicit Gdr1Codec(Format f) : f_(f) {}
    Format format() const noexcept override { return f_; }
    std::string_view name() const noexcept override { return f_ == Format::Gdr1Json ? "GDR1 JSON" : "GDR1 MessagePack"; }

    bool sniff(CSpan d) const noexcept override {
        if (f_ == Format::Gdr1MsgPack) {
            if (d.empty()) return false;
            const std::uint8_t b = d[0];
            return (b >= 0x80 && b <= 0x8F) || b == 0xDE || b == 0xDF;   // MessagePack map
        }
        std::size_t i = 0;
        if (d.size() >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) i = 3;
        while (i < d.size() && (d[i] == ' ' || d[i] == '\t' || d[i] == '\r' || d[i] == '\n')) ++i;
        return i < d.size() && d[i] == '{';
    }

    Result<Replay> decode(CSpan data, const ImportOptions& opt, Report& rep) const override {
        if (data.empty()) return makeError(ErrorCode::Empty, f_, "empty input");
        if (!sniff(data)) return makeError(ErrorCode::UnknownFormat, f_, "not a GDR1 file (first byte is neither '{' nor a MessagePack map)", 0);
        if (data.size() > opt.limits.maxFileBytes) return makeError(ErrorCode::LimitExceeded, f_, "file larger than Limits::maxFileBytes");
        try {
            json root;
            if (f_ == Format::Gdr1Json) {
                if (jsonDepth(data) > opt.limits.maxJsonDepth)
                    return makeError(ErrorCode::LimitExceeded, f_, "JSON nesting deeper than Limits::maxJsonDepth");
                root = json::parse(data.begin(), data.end(), nullptr, false);
                if (root.is_discarded()) return makeError(ErrorCode::Corrupt, f_, "invalid JSON text");
            } else {
                root = json::from_msgpack(data.begin(), data.end(), true, false);
                if (root.is_discarded()) return makeError(ErrorCode::Corrupt, f_, "invalid or truncated MessagePack");
            }
            return decodeGdr1Root(root, f_, opt, rep);
        } catch (const Gdr1Fail& e) {
            return e.error;
        } catch (const json::exception& e) {
            return makeError(ErrorCode::InvalidValue, f_, std::string("JSON error: ") + e.what());
        } catch (const std::bad_alloc&) {
            return makeError(ErrorCode::LimitExceeded, f_, "out of memory while decoding");
        }
    }

    Result<Bytes> encode(const Replay& rp, const ExportOptions& opt, Report& rep) const override {
        if (auto e = rp.validate()) {
            e->format = f_;
            return *e;
        }
        std::vector<Diagnostic> losses;
        const auto loss = [&](const char* code, std::string msg) { losses.push_back({Severity::Warning, code, std::move(msg)}); };
        const bool additive = opt.gdr1.writeAdditiveKeys;

        if (rp.replayExt.kind == ExtensionKind::GdrBinary)
            loss("gdr1.replay-extension-dropped", "binary (GDR2-style) replay extension has no GDR1 representation");
        if (rp.inputExt.kind == ExtensionKind::GdrBinary && std::any_of(rp.inputs.begin(), rp.inputs.end(), [](const Input& i) { return i.extIndex != 0; }))
            loss("gdr1.input-extension-dropped", "binary input extension '" + rp.inputExt.tag + "' has no GDR1 representation");
        if (rp.platformer && !additive) loss("gdr1.platformer-flag", "GDR1 has no platformer flag");
        if (!rp.deaths.empty() && !additive) loss("gdr1.deaths", "GDR1 has no deaths field");
        const bool anyNoRotate = std::any_of(rp.frameFixes.begin(), rp.frameFixes.end(), [](const FrameFix& x) { return !x.p1.rotate || !x.p2.rotate; });
        if (anyNoRotate && !additive) loss("gdr1.rotate-flag", "per-player 'rotate=false' has no GDR1 representation without additive keys");
        if (!losses.empty()) {
            if (opt.lossPolicy == LossPolicy::Error)
                return makeError(ErrorCode::LossyConversion, f_, losses.front().message + (losses.size() > 1 ? " (+" + std::to_string(losses.size() - 1) + " more)" : ""));
            for (auto& d : losses) rep.items.push_back(std::move(d));
        }

        try {
            const int offset = opt.applyLegacyDllBotFrameOffset ? legacyDllBotFrameOffset(rp.bot) : 0;
            const auto outFrame = [&](std::uint64_t f, const char* what) -> std::int64_t {
                const std::int64_t v = static_cast<std::int64_t>(std::min<std::uint64_t>(f, std::uint64_t{1} << 40)) - offset;
                if (v < 0 || v > INT32_MAX)
                    fail1(f_, ErrorCode::InvalidValue, std::string(what) + " frame " + std::to_string(f) + " cannot be written to GDR1 (offset " + std::to_string(offset) + ")");
                return v;
            };

            json j = json::object();
            if (rp.replayExt.kind == ExtensionKind::JsonObject) {
                j = json::from_msgpack(rp.replayExt.data, true, false);
                if (j.is_discarded() || !j.is_object()) fail1(f_, ErrorCode::InvalidValue, "replay JSON extension is not a MessagePack object");
            }
            j["gameVersion"] = static_cast<float>(rp.gameVersion) / 10000.f;
            j["description"] = rp.description;
            j["version"] = static_cast<float>(rp.gdr1FormatVersion);
            j["duration"] = rp.duration;
            j["bot"]["name"] = rp.bot.name;
            j["bot"]["version"] = rp.bot.version.empty() && rp.bot.versionNumber ? std::to_string(*rp.bot.versionNumber) : rp.bot.version;
            j["level"]["id"] = rp.level.id;
            j["level"]["name"] = rp.level.name;
            j["author"] = rp.author;
            j["seed"] = rp.seed;
            j["coins"] = rp.coins;
            j["ldm"] = rp.ldm;
            if (static_cast<double>(static_cast<float>(rp.framerate)) == rp.framerate) j["framerate"] = static_cast<float>(rp.framerate);
            else j["framerate"] = rp.framerate;
            if (rp.lastRecordedFrame > 0) j["lastRecordedFrame"] = rp.lastRecordedFrame;
            if (additive && rp.platformer) j["platformer"] = true;
            if (additive && !rp.deaths.empty()) j["deaths"] = rp.deaths;

            if (!rp.inputs.empty()) {
                json arr = json::array();
                for (const Input& x : rp.inputs) {
                    json ij = json::object();
                    const CSpan e = rp.inputExtension(x);
                    if (!e.empty() && rp.inputExt.kind == ExtensionKind::JsonObject) {
                        ij = json::from_msgpack(e, true, false);
                        if (ij.is_discarded() || !ij.is_object()) fail1(f_, ErrorCode::InvalidValue, "input JSON extension is not a MessagePack object");
                    }
                    ij["frame"] = static_cast<std::uint32_t>(outFrame(x.frame, "input"));
                    ij["btn"] = static_cast<int>(x.button);
                    ij["2p"] = x.player2;
                    ij["down"] = x.down;
                    arr.push_back(std::move(ij));
                }
                j["inputs"] = std::move(arr);
            }

            if (!rp.frameFixes.empty()) {
                json arr = json::array();
                std::size_t skipped = 0;
                const auto bitsNZ = [](float v) { return floatBits(v) != 0; };
                for (const FrameFix& ff : rp.frameFixes) {
                    json p1, p2;
                    if (bitsNZ(ff.p1.pos.x)) p1["x"] = ff.p1.pos.x;
                    if (bitsNZ(ff.p1.pos.y)) p1["y"] = ff.p1.pos.y;
                    if (bitsNZ(ff.p1.rotation)) p1["r"] = ff.p1.rotation;
                    if (bitsNZ(ff.p2.pos.x)) p2["x"] = ff.p2.pos.x;
                    if (bitsNZ(ff.p2.pos.y)) p2["y"] = ff.p2.pos.y;
                    if (bitsNZ(ff.p2.rotation)) p2["r"] = ff.p2.rotation;
                    if (p1.is_null() && p2.is_null() && !opt.gdr1.keepAllZeroFrameFixes) {
                        ++skipped;
                        continue;
                    }
                    if (additive && !ff.p1.rotate) p1["rotate"] = false;
                    if (additive && !ff.p2.rotate) p2["rotate"] = false;
                    json fj = json::object();
                    fj["frame"] = static_cast<int>(outFrame(ff.frame, "frame fix"));
                    fj["p1"] = p1;   // null when empty, exactly like the legacy writer
                    if (!p2.is_null()) fj["p2"] = p2;
                    arr.push_back(std::move(fj));
                }
                if (skipped)
                    rep.warn("gdr1.empty-framefix-skipped", std::to_string(skipped) + " all-zero frame fix(es) not written (legacy behavior; Gdr1Options::keepAllZeroFrameFixes)");
                if (!arr.empty()) j["frameFixes"] = std::move(arr);
            }

            if (f_ == Format::Gdr1MsgPack) return json::to_msgpack(j);
            const std::string s = j.dump(-1, ' ', false, json::error_handler_t::strict);
            return Bytes(s.begin(), s.end());
        } catch (const Gdr1Fail& e) {
            return e.error;
        } catch (const json::exception& e) {
            return makeError(ErrorCode::InvalidValue, f_, std::string("cannot serialize: ") + e.what());
        }
    }

private:
    Format f_;
};

}  // namespace
// =============================================================================================
// 5. registry, detection, public API, file I/O
// =============================================================================================
namespace {

class Registry {
public:
    Registry() {
        codecs_ = {std::make_shared<WbrCodec>(), std::make_shared<Gdr2Codec>(), std::make_shared<Gdr1Codec>(Format::Gdr1Json),
                   std::make_shared<Gdr1Codec>(Format::Gdr1MsgPack)};
        builtin_ = codecs_.size();
    }
    void add(std::shared_ptr<const ICodec> c) {
        std::lock_guard lock(m_);
        for (auto& e : codecs_)
            if (e->format() == c->format()) {
                e = std::move(c);
                return;
            }
        codecs_.push_back(std::move(c));
    }
    std::vector<std::shared_ptr<const ICodec>> snapshot() const {
        std::lock_guard lock(m_);
        return codecs_;
    }
    std::shared_ptr<const ICodec> find(Format f) const {
        std::lock_guard lock(m_);
        for (const auto& c : codecs_)
            if (c->format() == f) return c;
        return nullptr;
    }

private:
    mutable std::mutex m_;
    std::vector<std::shared_ptr<const ICodec>> codecs_;
    std::size_t builtin_ = 0;
};

Registry& registry() {
    static Registry r;
    return r;
}

}  // namespace

void registerCodec(std::shared_ptr<const ICodec> codec) {
    if (codec) registry().add(std::move(codec));
}

std::shared_ptr<const ICodec> findCodec(Format format) { return registry().find(format); }

Detection detectFormat(CSpan data) {
    Detection none;
    if (data.empty()) {
        none.note = "empty input";
        return none;
    }
    for (const auto& c : registry().snapshot())
        if (c->sniff(data)) return c->inspect(data);
    none.note = "no registered codec recognizes these bytes";
    return none;
}

Result<Replay> importReplay(CSpan data, const ImportOptions& options) {
    Report local;
    Report& rep = options.report ? *options.report : local;
    if (data.empty()) return makeError(ErrorCode::Empty, options.format, "empty input");
    if (data.size() > options.limits.maxFileBytes)
        return makeError(ErrorCode::LimitExceeded, options.format, "input larger than Limits::maxFileBytes");

    std::shared_ptr<const ICodec> codec;
    if (options.format == Format::Auto) {
        for (const auto& c : registry().snapshot())
            if (c->sniff(data)) {
                codec = c;
                break;
            }
        if (!codec) return makeError(ErrorCode::UnknownFormat, Format::Unknown, "no codec recognizes this data (empty, truncated or foreign file)", 0);
    } else if (options.format == Format::Unknown) {
        return makeError(ErrorCode::InvalidValue, Format::Unknown, "ImportOptions::format must be Auto or a concrete format");
    } else {
        codec = findCodec(options.format);
        if (!codec)
            return makeError(options.format == Format::Hlr ? ErrorCode::NotImplemented : ErrorCode::Internal, options.format,
                             options.format == Format::Hlr ? "no HLR codec registered (no HLR specification is available yet)"
                                                          : "codec not registered");
    }
    return codec->decode(data, options, rep);
}

Result<std::vector<std::uint8_t>> exportReplay(const Replay& replay, const ExportOptions& options) {
    Report local;
    Report& rep = options.report ? *options.report : local;
    if (options.format == Format::Auto || options.format == Format::Unknown)
        return makeError(ErrorCode::InvalidValue, options.format, "ExportOptions::format must be a concrete format");
    const auto codec = findCodec(options.format);
    if (!codec)
        return makeError(options.format == Format::Hlr ? ErrorCode::NotImplemented : ErrorCode::Internal, options.format,
                         options.format == Format::Hlr ? "no HLR codec registered (no HLR specification is available yet)"
                                                      : "codec not registered");
    return codec->encode(replay, options, rep);
}

Result<std::vector<std::uint8_t>> convertReplay(CSpan data, const ExportOptions& to, const ImportOptions& from) {
    auto r = importReplay(data, from);
    if (!r) return r.error();
    return exportReplay(r.value(), to);
}

Result<Replay> importReplayFile(const std::filesystem::path& path, const ImportOptions& options) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return makeError(ErrorCode::Io, options.format, "cannot stat '" + path.string() + "': " + ec.message());
    if (size == 0) return makeError(ErrorCode::Empty, options.format, "file is empty");
    if (size > options.limits.maxFileBytes)
        return makeError(ErrorCode::LimitExceeded, options.format, "file larger than Limits::maxFileBytes");
    std::ifstream f(path, std::ios::binary);
    if (!f) return makeError(ErrorCode::Io, options.format, "cannot open '" + path.string() + "'");
    Bytes buf(static_cast<std::size_t>(size));
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    if (static_cast<std::size_t>(f.gcount()) != buf.size())
        return makeError(ErrorCode::Io, options.format, "short read from '" + path.string() + "'");
    return importReplay(buf, options);
}

Result<Unit> exportReplayFile(const Replay& replay, const std::filesystem::path& path, const ExportOptions& options) {
    auto bytes = exportReplay(replay, options);
    if (!bytes) return bytes.error();
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return makeError(ErrorCode::Io, options.format, "cannot create '" + tmp.string() + "'");
        f.write(reinterpret_cast<const char*>(bytes.value().data()), static_cast<std::streamsize>(bytes.value().size()));
        f.flush();
        if (!f) return makeError(ErrorCode::Io, options.format, "write failed for '" + tmp.string() + "'");
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return makeError(ErrorCode::Io, options.format, "cannot move temp file to '" + path.string() + "'");
    }
    return Unit{};
}

}  // namespace gdr
