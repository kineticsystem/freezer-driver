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

#include <freezer_driver/sequence.hpp>

namespace freezer_driver::test
{
const SequenceLimits kLimits{ 3, 20, 1'000'000 };

Sequence two_steps()
{
  return Sequence{ {
      Step{ Step::Type::SetAndHold, 0x0102, 0x00030405 },
      Step{ Step::Type::SetAndHold, 0x0000, 20 },
  } };
}

/**
 * The payload of LoadSequence: the step count, then each step as its type,
 * its outputs and its hold, most significant byte first.
 */
TEST(TestSequence, encode)
{
  const std::vector<uint8_t> expected{
    0x02,                    // step count
    0x00,                    // type
    0x01, 0x02,              // outputs
    0x00, 0x03, 0x04, 0x05,  // hold
    0x00,                    // type
    0x00, 0x00,              // outputs
    0x00, 0x00, 0x00, 0x14,  // hold
  };
  EXPECT_EQ(two_steps().encode(), expected);
}

/**
 * The checksum is the CRC-16 Kermit of the payload, which the firmware
 * computes with the same code.
 */
TEST(TestSequence, checksum)
{
  EXPECT_EQ(two_steps().checksum(), 0x1D0A);
}

TEST(TestSequence, duration)
{
  EXPECT_EQ(two_steps().duration_us(), 0x00030405u + 20u);
}

TEST(TestSequence, valid)
{
  EXPECT_FALSE(validate(Sequence{ { Step{ Step::Type::SetAndHold, 0, 20 } } }, kLimits));
}

TEST(TestSequence, reject_empty)
{
  EXPECT_TRUE(validate(Sequence{}, kLimits));
}

TEST(TestSequence, reject_too_many_steps)
{
  const Step step{ Step::Type::SetAndHold, 0, 20 };
  EXPECT_TRUE(validate(Sequence{ { step, step, step, step } }, kLimits));
}

TEST(TestSequence, reject_short_hold)
{
  EXPECT_TRUE(validate(Sequence{ { Step{ Step::Type::SetAndHold, 0, 19 } } }, kLimits));
}

TEST(TestSequence, reject_long_sequence)
{
  const Step step{ Step::Type::SetAndHold, 0, 600'000 };
  EXPECT_TRUE(validate(Sequence{ { step, step } }, kLimits));
}

/** A camera must never be left with its shutter held. */
TEST(TestSequence, reject_outputs_left_on)
{
  const auto error = validate(Sequence{ { Step{ Step::Type::SetAndHold, 0x0001, 100 } } }, kLimits);
  ASSERT_TRUE(error);
  EXPECT_EQ(*error, "the last step must switch every output off");
}
}  // namespace freezer_driver::test
