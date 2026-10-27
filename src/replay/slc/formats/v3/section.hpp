#ifndef _SLC_V3_SECTION_HPP
#define _SLC_V3_SECTION_HPP

#include "formats/v3/action.hpp"
#include "formats/v3/error.hpp"
#include "util.hpp"

#include <cassert>
#include <span>
#include <vector>

#ifdef SLC_INSPECT
#include <print>
#endif

SLC_NS_BEGIN

namespace v3 {

class PlayerInput {
public:
  enum class Button : uint8_t {
    Swift = 0,
    Jump = 1,
    Left = 2,
    Right = 3,
  };

  uint64_t m_frame = 0;
  uint64_t m_delta = 0;
  Button m_button = Button::Jump;
  bool m_holding = false;
  bool m_player2 = false;
  bool m_difference = false;

  static PlayerInput fromAction(const Action& action) {
    assert(action.isPlayer());

    PlayerInput p;
    p.m_button = static_cast<Button>(action.m_type);

    if (action.swift()) {
      p.m_button = Button::Swift;
    }

    p.m_frame = action.m_frame;
    p.m_delta = action.delta();
    p.m_holding = action.m_holding;
    p.m_player2 = action.m_player2;

    return p;
  }

  static PlayerInput fromState(uint64_t prevFrame, uint64_t state) {
    PlayerInput p;

    p.m_delta = state >> 4;
    p.m_frame = prevFrame + p.m_delta;

    uint8_t button = (state >> 2) & 0b11;
    assert(button <= 3);

    p.m_button = static_cast<Button>(button);
    p.m_holding = (state & 0b1) == 0b1;
    p.m_player2 = (state & 0b10) == 0b10;

    return p;
  }

  uint64_t prepareState(uint8_t byteSize) const {
    uint64_t byteMask =
        byteSize == 8
            ? ~uint64_t{0}
            : (1ull << (byteSize * 8ull)) - 1ull;

    return byteMask &
           ((m_delta << 4) |
            (static_cast<uint8_t>(m_button) << 2) |
            (static_cast<uint64_t>(m_player2) << 1) |
            static_cast<uint64_t>(m_holding));
  }

  bool weakEq(const PlayerInput& other) const {
    return m_delta == other.m_delta &&
           m_holding == other.m_holding &&
           m_player2 == other.m_player2 &&
           m_button == other.m_button;
  }
};

class Section {
public:
  enum class Identifier : uint8_t {
    /**
     * 00 XX    XXXX       XXXXXXXX
     * -- --    ----       ---------
     * ID Size Count (2^X) Reserved
     */
    Input = 0,

    /**
     * 01 XX   XXXX   XXXXX   XXX
     * -- --   ----   -----   ---
     * ID Size Count  Repeats Reserved
     */
    Repeat = 1,

    /**
     * 10 XXXX XX
     * -- ---- --
     * ID Type Size
     */
    Special = 2,
  };

  enum class SpecialType : uint8_t {
    Restart = 0,
    RestartFull = 1,
    Death = 2,
    TPS = 3,
    Bugpoint = 4,
  };

private:
  // Player
  uint16_t m_countExp = 0;
  uint16_t m_repeatsExp = 0;

  // Special
  SpecialType m_specialType = SpecialType::Bugpoint;
  uint64_t m_seed = 0;
  double m_tps = 240.0;
  Action m_special;

public:
  Identifier m_id = Identifier::Input;
  uint16_t m_deltaSize = 0;

  std::vector<PlayerInput> m_playerInputs;

  bool m_markedForRemoval = false;

  uint64_t getInputCountDirty() const {
    return m_playerInputs.size();
  }

  uint64_t getRealDeltaSize() const {
    assert(m_deltaSize <= 3);
    return 1ull << static_cast<uint64_t>(m_deltaSize);
  }

  uint64_t getInputCount() const {
    return 1ull << static_cast<uint64_t>(m_countExp);
  }

  uint64_t getRepeatCount() const {
    return 1ull << static_cast<uint64_t>(m_repeatsExp);
  }

  bool isSpecial() const {
    return m_id == Identifier::Special;
  }

  void copyFrom(Section& other) {
    assert(!isSpecial());

    m_playerInputs.insert(
        m_playerInputs.end(),
        other.m_playerInputs.begin(),
        other.m_playerInputs.end()
    );
  }

  size_t totalSize() {
    return newSizeAssumingDeltaSize(
        getInputCount(),
        getRealDeltaSize()
    );
  }

