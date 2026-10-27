#ifndef _SLC_V3_REPLAY_HPP
#define _SLC_V3_REPLAY_HPP

#include "atom.hpp"
#include "builtin.hpp"
#include "error.hpp"
#include "metadata.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused"
#include "section.hpp"
#pragma GCC diagnostic pop

#include "util.hpp"

#include <array>
#include <cassert>
#include <expected>
#include <iostream>

SLC_NS_BEGIN

namespace v3 {

using DefaultRegistry = AtomRegistry<NullAtom, ActionAtom>;

template <typename Registry = DefaultRegistry> class Replay {
private:
  using Self = Replay;

public:
  Metadata m_meta;
  Registry m_atoms;

private:
public:
  static constexpr uint64_t HEADER_SIZE = 8;
  static constexpr std::array<uint8_t, HEADER_SIZE> HEADER = {
      'S', 'L', 'C', '3', 'R', 'P', 'L', 'Y'};

  static constexpr uint8_t FOOTER = 0xCC;

  static constexpr uint16_t META_SIZE = sizeof(Metadata);

  static Result<Self> read(std::istream &in) {
    std::array<uint8_t, HEADER_SIZE> headerBuf;
    in.read(reinterpret_cast<char *>(headerBuf.data()), HEADER_SIZE);

    if (HEADER != headerBuf) {
      return std::unexpected("invalid header in given container");
    }

    Self replay;

    uint16_t metaSize = util::binRead<uint16_t>(in);
    if (META_SIZE != metaSize) {
      return std::unexpected(
          "invalid metadata size, likely outdated or malformed replay");
    }

    // this is assuming metadata is actually correct in the replay
    // checksums will validate metadata but it could still be garbage
    // a way to prevent it would be to checksum the metadata itself
    // but that kinda sucks
    replay.m_meta = util::binRead<Metadata>(in);

    TRY(replay.m_atoms.readAll(in));

    uint8_t footerBuf = util::binRead<uint8_t>(in);
    if (FOOTER != footerBuf) {
      return std::unexpected("invalid footer in given container");
    }

    return replay;
  }

  Result<> write(std::ostream &out) {
    out.write(reinterpret_cast<const char *>(HEADER.data()), HEADER_SIZE);

    util::binWrite(out, META_SIZE);
    util::binWrite(out, m_meta);

    m_atoms.writeAll(out);

    util::binWrite(out, FOOTER);

    return {};
  }
};

} // namespace v3

SLC_NS_END
#endif
