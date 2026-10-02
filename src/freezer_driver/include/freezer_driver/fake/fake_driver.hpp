// Copyright 2026 Giovanni Remigi
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the Giovanni Remigi nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <freezer_driver/driver.hpp>

namespace freezer_driver
{
/**
 * @brief A Freezer controller in software, to run and test the node without
 * the board.
 *
 * It validates a sequence with the rules of the firmware, runs it on a clock
 * it is given, and records when each pattern would have been latched. Tests
 * drive the clock by hand, so a shot of a second takes no time to test, and
 * can make it fail in the ways that are hard to provoke on the real board.
 */
class FakeDriver : public Driver
{
public:
  using Clock = std::function<std::chrono::microseconds()>;

  /** The ways the fake controller can be told to fail. */
  enum class Failure
  {
    None,
    NeverFinish,      // The shot never ends, as if the controller hung.
    ResetDuringShot,  // The controller resets at the first status query of a shot.
  };

  /** A pattern and when it was latched. */
  struct Latch
  {
    std::chrono::microseconds time;
    uint16_t outputs;

    bool operator==(const Latch&) const = default;
  };

  /** The limits of the firmware. */
  static constexpr SequenceLimits kLimits{ 16, 40, 10'000'000 };

  /**
   * @param clock The time, by default the steady clock of the computer.
   */
  explicit FakeDriver(Clock clock = steady_clock);

  bool connect() override;
  void disconnect() override;
  InfoResponse get_info() override;
  LoadSequenceResponse load_sequence(const Sequence& sequence) override;
  ShootResponse shoot(uint16_t checksum) override;
  StatusResponse get_status() override;

  void set_failure(Failure failure);

  /** The lateness the next shots report, in µs. */
  void set_lateness(uint32_t lateness_us);

  /** Every pattern latched since the fake was created. */
  const std::vector<Latch>& timeline() const;

private:
  static std::chrono::microseconds steady_clock();

  /** End the running shot if its time is over. */
  void update();

  Clock clock_;
  Failure failure_ = Failure::None;
  uint32_t lateness_us_ = 0;

  std::optional<Sequence> loaded_;
  bool running_ = false;
  std::chrono::microseconds start_{ 0 };
  uint16_t next_shot_id_ = 1;
  uint16_t last_shot_id_ = 0;
  uint32_t worst_lateness_us_ = 0;
  std::vector<Latch> timeline_;
};
}  // namespace freezer_driver
