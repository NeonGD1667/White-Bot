// gdr.hpp - Unified Geometry Dash replay library.
//
// One replay model (gdr::Replay) + one codec per on-disk format:
//
//   Format::Gdr1MsgPack  GDR1 schema, MessagePack encoding   (".gdr"      - White Bot / DLL Bot legacy)
//   Format::Gdr1Json     GDR1 schema, JSON text encoding     (".gdr.json")
//   Format::Gdr2         GDR2 binary ("GDR" + varint 2)      (".gdr2"     - maxnut/GDReplayFormat, branch gdr2)
//   Format::Wbr          WBR 1.x, new White Bot format       (".wbr")
//   Format::Hlr          reserved: plug-in point only, NOT implemented (no specification available)
//
// The model never depends on how a replay is serialized, and it has no dependency on
// Geode/cocos2d (positions are gdr::Vec2). nlohmann::json is used only inside gdr.cpp.
//
// Requirements: C++20 (std::span, defaulted comparison). The White Bot project builds with C++23.
// Thread-safety: all free functions are re-entrant; registerCodec() is internally synchronized.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gdr {

inline constexpr int kLibraryVersionMajor = 1;
inline constexpr int kLibraryVersionMinor = 0;

// ---------------------------------------------------------------------------------------------
// Formats
// ---------------------------------------------------------------------------------------------

enum class Format : std::uint8_t {
    Unknown = 0,   // detection result only
    Auto,          // import only: detect from content (never from file extension)
    Gdr1MsgPack,
    Gdr1Json,
    Gdr2,
    Wbr,
    Hlr,           // reserved, see ICodec / registerCodec
};

[[nodiscard]] std::string_view formatName(Format f) noexcept;
/// Conventional file extension including the dot (".gdr", ".gdr.json", ".gdr2", ".wbr"). Informational only.
[[nodiscard]] std::string_view formatExtension(Format f) noexcept;

// ---------------------------------------------------------------------------------------------
// Errors / results / diagnostics
// ---------------------------------------------------------------------------------------------

enum class ErrorCode : std::uint8_t {
    // "the file is damaged"
    Empty,              // zero bytes
    Truncated,          // ended before the structure was complete
    Corrupt,            // structurally invalid (bad counts, bad varint, bad section, trailing junk where forbidden)
    ChecksumMismatch,   // WBR payload CRC32 does not match
    InvalidValue,       // well-formed but semantically invalid (negative frame, button out of range, bad UTF-8...)
    // "the file is intact but this build cannot read it"
    UnknownFormat,      // nothing recognizes these bytes
    UnsupportedVersion, // known format, version older/different than any supported version
    NewerVersion,       // known format, written by a newer (incompatible) version
    UnsupportedFeature, // WBR: a required feature bit / critical section this build does not know
    NotImplemented,     // e.g. Format::Hlr without a registered codec
    // "this operation was refused"
    LimitExceeded,      // a configured safety limit (Limits) was exceeded
    LossyConversion,    // target format cannot hold some data and LossPolicy::Error is active
    // environment
    Io,
    Internal,
};

[[nodiscard]] std::string_view errorCodeName(ErrorCode c) noexcept;

struct Error {
    ErrorCode code = ErrorCode::Internal;
    Format format = Format::Unknown;       // codec that produced the error, if any
    std::string message;
    std::optional<std::size_t> offset;     // byte offset in the input when known

    [[nodiscard]] std::string toString() const;
};

struct Unit {};

template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : v_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : v_(std::in_place_index<1>, std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return v_.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    // Precondition: ok(). (std::get throws std::bad_variant_access otherwise.)
    [[nodiscard]] T& value() & { return std::get<0>(v_); }
    [[nodiscard]] const T& value() const& { return std::get<0>(v_); }
    [[nodiscard]] T&& value() && { return std::get<0>(std::move(v_)); }

    // Precondition: !ok().
    [[nodiscard]] const Error& error() const& { return std::get<1>(v_); }

    [[nodiscard]] T valueOr(T fallback) const& { return ok() ? std::get<0>(v_) : std::move(fallback); }

private:
    std::variant<T, Error> v_;
};

enum class Severity : std::uint8_t { Info, Warning };

struct Diagnostic {
    Severity severity = Severity::Warning;
    std::string code;      // stable machine-readable id, e.g. "gdr1.rotation-dropped"
    std::string message;
};

/// Collects everything that was converted, skipped or dropped. Nothing is dropped without an entry here
/// (or an Error).
struct Report {
    std::vector<Diagnostic> items;

