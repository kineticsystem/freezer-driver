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
#include <optional>
#include <string>
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
using SetOutputs = freezer_msgs::srv::SetOutputs;
using Trigger = std_srvs::srv::Trigger;
using OutputsMsg = freezer_msgs::msg::Outputs;
using ShotMsg = freezer_msgs::msg::Shot;
using StatusMsg = freezer_msgs::msg::ControllerStatus;
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
        { "watch_period", 0.005 },
        { "reconnect_period", 0.01 },
    });
    node = std::make_shared<FreezerNode>(options, std::move(driver));
    client_node = std::make_shared<rclcpp::Node>("client");
    client = rclcpp_action::create_client<Shoot>(client_node, "/freezer/shoot");
    set_outputs_client = client_node->create_client<SetOutputs>("/freezer/set_outputs");
    stop_client = client_node->create_client<Trigger>("/freezer/stop");
    press_trigger_client = client_node->create_client<Trigger>("/freezer/fake/press_trigger");
    const auto latched = rclcpp::QoS{ 10 }.reliable().transient_local();
    outputs_subscription =
        client_node->create_subscription<OutputsMsg>("/freezer/outputs", latched, [this](const OutputsMsg& message) {
          std::lock_guard lock{ mutex };
          received_outputs.push_back(message);
        });
    shots_subscription =
        client_node->create_subscription<ShotMsg>("/freezer/shots", latched, [this](const ShotMsg& message) {
          std::lock_guard lock{ mutex };
          shots.push_back(message);
        });
    executor.add_node(node);
    executor.add_node(client_node);
    spinner = std::thread{ [this] { executor.spin(); } };
    ASSERT_TRUE(client->wait_for_action_server(5s));
    ASSERT_TRUE(set_outputs_client->wait_for_service(5s));
    ASSERT_TRUE(stop_client->wait_for_service(5s));
    ASSERT_TRUE(press_trigger_client->wait_for_service(5s));
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

  /**
   * Wait for the feedback that the shot has started: the fake driver is
   * the node's while a shot runs, the test must not read it.
   */
  bool wait_running()
  {
    for (int i = 0; i < 5'000; ++i)
    {
      {
        std::lock_guard lock{ mutex };
        if (std::find(states.begin(), states.end(), Shoot::Feedback::RUNNING) != states.end())
        {
          return true;
        }
      }
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }

  SetOutputs::Response set_outputs(uint16_t outputs)
  {
    auto request = std::make_shared<SetOutputs::Request>();
    request->outputs = outputs;
    auto future = set_outputs_client->async_send_request(request);
    EXPECT_EQ(future.wait_for(5s), std::future_status::ready);
    return *future.get();
  }

  Trigger::Response stop()
  {
    auto future = stop_client->async_send_request(std::make_shared<Trigger::Request>());
    EXPECT_EQ(future.wait_for(5s), std::future_status::ready);
    return *future.get();
  }

  Trigger::Response press_trigger()
  {
    auto future = press_trigger_client->async_send_request(std::make_shared<Trigger::Request>());
    EXPECT_EQ(future.wait_for(5s), std::future_status::ready);
    return *future.get();
  }

  /** Wait until the shot events received satisfy the predicate. */
  template <typename Predicate>
  bool wait_shots(Predicate predicate)
  {
    for (int i = 0; i < 5'000; ++i)
    {
      {
        std::lock_guard lock{ mutex };
        if (predicate(shots))
        {
          return true;
        }
      }
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }

  /** The last outputs received, once some came. */
  std::optional<uint16_t> last_outputs()
  {
    std::lock_guard lock{ mutex };
    return received_outputs.empty() ? std::nullopt : std::optional<uint16_t>{ received_outputs.back().outputs };
  }

  /** Wait until the last outputs received are these. */
  bool wait_outputs(uint16_t expected)
  {
    for (int i = 0; i < 5'000; ++i)
    {
      if (last_outputs() == expected)
      {
        return true;
      }
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }

  /** Wait until the condition holds, for up to 5 s. */
  template <typename Condition>
  static bool wait_until(Condition condition)
  {
    for (int i = 0; i < 5'000; ++i)
    {
      if (condition())
      {
        return true;
      }
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }

  /** A second node, unplugged at start, named apart from the fixture's. */
  static std::shared_ptr<FreezerNode> unplugged_node(FakeDriver*& fake_driver, double reconnect_period)
  {
    auto driver = std::make_unique<FakeDriver>();
    fake_driver = driver.get();
    fake_driver->set_plugged(false);
    rclcpp::NodeOptions options;
    options.arguments({ "--ros-args", "-r", "__node:=freezer_unplugged" });
    options.parameter_overrides({ { "watch_period", 0.005 }, { "reconnect_period", reconnect_period } });
    return std::make_shared<FreezerNode>(options, std::move(driver));
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
  rclcpp::Client<SetOutputs>::SharedPtr set_outputs_client;
  rclcpp::Client<Trigger>::SharedPtr stop_client;
  rclcpp::Client<Trigger>::SharedPtr press_trigger_client;
  rclcpp::Subscription<OutputsMsg>::SharedPtr outputs_subscription;
  rclcpp::Subscription<ShotMsg>::SharedPtr shots_subscription;
  std::vector<OutputsMsg> received_outputs;
  std::vector<ShotMsg> shots;
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

/** The lights stay on until switched off. */
TEST_F(TestFreezerNode, set_outputs)
{
  const auto response = set_outputs(0xC000);
  EXPECT_TRUE(response.success) << response.message;
  EXPECT_EQ(fake->outputs(), 0xC000);
  EXPECT_TRUE(set_outputs(0x0000).success);
  EXPECT_EQ(fake->outputs(), 0x0000);
}

/** The shot owns the outputs: setting them during it is refused. */
TEST_F(TestFreezerNode, set_outputs_refused_during_a_shot)
{
  auto handle = send(raw_goal({ { 0x0003, 300'000 }, { 0x0000, 1'000 } }));
  ASSERT_TRUE(handle);
  ASSERT_TRUE(wait_running());
  const auto response = set_outputs(0xC000);
  EXPECT_FALSE(response.success);
  EXPECT_THAT(response.message, ::testing::HasSubstr("a shot is running"));
  EXPECT_EQ(result(handle).code, ResultCode::SUCCEEDED);
}

/** A stop ends the shot at once, and its goal aborts. */
TEST_F(TestFreezerNode, stop_a_shot)
{
  auto handle = send(raw_goal({ { 0x0003, 2'000'000 }, { 0x0000, 1'000 } }));
  ASSERT_TRUE(handle);
  ASSERT_TRUE(wait_running());
  EXPECT_TRUE(stop().success);

  const auto wrapped = result(handle);
  EXPECT_EQ(wrapped.code, ResultCode::ABORTED);
  EXPECT_EQ(wrapped.result->message, "Stopped.");
  EXPECT_EQ(fake->outputs(), 0x0000);
  EXPECT_EQ(fake->timeline().back().outputs, 0x0000);
  EXPECT_LT(fake->timeline().back().time - fake->timeline().front().time, std::chrono::microseconds{ 1'000'000 });

  // The next shot runs.
  auto next = send(Shoot::Goal{});
  ASSERT_TRUE(next);
  EXPECT_EQ(result(next).code, ResultCode::SUCCEEDED);
}

TEST_F(TestFreezerNode, stop_switches_the_outputs_off)
{
  set_outputs(0xC000);
  EXPECT_TRUE(stop().success);
  EXPECT_EQ(fake->outputs(), 0x0000);
}

/** A shot fired by IN1 owns the controller: a goal meanwhile aborts. */
TEST_F(TestFreezerNode, abort_during_a_shot_of_the_trigger)
{
  auto first = send(raw_goal({ { 0x0003, 1'000 }, { 0x0000, 2'000'000 } }));
  ASSERT_TRUE(first);
  ASSERT_EQ(result(first).code, ResultCode::SUCCEEDED);
  ASSERT_TRUE(fake->trigger());

  auto second = send(raw_goal({ { 0x0003, 1'000 }, { 0x0000, 2'000'000 } }));
  ASSERT_TRUE(second);
  const auto wrapped = result(second);
  EXPECT_EQ(wrapped.code, ResultCode::ABORTED);
  EXPECT_THAT(wrapped.result->message, ::testing::HasSubstr("a shot is running"));
}

/** outputs tells what set_outputs and stop did. */
TEST_F(TestFreezerNode, outputs_topic)
{
  ASSERT_TRUE(wait_outputs(0x0000)) << "the node publishes the outputs, all off, when it starts";
  set_outputs(0xC000);
  EXPECT_TRUE(wait_outputs(0xC000));
  stop();
  EXPECT_TRUE(wait_outputs(0x0000));
}

/** shots tells when a shot starts and ends, with its steps. */
TEST_F(TestFreezerNode, shots_topic)
{
  auto handle = send(raw_goal({ { 0x0003, 1'000 }, { 0x0000, 2'000 } }));
  ASSERT_TRUE(handle);
  const auto wrapped = result(handle);
  ASSERT_EQ(wrapped.code, ResultCode::SUCCEEDED);

  ASSERT_TRUE(wait_shots([](const auto& received) { return received.size() >= 2; }));
  std::lock_guard lock{ mutex };
  EXPECT_EQ(shots[0].event, ShotMsg::STARTED);
  EXPECT_EQ(shots[0].source, ShotMsg::SOURCE_NODE);
  EXPECT_EQ(shots[0].shot_id, wrapped.result->shot_id);
  ASSERT_EQ(shots[0].steps.size(), 2u);
  EXPECT_EQ(shots[0].steps[0].outputs, 0x0003);
  EXPECT_EQ(shots[0].duration_us, 3'000u);
  EXPECT_EQ(shots[1].event, ShotMsg::ENDED);
  EXPECT_EQ(shots[1].elapsed_us, 3'000u);
  EXPECT_EQ(rclcpp::Time{ shots[1].started }, rclcpp::Time{ wrapped.result->started });
  EXPECT_EQ(received_outputs.back().outputs, 0x0000);
}

TEST_F(TestFreezerNode, shots_topic_tells_a_stop)
{
  auto handle = send(raw_goal({ { 0x0003, 2'000'000 }, { 0x0000, 1'000 } }));
  ASSERT_TRUE(handle);
  ASSERT_TRUE(wait_running());
  stop();
  ASSERT_EQ(result(handle).code, ResultCode::ABORTED);

  ASSERT_TRUE(wait_shots([](const auto& received) { return received.size() >= 2; }));
  std::lock_guard lock{ mutex };
  EXPECT_EQ(shots[1].event, ShotMsg::STOPPED);
  EXPECT_LT(shots[1].elapsed_us, 1'000'000u);
}

/**
 * The node sees the shots of the trigger while idle, and tells them with the
 * sequence it loaded last.
 */
TEST_F(TestFreezerNode, shots_of_the_trigger)
{
  EXPECT_FALSE(press_trigger().success) << "no sequence is loaded yet";

  auto handle = send(raw_goal({ { 0x0003, 1'000 }, { 0x0000, 200'000 } }));
  ASSERT_TRUE(handle);
  const uint16_t node_shot = result(handle).result->shot_id;
  EXPECT_TRUE(press_trigger().success);

  ASSERT_TRUE(wait_shots([](const auto& received) { return received.size() >= 4; }));
  std::lock_guard lock{ mutex };
  EXPECT_EQ(shots[2].event, ShotMsg::STARTED);
  EXPECT_EQ(shots[2].source, ShotMsg::SOURCE_TRIGGER);
  EXPECT_EQ(shots[2].shot_id, node_shot + 1);
  ASSERT_EQ(shots[2].steps.size(), 2u);
  EXPECT_EQ(shots[2].steps[0].outputs, 0x0003);
  EXPECT_EQ(shots[3].event, ShotMsg::ENDED);
  EXPECT_EQ(shots[3].shot_id, node_shot + 1);
}

/** A controller missing when the node starts is connected once it is plugged in. */
TEST_F(TestFreezerNode, connect_a_controller_plugged_in_later)
{
  FakeDriver* late = nullptr;
  const auto unplugged = unplugged_node(late, 0.01);
  EXPECT_FALSE(unplugged->connected());

  late->set_plugged(true);
  EXPECT_TRUE(wait_until([&] { return unplugged->connected(); }));
}

/** With reconnect_period 0, a controller missing at start stays missing, as before. */
TEST_F(TestFreezerNode, no_reconnection_when_turned_off)
{
  FakeDriver* late = nullptr;
  const auto unplugged = unplugged_node(late, 0.0);
  late->set_plugged(true);
  std::this_thread::sleep_for(100ms);
  EXPECT_FALSE(unplugged->connected());
}

/**
 * A controller unplugged is let go: shots and services are refused. Plugged in
 * again, it is connected again, with every output off, and shoots.
 */
TEST_F(TestFreezerNode, reconnect_a_controller_unplugged_and_plugged_in_again)
{
  ASSERT_TRUE(node->connected());
  ASSERT_TRUE(set_outputs(0x0003).success);

  fake->set_plugged(false);
  ASSERT_TRUE(wait_until([this] { return !node->connected(); }));
  EXPECT_EQ(set_outputs(0x0003).message, "No Freezer controller.");
  EXPECT_FALSE(send(Shoot::Goal{})) << "a shot is rejected while the controller is away";

  fake->set_plugged(true);
  ASSERT_TRUE(wait_until([this] { return node->connected(); }));
  EXPECT_TRUE(wait_outputs(0)) << "the Nano resets when its port opens";

  auto handle = send(Shoot::Goal{});
  ASSERT_TRUE(handle);
  EXPECT_EQ(result(handle).code, ResultCode::SUCCEEDED);
}

/**
 * status tells a page that opens late whether the controller is connected:
 * it is latched, and published again every second.
 */
TEST_F(TestFreezerNode, status_topic)
{
  auto listener = std::make_shared<rclcpp::Node>("status_listener");
  std::vector<StatusMsg> statuses;
  auto subscription = listener->create_subscription<StatusMsg>(
      "/freezer/status", rclcpp::QoS{ 1 }.reliable().transient_local(), [&](const StatusMsg& message) {
        std::lock_guard lock{ mutex };
        statuses.push_back(message);
      });
  executor.add_node(listener);

  ASSERT_TRUE(wait_until([&] {
    std::lock_guard lock{ mutex };
    return !statuses.empty();
  }));
  {
    std::lock_guard lock{ mutex };
    EXPECT_TRUE(statuses[0].connected);
    EXPECT_EQ(statuses[0].device, "fake");
    EXPECT_EQ(statuses[0].message, "");
  }
  std::this_thread::sleep_for(2500ms);
  {
    std::lock_guard lock{ mutex };
    EXPECT_GE(statuses.size(), 3u) << "the latched one, then one a second";
  }
  executor.remove_node(listener);
}

/**
 * status follows the controller unplugged and plugged in again, saying why it
 * is away. The node first tells that the controller stopped answering; the
 * tries to connect again that follow may replace the reason with their own
 * error, so every status is kept, not only the last.
 */
TEST_F(TestFreezerNode, status_follows_the_controller)
{
  auto listener = std::make_shared<rclcpp::Node>("status_listener");
  std::vector<StatusMsg> received;
  auto subscription = listener->create_subscription<StatusMsg>(
      "/freezer/status", rclcpp::QoS{ 100 }.reliable().transient_local(), [&](const StatusMsg& message) {
        std::lock_guard lock{ mutex };
        received.push_back(message);
      });
  executor.add_node(listener);
  auto last_status = [&] {
    std::lock_guard lock{ mutex };
    return received.empty() ? std::nullopt : std::optional<StatusMsg>{ received.back() };
  };

  ASSERT_TRUE(wait_until([&] { return last_status() && last_status()->connected; }));

  fake->set_plugged(false);
  ASSERT_TRUE(wait_until([&] { return last_status() && !last_status()->connected; }));
  {
    std::lock_guard lock{ mutex };
    EXPECT_TRUE(std::any_of(received.begin(), received.end(), [](const StatusMsg& status) {
      return !status.connected && status.message.rfind("The Freezer controller stopped answering: ", 0) == 0;
    })) << "the unplug is told with its reason";
  }
  EXPECT_FALSE(last_status()->message.empty());

  fake->set_plugged(true);
  ASSERT_TRUE(wait_until([&] { return last_status() && last_status()->connected; }));
  EXPECT_EQ(last_status()->message, "");
  executor.remove_node(listener);
}

/** A controller missing at start is told on status, with the reason. */
TEST_F(TestFreezerNode, status_of_a_controller_missing_at_start)
{
  FakeDriver* late = nullptr;
  const auto unplugged = unplugged_node(late, 0.0);
  auto listener = std::make_shared<rclcpp::Node>("status_listener");
  std::optional<StatusMsg> last;
  auto subscription = listener->create_subscription<StatusMsg>(
      "/freezer_unplugged/status", rclcpp::QoS{ 1 }.reliable().transient_local(), [&](const StatusMsg& message) {
        std::lock_guard lock{ mutex };
        last = message;
      });
  executor.add_node(listener);

  ASSERT_TRUE(wait_until([&] {
    std::lock_guard lock{ mutex };
    return last.has_value();
  }));
  std::lock_guard lock{ mutex };
  EXPECT_FALSE(last->connected);
  EXPECT_FALSE(last->message.empty());
  EXPECT_NE(last->message, "Not connected yet.") << "the reason of the failed connection";
  executor.remove_node(listener);
}

}  // namespace freezer_node::test
