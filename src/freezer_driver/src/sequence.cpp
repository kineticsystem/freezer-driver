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

#include <stdexcept>

#include <freezer_driver/sequence.hpp>

#include <framed_serial/crc_utils.hpp>

namespace freezer_driver
{
Sequence::Sequence(std::vector<Step> steps) : steps_{ std::move(steps) }
{
}

const std::vector<Step>& Sequence::steps() const
{
  return steps_;
}

uint64_t Sequence::duration_us() const
{
  uint64_t duration = 0;
  for (const auto& step : steps_)
  {
    duration += step.hold_us;
  }
  return duration;
}

std::vector<uint8_t> Sequence::encode() const
{
  if (steps_.size() > 255)
  {
    throw std::length_error("A sequence cannot have more than 255 steps.");
  }
  std::vector<uint8_t> bytes;
  bytes.reserve(1 + 7 * steps_.size());
  bytes.push_back(static_cast<uint8_t>(steps_.size()));
  for (const auto& step : steps_)
  {
    bytes.push_back(static_cast<uint8_t>(step.type));
    bytes.push_back(static_cast<uint8_t>(step.outputs >> 8));
    bytes.push_back(static_cast<uint8_t>(step.outputs & 0xFF));
    bytes.push_back(static_cast<uint8_t>(step.hold_us >> 24));
    bytes.push_back(static_cast<uint8_t>((step.hold_us >> 16) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((step.hold_us >> 8) & 0xFF));
    bytes.push_back(static_cast<uint8_t>(step.hold_us & 0xFF));
  }
  return bytes;
}

uint16_t Sequence::checksum() const
{
  return framed_serial::crc_ccitt(encode());
}

std::optional<std::string> validate(const Sequence& sequence, const SequenceLimits& limits)
{
  const auto& steps = sequence.steps();
  if (steps.empty())
  {
    return "the sequence has no steps";
  }
  if (steps.size() > limits.max_steps)
  {
    return "the sequence has " + std::to_string(steps.size()) + " steps, the controller accepts " +
           std::to_string(limits.max_steps);
  }
  for (std::size_t i = 0; i < steps.size(); ++i)
  {
    if (steps[i].type != Step::Type::SetAndHold)
    {
      return "step " + std::to_string(i) + " has an unknown type";
    }
    if (steps[i].hold_us < limits.min_hold_us)
    {
      return "step " + std::to_string(i) + " holds " + std::to_string(steps[i].hold_us) +
             " us, the controller needs at least " + std::to_string(limits.min_hold_us) + " us";
    }
  }
  if (sequence.duration_us() > limits.max_duration_us)
  {
    return "the sequence lasts " + std::to_string(sequence.duration_us()) + " us, the controller accepts " +
           std::to_string(limits.max_duration_us) + " us";
  }
  if (steps.back().outputs != 0)
  {
    return "the last step must switch every output off";
  }
  return std::nullopt;
}
}  // namespace freezer_driver
