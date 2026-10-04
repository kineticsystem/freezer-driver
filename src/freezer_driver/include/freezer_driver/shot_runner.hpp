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
#include <string>

#include <freezer_driver/driver.hpp>
#include <freezer_driver/sequence.hpp>

namespace freezer_driver
{
/**
 * @brief Run one shot on a controller: load its sequence when it is not
 * already there, fire it, and poll the controller until the shot has ended.
 *
 * The controller is the authority on the end of a shot. The host knows its
 * duration, but only as a deadline: the clock of the controller, the USB
 * latency or a reset can each make the prediction wrong.
 *
 * The runner knows nothing of ROS. Time comes in as a clock and a sleep, so
 * that a test can run a shot on the clock of a FakeDriver, instantly and
 * exactly.
 */
class ShotRunner
{
public:
  using Clock = std::function<std::chrono::microseconds()>;
  using Sleep = std::function<void(std::chrono::microseconds)>;

  struct Config
  {
    // Time between two status queries.
    std::chrono::microseconds poll_period{ 10'000 };
    // Time after the duration of a shot before giving up on it.
    std::chrono::microseconds end_margin{ 100'000 };
  };

  /** What the runner tells its caller while a shot runs. */
  struct Callbacks
  {
    // Called right before Shoot is sent. Return false to cancel the shot:
    // nothing is fired. After it returns true, the shot can no longer be
    // canceled.
    std::function<bool()> start = [] { return true; };
    // Called once the controller has started the shot.
    std::function<void(uint16_t shot_id, uint32_t duration_us)> started = [](uint16_t, uint32_t) {};
    // Called at each status query while the shot runs.
    std::function<void(uint8_t step, uint32_t elapsed_us)> progress = [](uint8_t, uint32_t) {};
  };

  struct Result
  {
    enum class Outcome
    {
      Succeeded,
      Aborted,
      Canceled,
    };

    Outcome outcome = Outcome::Aborted;
    std::string message;  // Why the shot was aborted or canceled.
    uint16_t shot_id = 0;
    uint32_t duration_us = 0;
    uint32_t worst_lateness_us = 0;
  };

  /**
   * @param driver The controller, which must outlive the runner.
   * @param clock The time, used for the deadline of a shot.
   * @param sleep Wait between two status queries.
   */
  ShotRunner(Driver& driver, Config config, Clock clock, Sleep sleep);

  /**
   * Run a shot, from loading its sequence to its end. The sequence must be
   * valid for the controller.
   */
  Result run(const Sequence& sequence, const Callbacks& callbacks);

  /** Run a shot, with no callbacks. */
  Result run(const Sequence& sequence);

private:
  /**
   * Load the sequence into the controller, unless it is already there.
   * @throw std::runtime_error when the controller refuses it.
   */
  void load(const Sequence& sequence);

  Driver& driver_;
  Config config_;
  Clock clock_;
  Sleep sleep_;

  // The sequence the runner loaded last, which it believes is still in the
  // controller. Reset whenever the controller may have lost it, so the next
  // shot loads it again. The controller does not tell which table it holds:
  // only the runner may load one.
  std::optional<Sequence> loaded_;
};
}  // namespace freezer_driver
