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

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <freezer_driver/fake/fake_driver.hpp>
#include <freezer_node/freezer_node.hpp>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

namespace freezer_node::test
{
using freezer_driver::FakeDriver;
using Shoot = freezer_msgs::action::Shoot;
using ResultCode = rclcpp_action::ResultCode;
using namespace std::chrono_literals;

/**
 * The node with a fake controller, and an action client in the same process.
 * The default sequence lasts 4 ms, so that a shot takes no time.
 */
class TestFreezerNode : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestSuite()
  {
    rclcpp::shutdown();
  }

  void SetUp() override
  {
    auto driver = std::make_unique<FakeDriver>();
    fake = driver.get();
    rclcpp::NodeOptions options;
    options.parameter_overrides({
        { "sequences.flash_shot.focus_ms", 1.0 },
        { "sequences.flash_shot.shutter_to_flash_ms", 1.0 },
        { "sequences.flash_shot.flash_ms", 1.0 },
        { "sequences.flash_shot.cooldown_ms", 1.0 },
        { "poll_period", 0.001 },
        { "end_margin", 0.05 },
    });
    node = std::make_shared<FreezerNode>(options, std::move(driver));
    client_node = std::make_shared<rclcpp::Node>("client");
    client = rclcpp_action::create_client<Shoot>(client_node, "/freezer/shoot");
    executor.add_node(node);
    executor.add_node(client_node);
    spinner = std::thread{ [this] { executor.spin(); } };
    ASSERT_TRUE(client->wait_for_action_server(5s));
  }

  void TearDown() override
  {
    executor.cancel();
    spinner.join();
  }

  /** Send a goal; nullptr when the node rejects it. */
  rclcpp_action::ClientGoalHandle<Shoot>::SharedPtr send(const Shoot::Goal& goal)
  {
    rclcpp_action::Client<Shoot>::SendGoalOptions options;
    options.feedback_callback = [this](auto, const std::shared_ptr<const Shoot::Feedback> feedback) {
      std::lock_guard lock{ mutex };
      states.push_back(feedback->state);
    };
    auto future = client->async_send_goal(goal, options);
    if (future.wait_for(5s) != std::future_status::ready)
    {
      return nullptr;
    }
    return future.get();
  }

  rclcpp_action::ClientGoalHandle<Shoot>::WrappedResult
  result(const rclcpp_action::ClientGoalHandle<Shoot>::SharedPtr& handle)
  {
    auto future = client->async_get_result(handle);
    EXPECT_EQ(future.wait_for(5s), std::future_status::ready);
    return future.get();
  }

  static Shoot::Goal raw_goal(std::vector<std::pair<uint16_t, uint32_t>> steps)
  {
    Shoot::Goal goal;
    for (const auto& [outputs, hold_us] : steps)
    {
      freezer_msgs::msg::Step step;
      step.type = freezer_msgs::msg::Step::SET_AND_HOLD;
      step.outputs = outputs;
      step.hold_us = hold_us;
      goal.steps.push_back(step);
    }
    return goal;
  }

  FakeDriver* fake = nullptr;
  std::shared_ptr<FreezerNode> node;
  rclcpp::Node::SharedPtr client_node;
  rclcpp_action::Client<Shoot>::SharedPtr client;
  rclcpp::executors::SingleThreadedExecutor executor;
  std::thread spinner;
  std::mutex mutex;
  std::vector<uint8_t> states;
};

