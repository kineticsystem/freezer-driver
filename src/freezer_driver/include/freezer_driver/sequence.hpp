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
#include <optional>
#include <string>
#include <vector>

namespace freezer_driver
{
/**
 * One step of a shot: output a pattern on the 16 optocouplers, then hold it.
 */
struct Step
{
  enum class Type : uint8_t
  {
    SetAndHold = 0x00,  // The other values are reserved for repeat and wait steps.
  };

  Type type = Type::SetAndHold;
  uint16_t outputs = 0;
  uint32_t hold_us = 0;

  bool operator==(const Step&) const = default;
};

/**
 * What the controller accepts, as reported at the handshake.
 */
struct SequenceLimits
{
  uint8_t max_steps = 0;
  uint32_t min_hold_us = 0;
  uint32_t max_duration_us = 0;
};

/**
 * The table of steps of a shot, as LoadSequence carries it.
 */
class Sequence
{
public:
  Sequence() = default;
  explicit Sequence(std::vector<Step> steps);

  const std::vector<Step>& steps() const;

  /** The sum of the holds, in µs. */
  uint64_t duration_us() const;

  /**
   * The payload of LoadSequence: the step count, then for each step its type,
   * its outputs and its hold, most significant byte first.
   * @throw std::length_error if there are more than 255 steps.
   */
  std::vector<uint8_t> encode() const;

  /**
   * The CRC-16 of the encoded sequence. The controller computes the same, and
   * Shoot carries it so that the controller never fires a table the host did
   * not mean.
   */
  uint16_t checksum() const;

  bool operator==(const Sequence&) const = default;

private:
  std::vector<Step> steps_;
};

/**
 * Check a sequence against the rules of the controller, before sending it.
 * The firmware applies the same rules: a camera must never be left with its
 * shutter held, so the last step must switch every output off.
 * @return Why the sequence is refused, or nothing when it is valid.
 */
std::optional<std::string> validate(const Sequence& sequence, const SequenceLimits& limits);
}  // namespace freezer_driver