    void warn(std::string code, std::string message);
    void info(std::string code, std::string message);
    [[nodiscard]] bool hasWarnings() const noexcept;
    [[nodiscard]] bool has(std::string_view code) const noexcept;
};

// ---------------------------------------------------------------------------------------------
// Replay model
// ---------------------------------------------------------------------------------------------

struct Vec2 {
    float x = 0.f;
    float y = 0.f;
    friend bool operator==(const Vec2&, const Vec2&) = default;
};

/// Corrective state of one player at one frame (White Bot "FrameFix").
struct PlayerState {
    Vec2 pos{};
    float rotation = 0.f;
    /// false = "do not apply rotation for this fix" (set by the legacy string/XY frame-fix encodings).
    bool rotate = true;
    friend bool operator==(const PlayerState&, const PlayerState&) = default;
};

struct FrameFix {
    std::uint64_t frame = 0;
    PlayerState p1{};
    PlayerState p2{};   // all-zero position/rotation means "no player 2 data" (White Bot convention)
    friend bool operator==(const FrameFix&, const FrameFix&) = default;
};

/// The bot that recorded the replay. This is NOT the file-format version (see Replay::source).
struct Bot {
    std::string name;
    /// Free-form version string as written by the bot ("v3.1.0-Neon", "2.3.5", "3").
    std::string version;
    /// Numeric bot version for formats that only store an integer (GDR2). Empty = derive when needed.
    std::optional<std::int64_t> versionNumber;
};

struct Level {
    std::uint32_t id = 0;
    std::string name;
};

enum class ExtensionKind : std::uint8_t {
    None = 0,
    GdrBinary,   // opaque bytes of a GDR2-style extension block / per-input extension
    JsonObject,  // a JSON object stored as MessagePack bytes (GDR1 "extra keys")
};

/// Replay-level extension. Preserved verbatim when the target format can hold it.
struct ExtensionBlob {
    ExtensionKind kind = ExtensionKind::None;
    std::string tag;                 // informational / GDR2 input tag
    std::vector<std::uint8_t> data;
    [[nodiscard]] bool empty() const noexcept { return kind == ExtensionKind::None; }
};

/// Per-input extensions: all inputs share one kind/tag (as in GDR2); data lives in one arena.
struct InputExtensions {
    struct Span {
        std::uint64_t offset = 0;
        std::uint32_t size = 0;
    };
    ExtensionKind kind = ExtensionKind::None;
    std::string tag;
    std::vector<std::uint8_t> arena;
    std::vector<Span> spans;        // Input::extIndex == i + 1 refers to spans[i]
    [[nodiscard]] bool empty() const noexcept { return kind == ExtensionKind::None && spans.empty(); }
};

struct Input {
    std::uint64_t frame = 0;
    std::uint8_t button = 1;        // 1 = jump, 2 = left, 3 = right (platformer). Non-platformer replays use 1.
    bool player2 = false;
    bool down = false;              // true = press, false = release
    std::uint32_t extIndex = 0;     // 0 = no extension, otherwise 1-based index into Replay::inputExt.spans

    friend bool operator==(const Input& a, const Input& b) noexcept {
        return a.frame == b.frame && a.button == b.button && a.player2 == b.player2 && a.down == b.down &&
               a.extIndex == b.extIndex;
    }
};

struct Replay {
    // --- identity -------------------------------------------------------------------------
    std::string author;
    std::string description;
    Bot bot;
    Level level;

    // --- game / recording settings ------------------------------------------------------------
    /// Geometry Dash build as an integer: 22081 == 2.2081 (GDR2 convention). GDR1 stores a float; the GDR1 codec
    /// converts with round(x * 10000).
    std::int32_t gameVersion = 0;
    float duration = 0.f;           // seconds
    double framerate = 240.0;       // ticks per second
    std::int32_t seed = 0;          // bit pattern of the 32-bit attempt seed
    std::int32_t coins = 0;
    bool ldm = false;
    bool platformer = false;        // GDR2/WBR/GDR1 (additive key) only

    // --- recording data -------------------------------------------------------------------
    std::vector<Input> inputs;                 // chronological; ties keep recording order
    std::vector<FrameFix> frameFixes;          // GDR1 + WBR (+ GDR2 via the White Bot extension block)
    std::vector<std::uint64_t> deaths;         // GDR2 + WBR (+ GDR1 additive key)
    std::uint32_t lastRecordedFrame = 0;       // White Bot extension; 0 = unset