  size_t newSizeAssumingDeltaSize(uint64_t count, uint64_t size) {
    switch (m_id) {
    case Identifier::Input:
      return count * size + 1;

    case Identifier::Repeat:
      return count * size * getRepeatCount() + 2;

    case Identifier::Special:
      return 1 + 8 + size;
    }

    return 0;
  }

  static Section player(
      std::span<const Action> actions,
      size_t start,
      size_t end
  ) {
    Section s;

    s.m_id = Identifier::Input;

    uint32_t count = 0;

    for (size_t i = start; i < end; i++) {
      const auto& action = actions[i];

      if (action.m_holding || !action.swift()) {
#ifdef SLC_INSPECT
        std::println(
            "Processing {} button {} delta {}, marked swift {}",
            i,
            static_cast<int>(action.m_type),
            action.delta(),
            action.swift()
        );
#endif

        s.m_playerInputs.push_back(
            PlayerInput::fromAction(action)
        );

        count++;
      }
    }

    s.m_countExp = util::exponentOfTwo(count);

#ifdef SLC_INSPECT
    std::println(
        "Preparing Input section from {} to {}, with count {}",
        start,
        end,
        count
    );
#endif

    return s;
  }

  static Section player(const Action& start) {
    assert(start.isPlayer());

    Section s;

    s.m_id = Identifier::Input;
    s.m_playerInputs = {
        PlayerInput::fromAction(start)
    };

    return s;
  }

  static Result<Section> special(const Action& action) {
    assert(!action.isPlayer());

    Section s;

    s.m_id = Identifier::Special;

    using A = Action::ActionType;

    switch (action.m_type) {
    case A::TPS: {
      if (action.m_tps <= 0.0) {
        return std::unexpected(
            "invalid TPS value for special section"
        );
      }

      s.m_tps = action.m_tps;
      s.m_specialType = SpecialType::TPS;
      break;
    }

    case A::Death:
    case A::Restart:
    case A::RestartFull: {
      s.m_seed = action.m_seed;

      s.m_specialType =
          static_cast<SpecialType>(
              static_cast<int>(action.m_type) - 4
          );

      break;
    }

    case A::Bugpoint: {
      s.m_specialType = SpecialType::Bugpoint;
      break;
    }

    default:
      return std::unexpected(
          "Invalid action to create a special section"
      );
    }

    s.m_special = action;
    s.m_deltaSize = action.getMinimumSize();

    return s;
  }

  static void distributeInputsToSections(
      std::vector<Section>& sections,
      std::vector<PlayerInput>& inputs,
      uint16_t deltaSize
  ) {
    size_t i = 0;

    while (i < inputs.size()) {
      uint64_t count =
          util::largestPowerOfTwo(inputs.size() - i);

      Section s;

      s.m_playerInputs = std::vector<PlayerInput>(
          inputs.begin() + i,
          inputs.begin() + i + count
      );

      s.m_countExp = util::exponentOfTwo(count);
      s.m_deltaSize = deltaSize;
      s.m_id = Identifier::Input;

      i += count;

      sections.push_back(std::move(s));
    }

    inputs.clear();
  }

