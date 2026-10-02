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

#include <cstdint>
#include <vector>

#include <freezer_driver/sequence.hpp>

/**
 * The recipes turn what a photographer thinks in, cameras, flashes and delays,
 * into the table of output patterns the controller replays.
 *
 * Each jack of the Freezer board, OUT1 to OUT8, drives two optocouplers: for
 * jack k, bit 2(k-1) of the outputs closes the shutter line, the tip of the
 * plug, and bit 2(k-1)+1 the focus line, its ring.
 */
namespace freezer_driver
{
constexpr int kJackCount = 8;

/**
 * The bit of the shutter line of the given jacks, 1 to 8.
 * @throw std::invalid_argument for a jack outside 1 to 8.
 */
uint16_t shutter_outputs(const std::vector<int64_t>& jacks);

/**
 * The bit of the focus line of the given jacks, 1 to 8.
 * @throw std::invalid_argument for a jack outside 1 to 8.
 */
uint16_t focus_outputs(const std::vector<int64_t>& jacks);

/**
 * Convert milliseconds, as people write them, to the microseconds the
 * controller counts.
 * @throw std::invalid_argument for a negative or too long time.
 */
uint32_t ms_to_us(double ms);

/**
 * The shot of the Freezer sketch: focus, open the shutters, fire the flashes,
 * then release everything. A flash jack closes both its lines.
 */
struct FlashShotRecipe
{
  std::vector<int64_t> cameras;
  std::vector<int64_t> flashes;
  double focus_ms = 100.0;
  double shutter_to_flash_ms = 100.0;
  double flash_ms = 100.0;
  double cooldown_ms = 200.0;

  Sequence build() const;
};

/**
 * The shot of StepIt Old: focus, open the shutters, switch the lights on for
 * a given time, switch them off, then close the shutters. The controller
 * decides how long the subject is lit.
 */
struct TimedLightRecipe
{
  std::vector<int64_t> cameras;
  std::vector<int64_t> lights;
  double focus_ms = 100.0;
  double shutter_to_light_ms = 500.0;
  double light_ms = 200.0;
  double light_to_close_ms = 200.0;
  double cooldown_ms = 200.0;

  Sequence build() const;
};
}  // namespace freezer_driver
