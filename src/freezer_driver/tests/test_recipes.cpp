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

#include <gtest/gtest.h>

#include <stdexcept>

#include <freezer_driver/recipes.hpp>

namespace freezer_driver::test
{
std::vector<uint16_t> outputs(const Sequence& sequence)
{
  std::vector<uint16_t> result;
  for (const auto& step : sequence.steps())
  {
    result.push_back(step.outputs);
  }
  return result;
}

std::vector<uint32_t> holds(const Sequence& sequence)
{
  std::vector<uint32_t> result;
  for (const auto& step : sequence.steps())
  {
    result.push_back(step.hold_us);
  }
  return result;
}

TEST(TestRecipes, jack_bits)
{
  EXPECT_EQ(shutter_outputs({ 1 }), 0x0001);
  EXPECT_EQ(focus_outputs({ 1 }), 0x0002);
  EXPECT_EQ(shutter_outputs({ 8 }), 0x4000);
  EXPECT_EQ(focus_outputs({ 8 }), 0x8000);
  EXPECT_EQ(shutter_outputs({ 1, 2, 3 }), 0x0015);
}

TEST(TestRecipes, reject_missing_jack)
{
  EXPECT_THROW(shutter_outputs({ 0 }), std::invalid_argument);
  EXPECT_THROW(focus_outputs({ 9 }), std::invalid_argument);
}

TEST(TestRecipes, ms_to_us)
{
  EXPECT_EQ(ms_to_us(100.0), 100'000u);
  EXPECT_EQ(ms_to_us(0.0204), 20u);
  EXPECT_THROW(ms_to_us(-1.0), std::invalid_argument);
  EXPECT_THROW(ms_to_us(1e10), std::invalid_argument);
}

/**
 * Seven cameras and a flash on OUT8 give the patterns of the Freezer sketch:
 * 10922 to focus, 16383 to open the shutters and 65535 to fire the flash.
 */
TEST(TestRecipes, flash_shot_matches_freezer_sketch)
{
  const FlashShotRecipe recipe{ { 1, 2, 3, 4, 5, 6, 7 }, { 8 }, 100.0, 100.0, 100.0, 200.0 };
  const Sequence sequence = recipe.build();
  EXPECT_EQ(outputs(sequence), (std::vector<uint16_t>{ 10922, 16383, 65535, 0 }));
  EXPECT_EQ(holds(sequence), (std::vector<uint32_t>{ 100'000, 100'000, 100'000, 200'000 }));
}

/**
 * Four cameras and lights on OUT5 to OUT8 give the patterns of StepIt Old:
 * 0x00AA to focus, 0x00FF to open the shutters, 0xFFFF with the lights on.
 */
TEST(TestRecipes, timed_light_matches_stepit_old)
{
  const TimedLightRecipe recipe{ { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, 100.0, 500.0, 200.0, 200.0, 200.0 };
  const Sequence sequence = recipe.build();
  EXPECT_EQ(outputs(sequence), (std::vector<uint16_t>{ 0x00AA, 0x00FF, 0xFFFF, 0x00FF, 0x0000 }));
  EXPECT_EQ(holds(sequence), (std::vector<uint32_t>{ 100'000, 500'000, 200'000, 200'000, 200'000 }));
}
}  // namespace freezer_driver::test