  std::vector<Section> runLengthEncode() {
    assert(m_id == Identifier::Input);

    std::vector<Section> newSections;
    std::vector<PlayerInput> freeInputs;

    constexpr size_t MAX_CLUSTER_SIZE = 64;

    size_t idx = 0;
    const size_t N = m_playerInputs.size();

    while (idx < N) {
      bool foundAnyRepetitions = false;

      size_t bestCluster = 0;
      size_t bestClusterRepetitions = 0;
      int64_t bestClusterScore = 0;

      for (
          size_t cluster = 1;
          cluster <= MAX_CLUSTER_SIZE && cluster <= N;
          cluster <<= 1
      ) {
        if (idx + cluster >= N)
          break;

        size_t offset = 1;

        while (true) {
          const size_t start =
              idx + offset * cluster;

          const size_t end =
              idx + offset * (cluster + 1);

          if (
              end >= N ||
              (start + cluster) > N ||
              (idx + cluster) > N
          ) {
            break;
          }

          bool allEqual = true;

          for (size_t j = 0; j < cluster; j++) {
            if (
                !m_playerInputs[idx + j].weakEq(
                    m_playerInputs[start + j]
                )
            ) {
              allEqual = false;
              break;
            }
          }

          if (!allEqual)
            break;

          offset++;
        }

        offset--;

        if (offset <= 1)
          continue;

        offset = util::largestPowerOfTwo(offset);

        int64_t score =
            static_cast<int64_t>(cluster) *
            (static_cast<int64_t>(offset) - 1);

        if (score > bestClusterScore) {
          foundAnyRepetitions = true;
          bestClusterScore = score;
          bestCluster = cluster;
          bestClusterRepetitions = offset;
        }
      }

      if (foundAnyRepetitions) {
        distributeInputsToSections(
            newSections,
            freeInputs,
            m_deltaSize
        );

        Section repeat;

        repeat.m_deltaSize = m_deltaSize;
        repeat.m_repeatsExp =
            util::exponentOfTwo(bestClusterRepetitions);
        repeat.m_countExp =
            util::exponentOfTwo(bestCluster);

        repeat.m_playerInputs.insert(
            repeat.m_playerInputs.end(),
            m_playerInputs.begin() + idx,
            m_playerInputs.begin() +
                idx + bestCluster
        );

        repeat.m_id = Identifier::Repeat;

        newSections.push_back(std::move(repeat));

        idx +=
            bestCluster *
            bestClusterRepetitions;
      }
      else {
        freeInputs.push_back(
            std::move(m_playerInputs[idx])
        );

        idx++;
      }
    }

    distributeInputsToSections(
        newSections,
        freeInputs,
        m_deltaSize
    );

    return newSections;
  }

  static void read(
      std::istream& s,
      std::vector<Action>& actions
  ) {
    uint16_t initialHeader =
        util::binRead<uint16_t>(s);

    Identifier id =
        static_cast<Identifier>(
            initialHeader >> 14
        );

    switch (id) {
    case Identifier::Input: {
      uint16_t deltaSize =
          (initialHeader >> 12) & 0b11;

      uint16_t countExp =
          (initialHeader >> 8) & 0b1111;

      uint64_t byteSize =
          1ull << static_cast<uint64_t>(deltaSize);

      uint64_t length =
          1ull << static_cast<uint64_t>(countExp);

      for (uint64_t i = 0; i < length; i++) {
        uint64_t state = 0;

        s.read(
            reinterpret_cast<char*>(&state),
            byteSize
        );

        uint64_t previousFrame = 0;

        if (!actions.empty()) {
          previousFrame =
              actions.back().m_frame;
        }

        PlayerInput p =
            PlayerInput::fromState(
                previousFrame,
                state
            );

        if (
            p.m_button ==
            PlayerInput::Button::Swift
        ) {
          actions.push_back(
              Action(
                  previousFrame,
                  p.m_delta,
                  Action::ActionType::Jump,
                  true,
                  p.m_player2
              )
          );

          actions.back().m_swift = true;

          actions.push_back(
              Action(
                  p.m_frame,
                  0,
                  Action::ActionType::Jump,
                  false,
                  p.m_player2
              )
          );

          actions.back().m_swift = true;
        }
        else {
          actions.push_back(
              Action(
                  previousFrame,
                  p.m_delta,
                  static_cast<Action::ActionType>(
                      p.m_button
                  ),
                  p.m_holding,
                  p.m_player2
              )
          );
        }
      }

      break;
    }

    case Identifier::Repeat: {
      uint16_t deltaSize =
          (initialHeader >> 12) & 0b11;

      uint16_t countExp =
          (initialHeader >> 8) & 0b1111;

      uint16_t repeatsExp =
          (initialHeader >> 3) & 0b11111;

      uint64_t byteSize =
          1ull << static_cast<uint64_t>(deltaSize);

      uint64_t length =
          1ull << static_cast<uint64_t>(countExp);

      uint64_t repeats =
          1ull << static_cast<uint64_t>(repeatsExp);

      std::vector<PlayerInput> inputs;
      inputs.reserve(length);

      for (uint64_t i = 0; i < length; i++) {
        uint64_t state = 0;

        s.read(
            reinterpret_cast<char*>(&state),
            byteSize
        );

        uint64_t previousFrame = 0;

        if (!inputs.empty()) {
          previousFrame =
              inputs.back().m_frame;
        }

        PlayerInput p =
            PlayerInput::fromState(
                previousFrame,
                state
            );

        inputs.push_back(p);
      }

      for (uint64_t i = 0; i < repeats; i++) {
        for (auto& p : inputs) {
          uint64_t previousFrame = 0;

          if (!actions.empty()) {
            previousFrame =
                actions.back().m_frame;
          }

          if (
              p.m_button ==
              PlayerInput::Button::Swift
          ) {
            actions.push_back(
                Action(
                    previousFrame,
                    p.m_delta,
                    Action::ActionType::Jump,
                    true,
                    p.m_player2
                )
            );

            actions.back().m_swift = true;

            actions.push_back(
                Action(
                    previousFrame + p.m_delta,
                    0,
                    Action::ActionType::Jump,
                    false,
                    p.m_player2
                )
            );

            actions.back().m_swift = true;
          }
          else {
            actions.push_back(
                Action(
                    previousFrame,
                    p.m_delta,
                    static_cast<Action::ActionType>(
                        p.m_button
                    ),
                    p.m_holding,
                    p.m_player2
                )
            );
          }
        }
      }

      break;
    }

    case Identifier::Special: {
      uint16_t deltaSize =
          (initialHeader >> 8) & 0b11;

      SpecialType specialType =
          static_cast<SpecialType>(
              (initialHeader >> 10) & 0b1111
          );

      uint64_t frameDelta = 0;

      s.read(
          reinterpret_cast<char*>(&frameDelta),
          1ull << deltaSize
      );

      uint64_t currentFrame = 0;

      if (!actions.empty()) {
        currentFrame =
            actions.back().m_frame;
      }

      switch (specialType) {
      case SpecialType::TPS: {
        double tps =
            util::binRead<double>(s);

        actions.push_back(
            Action(
                currentFrame,
                frameDelta,
                tps
            )
        );

        break;
      }

      case SpecialType::Restart:
      case SpecialType::RestartFull:
      case SpecialType::Death: {
        uint64_t seed =
            util::binRead<uint64_t>(s);

        actions.push_back(
            Action(
                currentFrame,
                frameDelta,
                static_cast<Action::ActionType>(
                    static_cast<int>(specialType) + 4
                ),
                seed
            )
        );

        // FIX:
        // Prevent Death/Restart/RestartFull from
        // falling through into Bugpoint.
        break;
      }

      case SpecialType::Bugpoint: {
        actions.push_back(
            Action(
                currentFrame,
                frameDelta,
                Action::ActionType::Bugpoint
            )
        );

        break;
      }
      }

      break;
    }
    }
  }