    // --- extensions -----------------------------------------------------------------------
    ExtensionBlob replayExt;
    InputExtensions inputExt;

    // --- provenance (informational; NOT the bot version) -----------------------------------------
    struct Source {
        Format format = Format::Unknown;
        /// File-format version as found in the file: GDR1 "version" (float, usually 1.0), GDR2 version varint,
        /// WBR "major.minor".
        std::string formatVersion;
    } source;
    /// Value written to the GDR1 "version" key. Kept so GDR1 -> GDR1 round-trips preserve it.
    double gdr1FormatVersion = 1.0;

    // --- helpers --------------------------------------------------------------------------
    /// Stable sort by frame. Does not touch extension indices.
    void sortInputs();
    /// Span of the extension bytes attached to `in` (empty span if none).
    [[nodiscard]] std::span<const std::uint8_t> inputExtension(const Input& in) const noexcept;
    /// Attach extension bytes to an input; sets inputExt.kind/tag if still unset. Returns false when the kind/tag
    /// conflicts with extensions already present.
    bool setInputExtension(Input& in, ExtensionKind kind, std::string_view tag, std::span<const std::uint8_t> bytes);
    /// Cheap internal-consistency check (extension indices in range, kinds consistent).
    [[nodiscard]] std::optional<Error> validate() const;
};

// ---------------------------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------------------------

/// Safety limits applied by every parser BEFORE allocating. Counts are additionally bounded by the number of
/// bytes that remain in the input (every element needs at least one byte).
struct Limits {
    std::size_t maxFileBytes = std::size_t{1} << 30;        // 1 GiB
    std::size_t maxInputs = std::size_t{1} << 26;           // 67,108,864
    std::size_t maxFrameFixes = std::size_t{1} << 26;
    std::size_t maxDeaths = std::size_t{1} << 24;
    std::size_t maxStringBytes = std::size_t{1} << 20;      // 1 MiB per string
    std::size_t maxExtensionBytes = std::size_t{1} << 26;   // per replay-level blob and for all input blobs together
    std::size_t maxJsonDepth = 64;                          // GDR1 JSON text only
};

/// How GDR1 "p1"/"p2" frame-fix rotation is treated on import.
enum class RotationPolicy : std::uint8_t {
    /// Bug-compatible with the current White Bot importer: rotation ("r") is read only from DLL Bot macros whose
    /// version string has no "alpha."/"beta." (and from "Dll Bot v3.0.1"); every other macro gets rotation 0.
    /// Each dropped value is reported ("gdr1.rotation-dropped").
    LegacyWhiteBot,
    /// Always keep "r" when present.
    PreserveAll,
};

struct ImportOptions {
    Format format = Format::Auto;
    Limits limits{};
    /// GDR1 only. Reproduces the White Bot "DLL Bot frame offset": macros recorded by "Dll Bot" older than 2.3.6
    /// get +1 on every input/frame-fix frame. See legacyDllBotFrameOffset().
    bool applyLegacyDllBotFrameOffset = true;
    RotationPolicy rotation = RotationPolicy::LegacyWhiteBot;
    /// Parse metadata only (no inputs / frame fixes / deaths). Much faster for file lists.
    bool metadataOnly = false;
    /// GDR2: bot names whose replay-extension block may be a White Bot "WBX1" block (see Gdr2Options).
    std::vector<std::string> gdr2ExtensionBotNames = {"White Bot", "Dll Bot"};
    Report* report = nullptr;
};

enum class LossPolicy : std::uint8_t {
    /// Refuse (ErrorCode::LossyConversion) when the target cannot hold some data.
    Error,
    /// Convert anyway, add a Diagnostic for each thing that was dropped or truncated.
    ReportAndDrop,
};

struct Gdr1Options {
    /// The current White Bot writer silently skips frame fixes whose p1 and p2 are all-zero. Default keeps that.
    bool keepAllZeroFrameFixes = false;
    /// Write additive keys unknown to old readers (they ignore them): "platformer", "deaths", per-player "rotate".
    bool writeAdditiveKeys = true;
};

struct Gdr2Options {
    /// Store White Bot data that GDR2 has no field for (frame fixes, bot version string, last recorded frame,
    /// JSON extension) in the replay extension block, tagged "WBX1". Other readers skip the block by its size.
    bool emitWhiteBotExtension = true;
    /// Bot names for which the extension block is written/interpreted. Others keep their block opaque.
    std::vector<std::string> extensionBotNames = {"White Bot", "Dll Bot"};
};

enum class WbrCompression : std::uint8_t {
    None = 0,
    // 1.. reserved for future compression ids (decoders reject unknown ids with UnsupportedFeature)
};