/** The default sequence fires the patterns of the Freezer sketch. */
TEST_F(TestFreezerNode, shoot_default_sequence)
{
  auto handle = send(Shoot::Goal{});
  ASSERT_TRUE(handle);
  const auto wrapped = result(handle);
  ASSERT_EQ(wrapped.code, ResultCode::SUCCEEDED) << wrapped.result->message;
  EXPECT_EQ(wrapped.result->shot_id, 1);
  EXPECT_EQ(wrapped.result->duration_us, 4'000u);

  std::vector<uint16_t> outputs;
  for (const auto& latch : fake->timeline())
  {
    outputs.push_back(latch.outputs);
  }
  EXPECT_EQ(outputs, (std::vector<uint16_t>{ 10922, 16383, 65535, 0 }));

  // rclcpp_action drops the feedback published before the client has
  // processed the acceptance of its goal, so the client may miss LOADING, and
  // on a slow machine the first RUNNING too. What it receives is in order.
  std::lock_guard lock{ mutex };
  const auto running = std::find(states.begin(), states.end(), Shoot::Feedback::RUNNING);
  EXPECT_EQ(std::find(running, states.end(), Shoot::Feedback::LOADING), states.end());
}

TEST_F(TestFreezerNode, shoot_named_sequence)
{
  Shoot::Goal goal;
  goal.sequence = "flash_shot";
  auto handle = send(goal);
  ASSERT_TRUE(handle);
  EXPECT_EQ(result(handle).code, ResultCode::SUCCEEDED);
}

TEST_F(TestFreezerNode, shoot_raw_table)
{
  auto handle = send(raw_goal({ { 0x0003, 1'000 }, { 0x0000, 1'000 } }));
  ASSERT_TRUE(handle);
  EXPECT_EQ(result(handle).code, ResultCode::SUCCEEDED);
  ASSERT_EQ(fake->timeline().size(), 2u);
  EXPECT_EQ(fake->timeline()[0].outputs, 0x0003);
}

/** The shot ids of the controller come back in the results. */
TEST_F(TestFreezerNode, shoot_twice)
{
  auto first = send(Shoot::Goal{});
  ASSERT_TRUE(first);
  EXPECT_EQ(result(first).result->shot_id, 1);
  auto second = send(Shoot::Goal{});
  ASSERT_TRUE(second);
  EXPECT_EQ(result(second).result->shot_id, 2);
}

TEST_F(TestFreezerNode, reject_unknown_sequence)
{
  Shoot::Goal goal;
  goal.sequence = "nonexistent";
  EXPECT_FALSE(send(goal));
}

/** A camera must never be left with its shutter held. */
TEST_F(TestFreezerNode, reject_outputs_left_on)
{
  EXPECT_FALSE(send(raw_goal({ { 0x0003, 1'000 } })));
}

TEST_F(TestFreezerNode, reject_while_shooting)
{
  auto first = send(raw_goal({ { 0x0003, 300'000 }, { 0x0000, 1'000 } }));
  ASSERT_TRUE(first);
  EXPECT_FALSE(send(Shoot::Goal{}));
  EXPECT_EQ(result(first).code, ResultCode::SUCCEEDED);
}

TEST_F(TestFreezerNode, abort_when_the_shot_never_ends)
{
  fake->set_failure(FakeDriver::Failure::NeverFinish);
  auto handle = send(Shoot::Goal{});
  ASSERT_TRUE(handle);
  const auto wrapped = result(handle);
  EXPECT_EQ(wrapped.code, ResultCode::ABORTED);
  EXPECT_THAT(wrapped.result->message, ::testing::HasSubstr("did not end"));
}

/** After a reset the node loads the sequence again before the next shot. */
TEST_F(TestFreezerNode, abort_when_the_controller_resets)
{
  fake->set_failure(FakeDriver::Failure::ResetDuringShot);
  auto handle = send(Shoot::Goal{});
  ASSERT_TRUE(handle);
  const auto wrapped = result(handle);
  EXPECT_EQ(wrapped.code, ResultCode::ABORTED);
  EXPECT_THAT(wrapped.result->message, ::testing::HasSubstr("may have reset"));

  fake->set_failure(FakeDriver::Failure::None);
  auto next = send(Shoot::Goal{});
  ASSERT_TRUE(next);
  EXPECT_EQ(result(next).code, ResultCode::SUCCEEDED);
}
}  // namespace freezer_node::test
