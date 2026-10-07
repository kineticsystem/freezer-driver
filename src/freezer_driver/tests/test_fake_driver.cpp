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

#include <freezer_driver/fake/fake_driver.hpp>

namespace freezer_driver::test
{
using std::chrono::microseconds;

/** A fake controller on a clock the test moves by hand. */
struct TestFakeDriver : public ::testing::Test
{
  microseconds now{ 1'000 };
  FakeDriver driver{ [this] { return now; } };

  // Focus 100 µs, shutter 200 µs, release 300 µs.
  const Sequence sequence{ {
      Step{ Step::Type::SetAndHold, 0x0002, 100 },
      Step{ Step::Type::SetAndHold, 0x0003, 200 },
      Step{ Step::Type::SetAndHold, 0x0000, 300 },
  } };
};

TEST_F(TestFakeDriver, handshake)
{
  EXPECT_TRUE(driver.connect());
  const InfoResponse info = driver.get_info();
  EXPECT_EQ(info.name, "FREEZER");
  EXPECT_EQ(info.version.major(), 2);
  EXPECT_EQ(info.limits.max_steps, 16);
}

/** Unplugged, it answers nothing; plugged in again, it starts afresh. */
TEST_F(TestFakeDriver, unplugged_and_plugged_in_again)
{
  ASSERT_TRUE(driver.load_sequence(sequence).success());
  ASSERT_TRUE(driver.shoot().success());

  driver.set_plugged(false);
  EXPECT_THROW(driver.connect(), std::runtime_error);
  EXPECT_THROW(driver.get_status(), std::runtime_error);
  EXPECT_THROW(driver.set_outputs(0x0001), std::runtime_error);

  driver.set_plugged(true);
  EXPECT_TRUE(driver.connect());
  const StatusResponse status = driver.get_status();
  EXPECT_EQ(status.last_shot_id, 0) << "the shot ids start again";
  EXPECT_FALSE(driver.shoot().success()) << "the sequence is forgotten";
}

TEST_F(TestFakeDriver, load_returns_duration)
{
  const LoadSequenceResponse response = driver.load_sequence(sequence);
  EXPECT_TRUE(response.success());
  EXPECT_EQ(response.duration_us, 600u);
}

TEST_F(TestFakeDriver, shot_runs_and_ends)
{
  driver.load_sequence(sequence);
  const ShootResponse shot = driver.shoot();
  ASSERT_TRUE(shot.success());
  EXPECT_EQ(shot.shot_id, 1);
  EXPECT_EQ(shot.duration_us, 600u);

  now += microseconds{ 150 };
  StatusResponse status = driver.get_status();
  EXPECT_EQ(status.state, StatusResponse::State::Running);
  EXPECT_EQ(status.step, 1);
  EXPECT_EQ(status.elapsed_us, 150u);
  EXPECT_EQ(status.last_shot_id, 1);

  now += microseconds{ 450 };
  status = driver.get_status();
  EXPECT_EQ(status.state, StatusResponse::State::Idle);
  EXPECT_EQ(status.last_shot_id, 1);
}

/** The timeline records every pattern and when it was latched. */
TEST_F(TestFakeDriver, timeline)
{
  driver.load_sequence(sequence);
  driver.shoot();
  const std::vector<FakeDriver::Latch> expected{
    { microseconds{ 1'000 }, 0x0002 },
    { microseconds{ 1'100 }, 0x0003 },
    { microseconds{ 1'300 }, 0x0000 },
  };
  EXPECT_EQ(driver.timeline(), expected);
}

TEST_F(TestFakeDriver, shot_ids_increase)
{
  driver.load_sequence(sequence);
  EXPECT_EQ(driver.shoot().shot_id, 1);
  now += microseconds{ 600 };
  EXPECT_EQ(driver.shoot().shot_id, 2);
}

TEST_F(TestFakeDriver, refuse_shot_without_sequence)
{
  EXPECT_EQ(driver.shoot().reason(), Response::Reason::NoTable);
}

TEST_F(TestFakeDriver, refuse_while_running)
{
  driver.load_sequence(sequence);
  driver.shoot();
  EXPECT_EQ(driver.shoot().reason(), Response::Reason::Busy);
  EXPECT_EQ(driver.load_sequence(sequence).reason(), Response::Reason::Busy);
}

TEST_F(TestFakeDriver, refuse_invalid_sequence)
{
  const Sequence outputs_left_on{ { Step{ Step::Type::SetAndHold, 0x0001, 100 } } };
  EXPECT_EQ(driver.load_sequence(outputs_left_on).reason(), Response::Reason::InvalidTable);
}

TEST_F(TestFakeDriver, never_finish)
{
  driver.set_failure(FakeDriver::Failure::NeverFinish);
  driver.load_sequence(sequence);
  driver.shoot();
  now += microseconds{ 10'000 };
  EXPECT_EQ(driver.get_status().state, StatusResponse::State::Running);
}

/** A reset forgets the sequence and the shot: the host must notice. */
TEST_F(TestFakeDriver, reset_during_shot)
{
  driver.set_failure(FakeDriver::Failure::ResetDuringShot);
  driver.load_sequence(sequence);
  driver.shoot();
  const StatusResponse status = driver.get_status();
  EXPECT_EQ(status.state, StatusResponse::State::Idle);
  EXPECT_EQ(status.last_shot_id, 0);
}

TEST_F(TestFakeDriver, lateness)
{
  driver.set_lateness(8);
  driver.load_sequence(sequence);
  driver.shoot();
  now += microseconds{ 600 };
  EXPECT_EQ(driver.get_status().worst_lateness_us, 8u);
}
/** The outputs stay as set, until set again. */
TEST_F(TestFakeDriver, set_outputs)
{
  EXPECT_TRUE(driver.set_outputs(0xC000).success());
  EXPECT_EQ(driver.outputs(), 0xC000);
  now += microseconds{ 1'000 };
  EXPECT_EQ(driver.outputs(), 0xC000);
  EXPECT_TRUE(driver.set_outputs(0x0000).success());
  EXPECT_EQ(driver.outputs(), 0x0000);
}

/** The shot owns the outputs, and ends with every one off, lights included. */
TEST_F(TestFakeDriver, a_shot_owns_the_outputs)
{
  driver.set_outputs(0xC000);
  driver.load_sequence(sequence);
  driver.shoot();
  EXPECT_EQ(driver.set_outputs(0xC000).reason(), Response::Reason::Busy);
  EXPECT_EQ(driver.outputs(), 0x0002);
  now += microseconds{ 600 };
  EXPECT_EQ(driver.outputs(), 0x0000);
  EXPECT_TRUE(driver.set_outputs(0xC000).success());
}

/** A stop ends the shot where it is: the patterns still to come never latch. */
TEST_F(TestFakeDriver, stop_ends_the_shot)
{
  driver.load_sequence(sequence);
  const uint16_t shot_id = driver.shoot().shot_id;
  now += microseconds{ 150 };
  EXPECT_TRUE(driver.stop().success());

  const StatusResponse status = driver.get_status();
  EXPECT_EQ(status.state, StatusResponse::State::Idle);
  EXPECT_EQ(status.last_shot_id, shot_id);
  const std::vector<FakeDriver::Latch> expected{
    { microseconds{ 1'000 }, 0x0002 },
    { microseconds{ 1'100 }, 0x0003 },
    { microseconds{ 1'150 }, 0x0000 },
  };
  EXPECT_EQ(driver.timeline(), expected);
}

TEST_F(TestFakeDriver, stop_switches_the_outputs_off)
{
  driver.set_outputs(0xC000);
  EXPECT_TRUE(driver.stop().success());
  EXPECT_EQ(driver.outputs(), 0x0000);
}

/** IN1 fires the loaded sequence, unless a shot runs or none is loaded. */
TEST_F(TestFakeDriver, trigger)
{
  EXPECT_FALSE(driver.trigger());
  driver.load_sequence(sequence);
  EXPECT_TRUE(driver.trigger());
  EXPECT_EQ(driver.get_status().last_shot_id, 1);
  EXPECT_FALSE(driver.trigger());
  EXPECT_EQ(driver.shoot().reason(), Response::Reason::Busy);
  now += microseconds{ 600 };
  EXPECT_TRUE(driver.trigger());
  EXPECT_EQ(driver.get_status().last_shot_id, 2);
}
}  // namespace freezer_driver::test
