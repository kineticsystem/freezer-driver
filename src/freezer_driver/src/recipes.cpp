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

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include <freezer_driver/recipes.hpp>

namespace freezer_driver
{
namespace
{
/** The index of the first bit of a jack, after checking it exists. */
int first_bit(int64_t jack)
{
  if (jack < 1 || jack > kJackCount)
  {
    throw std::invalid_argument("Jack " + std::to_string(jack) + " does not exist: the board has jacks 1 to " +
                                std::to_string(kJackCount) + ".");
  }
  return 2 * static_cast<int>(jack - 1);
}
}  // namespace

uint16_t shutter_outputs(const std::vector<int64_t>& jacks)
{
  unsigned int outputs = 0;
  for (auto jack : jacks)
  {
    outputs |= 1u << first_bit(jack);
  }
  return static_cast<uint16_t>(outputs);
}

uint16_t focus_outputs(const std::vector<int64_t>& jacks)
{
  unsigned int outputs = 0;
  for (auto jack : jacks)
  {
    outputs |= 1u << (first_bit(jack) + 1);
  }
  return static_cast<uint16_t>(outputs);
}

uint32_t ms_to_us(double ms)
{
  const double us = std::round(ms * 1000.0);
  if (!(us >= 0.0) || us > static_cast<double>(std::numeric_limits<uint32_t>::max()))
  {
    throw std::invalid_argument("A time of " + std::to_string(ms) + " ms is out of range.");
  }
  return static_cast<uint32_t>(us);
}

Sequence FlashShotRecipe::build() const
{
  const uint16_t focus = focus_outputs(cameras);
  const uint16_t open = static_cast<uint16_t>(focus | shutter_outputs(cameras));
  const uint16_t flash = static_cast<uint16_t>(open | focus_outputs(flashes) | shutter_outputs(flashes));
  return Sequence{ {
      Step{ Step::Type::SetAndHold, focus, ms_to_us(focus_ms) },
      Step{ Step::Type::SetAndHold, open, ms_to_us(shutter_to_flash_ms) },
      Step{ Step::Type::SetAndHold, flash, ms_to_us(flash_ms) },
      Step{ Step::Type::SetAndHold, 0, ms_to_us(cooldown_ms) },
  } };
}

Sequence TimedLightRecipe::build() const
{
  const uint16_t focus = focus_outputs(cameras);
  const uint16_t open = static_cast<uint16_t>(focus | shutter_outputs(cameras));
  const uint16_t lit = static_cast<uint16_t>(open | focus_outputs(lights) | shutter_outputs(lights));
  return Sequence{ {
      Step{ Step::Type::SetAndHold, focus, ms_to_us(focus_ms) },
      Step{ Step::Type::SetAndHold, open, ms_to_us(shutter_to_light_ms) },
      Step{ Step::Type::SetAndHold, lit, ms_to_us(light_ms) },
      Step{ Step::Type::SetAndHold, open, ms_to_us(light_to_close_ms) },
      Step{ Step::Type::SetAndHold, 0, ms_to_us(cooldown_ms) },
  } };
}
}  // namespace freezer_driver