struct WbrOptions {
    WbrCompression compression = WbrCompression::None;
};

struct ExportOptions {
    Format format = Format::Wbr;
    LossPolicy lossPolicy = LossPolicy::Error;
    /// GDR1 only. Undo the DLL Bot offset that the importer would add, so write->read is the identity.
    bool applyLegacyDllBotFrameOffset = true;
    Gdr1Options gdr1{};
    Gdr2Options gdr2{};
    WbrOptions wbr{};
    Report* report = nullptr;
};

// ---------------------------------------------------------------------------------------------
// Detection and I/O
// ---------------------------------------------------------------------------------------------

struct Detection {
    enum class Confidence : std::uint8_t {
        None,        // not recognized
        MagicOnly,   // magic/first bytes match, structure not validated (decode will report the exact problem)
        Structure,   // header structurally validated
    };
    Format format = Format::Unknown;
    Confidence confidence = Confidence::None;
    std::string note;   // why (or why not), e.g. "GDR magic with unsupported version 3"
};

/// Content-based detection. Never looks at a file name. GDR and GDR2 share no magic: GDR1 is JSON/MessagePack
/// (first byte '{' or a MessagePack map marker); GDR2 is "GDR" followed by version varint 2.
[[nodiscard]] Detection detectFormat(std::span<const std::uint8_t> data);

[[nodiscard]] Result<Replay> importReplay(std::span<const std::uint8_t> data, const ImportOptions& options = {});
[[nodiscard]] Result<Replay> importReplayFile(const std::filesystem::path& path, const ImportOptions& options = {});

[[nodiscard]] Result<std::vector<std::uint8_t>> exportReplay(const Replay& replay, const ExportOptions& options);
/// Writes atomically (temp file + rename).
[[nodiscard]] Result<Unit> exportReplayFile(const Replay& replay, const std::filesystem::path& path,
                                            const ExportOptions& options);

/// import + export. `from.format` may be Auto.
[[nodiscard]] Result<std::vector<std::uint8_t>> convertReplay(std::span<const std::uint8_t> data,
                                                              const ExportOptions& to,
                                                              const ImportOptions& from = {});

// ---------------------------------------------------------------------------------------------
// Compatibility helpers (exposed so White Bot code and tests can use the exact same rules)
// ---------------------------------------------------------------------------------------------

/// +1 for "Dll Bot" macros older than 2.3.6 (and for pre-release version strings the legacy parser could not split
/// into <= 3 numeric parts), otherwise 0. This is a literal port of Global/gdr.hpp importData() - see docs.
[[nodiscard]] int legacyDllBotFrameOffset(const Bot& bot) noexcept;

/// Literal port of the "rotation" flag in the White Bot importer (see RotationPolicy::LegacyWhiteBot).
[[nodiscard]] bool legacyRotationAllowed(const Bot& bot) noexcept;

// ---------------------------------------------------------------------------------------------
// Codec interface: add a new format (HLR) without touching Replay or the other codecs
// ---------------------------------------------------------------------------------------------

class ICodec {
public:
    virtual ~ICodec() = default;
    [[nodiscard]] virtual Format format() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    /// Cheap, allocation-free sniff of magic bytes / first bytes. Used for Format::Auto. A codec that returns true is
    /// authoritative: decode() is then called and its error is reported as-is; other codecs are never tried.
    [[nodiscard]] virtual bool sniff(std::span<const std::uint8_t> data) const noexcept = 0;
    /// Validates the header without decoding the body. Optional refinement of sniff() used by detectFormat().
    [[nodiscard]] virtual Detection inspect(std::span<const std::uint8_t> data) const;
    [[nodiscard]] virtual Result<Replay> decode(std::span<const std::uint8_t> data, const ImportOptions& options,
                                                Report& report) const = 0;
    [[nodiscard]] virtual Result<std::vector<std::uint8_t>> encode(const Replay& replay, const ExportOptions& options,
                                                                   Report& report) const = 0;
};

/// Registers (or replaces, by Format) a codec. Built-in codecs are registered on first use. External codecs are
/// consulted after the built-in ones for Format::Auto. HLR plugs in here:
///     gdr::registerCodec(std::make_shared<HlrCodec>());   // HlrCodec : gdr::ICodec, format() == Format::Hlr
void registerCodec(std::shared_ptr<const ICodec> codec);
[[nodiscard]] std::shared_ptr<const ICodec> findCodec(Format format);

}  // namespace gdr