  void write(std::ostream& s) const {
    if (m_markedForRemoval)
      return;

    switch (m_id) {
    case Identifier::Input: {
      uint16_t header =
          static_cast<uint16_t>(
              (m_countExp << 8) |
              (m_deltaSize << 12)
          );

      util::binWrite(s, header);

      uint64_t byteSize =
          getRealDeltaSize();

      for (const auto& input : m_playerInputs) {
        uint64_t state =
            input.prepareState(
                static_cast<uint8_t>(byteSize)
            );

        s.write(
            reinterpret_cast<const char*>(&state),
            byteSize
        );
      }

      break;
    }

    case Identifier::Repeat: {
      uint16_t header =
          static_cast<uint16_t>(
              (static_cast<uint16_t>(
                   Identifier::Repeat
               ) << 14) |
              (m_deltaSize << 12) |
              (m_countExp << 8) |
              (m_repeatsExp << 3)
          );

      util::binWrite(s, header);

      uint64_t byteSize =
          getRealDeltaSize();

      for (const auto& input : m_playerInputs) {
        uint64_t state =
            input.prepareState(
                static_cast<uint8_t>(byteSize)
            );

        s.write(
            reinterpret_cast<const char*>(&state),
            byteSize
        );
      }

      break;
    }

    case Identifier::Special: {
      uint16_t header =
          static_cast<uint16_t>(
              (static_cast<uint16_t>(
                   Identifier::Special
               ) << 14) |
              (static_cast<uint16_t>(
                   m_specialType
               ) << 10) |
              (m_deltaSize << 8)
          );

      util::binWrite(s, header);

      uint64_t delta =
          m_special.delta();

      s.write(
          reinterpret_cast<const char*>(&delta),
          getRealDeltaSize()
      );

      switch (m_specialType) {
      case SpecialType::Restart:
      case SpecialType::RestartFull:
      case SpecialType::Death:
        util::binWrite(s, m_seed);
        break;

      case SpecialType::TPS:
        util::binWrite(s, m_tps);
        break;

      case SpecialType::Bugpoint:
        break;
      }

      break;
    }
    }
  }
};

} // namespace v3

SLC_NS_END

#endif // _SLC_V3_SECTION_HPP