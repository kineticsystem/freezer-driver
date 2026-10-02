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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <freezer_driver/fake/fake_driver.hpp>
#include <freezer_driver/shot_runner.hpp>

namespace freezer_driver::test
{
using std::chrono::microseconds;
using Outcome = ShotRunner::Result::Outcome;

/**
 * A runner on a fake controller and a clock the test owns: each sleep of the
 * runner moves the clock forward, so a shot runs instantly and exactly.
 */
struct TestShotRunner : public ::testing::Test
{
  microseconds now{ 0 };
  FakeDriver driver{ [this] { return now; } };
  ShotRunner runner{ driver, ShotRunner::Config{ microseconds{ 1'000 }, microseconds{ 5'000 } }, [this] { return now; },
                     [this](microseconds period) { now += period; } };

  // Focus 2 ms, shutter 3 ms, release 5 ms: 10 ms in all.
  const Sequence sequence{ {
      Step{ Step::Type::SetAndHold, 0x0002, 2'000 },
      Step{ Step::Type::SetAndHold, 0x0003, 3'000 },
      Step{ Step::Type::SetAndHold, 0x0000, 5'000 },
  } };
};

TEST_F(TestShotRunner, shot_succeeds)
{
  driver.set_lateness(7);
  const auto result = runner.run(sequence);
  EXPECT_EQ(result.outcome, Outcome::Succeeded) << result.message;
  EXPECT_EQ(result.shot_id, 1);
  EXPECT_EQ(result.duration_us, 10'000u);
  EXPECT_EQ(result.worst_lateness_us, 7u);

  const std::vector<FakeDriver::Latch> expected{
    { microseconds{ 0 }, 0x0002 },
    { microseconds{ 2'000 }, 0x0003 },
    { microseconds{ 5'000 }, 0x0000 },
  };
  EXPECT_EQ(driver.timeline(), expected);
}

/** The end is learned at the first status query after it: one poll late at most. */
TEST_F(TestShotRunner, shot_ends_at_the_first_poll_after_its_end)
{
  runner.run(sequence);
  EXPECT_EQ(now, microseconds{ 10'000 });
}

TEST_F(TestShotRunner, callbacks)
{
  std::vector<std::string> calls;
  ShotRunner::Callbacks callbacks;
  callbacks.start = [&] {
    calls.push_back("start");
    return true;
  };
  callbacks.started = [&](uint16_t shot_id, uint32_t duration_us) {
    calls.push_back("started " + std::to_string(shot_id) + " " + std::to_string(duration_us));
  };
  callbacks.progress = [&](uint8_t step, uint32_t elapsed_us) {
    calls.push_back("step " + std::to_string(step) + " at " + std::to_string(elapsed_us));
  };
  runner.run(sequence, callbacks);
  EXPECT_THAT(calls, ::testing::ElementsAre("start", "started 1 10000", "step 0 at 1000", "step 1 at 2000",
                                            "step 1 at 3000", "step 1 at 4000", "step 2 at 5000", "step 2 at 6000",
                                            "step 2 at 7000", "step 2 at 8000", "step 2 at 9000"));
}

/** The controller keeps the sequence: firing it again does not load it again. */
TEST_F(TestShotRunner, same_sequence_is_loaded_once)
{
  EXPECT_EQ(runner.run(sequence).shot_id, 1);
  EXPECT_EQ(runner.run(sequence).shot_id, 2);
  EXPECT_EQ(driver.loads(), 1);
}

TEST_F(TestShotRunner, changed_sequence_is_loaded_again)
{
  runner.run(sequence);
  const Sequence other{ { Step{ Step::Type::SetAndHold, 0x0000, 1'000 } } };
  EXPECT_EQ(runner.run(other).outcome, Outcome::Succeeded);
  EXPECT_EQ(driver.loads(), 2);
}

TEST_F(TestShotRunner, cancel_before_the_shot)
{
  ShotRunner::Callbacks callbacks;
  callbacks.start = [] { return false; };
  const auto result = runner.run(sequence, callbacks);
  EXPECT_EQ(result.outcome, Outcome::Canceled);
  EXPECT_TRUE(driver.timeline().empty());
}

TEST_F(TestShotRunner, refused_sequence)
{
  // The fake controller applies the rules of the firmware.
  const Sequence outputs_left_on{ { Step{ Step::Type::SetAndHold, 0x0001, 1'000 } } };
  const auto result = runner.run(outputs_left_on);
  EXPECT_EQ(result.outcome, Outcome::Aborted);
  EXPECT_EQ(result.message, "The controller refused the sequence: invalid sequence.");
}

/** The deadline is the duration plus the margin, on the clock of the host. */
TEST_F(TestShotRunner, shot_that_never_ends)
{
  driver.set_failure(FakeDriver::Failure::NeverFinish);
  const auto result = runner.run(sequence);
  EXPECT_EQ(result.outcome, Outcome::Aborted);
  EXPECT_THAT(result.message, ::testing::HasSubstr("did not end"));
  EXPECT_EQ(now, microseconds{ 16'000 });
}

/** A controller that resets during a shot loses it, and its sequence. */
TEST_F(TestShotRunner, reset_during_the_shot)
{
  driver.set_failure(FakeDriver::Failure::ResetDuringShot);
  const auto result = runner.run(sequence);
  EXPECT_EQ(result.outcome, Outcome::Aborted);
  EXPECT_THAT(result.message, ::testing::HasSubstr("may have reset"));

  driver.set_failure(FakeDriver::Failure::None);
  EXPECT_EQ(runner.run(sequence).outcome, Outcome::Succeeded);
  EXPECT_EQ(driver.loads(), 2);
}

/** A controller that reset between two shots refuses the second: load again, once. */
TEST_F(TestShotRunner, reset_between_shots)
{
  runner.run(sequence);
  driver.reset();
  const auto result = runner.run(sequence);
  EXPECT_EQ(result.outcome, Outcome::Succeeded) << result.message;
  EXPECT_EQ(result.shot_id, 1);
  EXPECT_EQ(driver.loads(), 2);
}
}  // namespace freezer_driver::test
