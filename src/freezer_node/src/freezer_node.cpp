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

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>

#include <freezer_node/freezer_node.hpp>

#include <framed_serial/default_framed_serial.hpp>
#include <framed_serial/default_serial.hpp>
#include <freezer_driver/default_driver.hpp>
#include <freezer_driver/fake/fake_driver.hpp>
#include <freezer_driver/recipes.hpp>

namespace freezer_node
{
using freezer_driver::Response;
using freezer_driver::Sequence;
using freezer_driver::StatusResponse;
using freezer_driver::Step;

namespace
{
constexpr auto kFlashRecipe = "flash";
constexpr auto kTimedLightRecipe = "timed_light";

std::string prefix(const std::string& name)
{
  return "sequences." + name + ".";
}

std::chrono::duration<double> seconds(double value)
{
  return std::chrono::duration<double>{ value };
}
}  // namespace

FreezerNode::FreezerNode(const rclcpp::NodeOptions& options) : FreezerNode(options, nullptr)
{
}

FreezerNode::FreezerNode(const rclcpp::NodeOptions& options, std::unique_ptr<freezer_driver::Driver> driver)
  : rclcpp::Node("freezer", options), driver_{ std::move(driver) }
{
  declare_parameter<bool>("use_fake", true);
  declare_parameter<std::string>("usb_port", "/dev/ttyUSB0");
  declare_parameter<int>("baudrate", 9600);
  declare_parameter<double>("timeout", 0.2);
  declare_parameter<double>("connect_delay", 1.0);
  declare_parameter<double>("poll_period", 0.01);
  declare_parameter<double>("end_margin", 0.1);
  declare_parameter<std::string>("default_sequence", "flash_shot");
  declare_parameter<std::vector<std::string>>("sequence_names", { "flash_shot" });
  declare_sequence_parameters();

  if (!driver_)
  {
    driver_ = create_driver();
  }
  connect();

  action_server_ = rclcpp_action::create_server<Shoot>(
      this, "~/shoot",
      [this](const rclcpp_action::GoalUUID& uuid, std::shared_ptr<const Shoot::Goal> goal) {
        return handle_goal(uuid, std::move(goal));
      },
      [this](std::shared_ptr<GoalHandle> goal_handle) { return handle_cancel(std::move(goal_handle)); },
      [this](std::shared_ptr<GoalHandle> goal_handle) { handle_accepted(std::move(goal_handle)); });
}

FreezerNode::~FreezerNode()
{
  if (worker_.joinable())
  {
    worker_.join();
  }
  if (connected_)
  {
    driver_->disconnect();
  }
}

bool FreezerNode::connected() const
{
  return connected_;
}

void FreezerNode::declare_sequence_parameters()
{
  // A node must declare a parameter before reading it, and the parameters of
  // a sequence depend on its recipe: read the recipe first, then declare the
  // rest. The defaults are the shots of the Freezer sketch and of StepIt Old.
  const auto names = get_parameter("sequence_names").as_string_array();
  for (const auto& name : names)
  {
    const std::string p = prefix(name);
    const auto recipe = declare_parameter<std::string>(p + "recipe", kFlashRecipe);
    if (recipe == kFlashRecipe)
    {
      declare_parameter<std::vector<int64_t>>(p + "cameras", { 1, 2, 3, 4, 5, 6, 7 });
      declare_parameter<std::vector<int64_t>>(p + "flashes", { 8 });
      declare_parameter<double>(p + "focus_ms", 100.0);
      declare_parameter<double>(p + "shutter_to_flash_ms", 100.0);
      declare_parameter<double>(p + "flash_ms", 100.0);
      declare_parameter<double>(p + "cooldown_ms", 200.0);
    }
    else if (recipe == kTimedLightRecipe)
    {
      declare_parameter<std::vector<int64_t>>(p + "cameras", { 1, 2, 3, 4 });
      declare_parameter<std::vector<int64_t>>(p + "lights", { 5, 6, 7, 8 });
      declare_parameter<double>(p + "focus_ms", 100.0);
      declare_parameter<double>(p + "shutter_to_light_ms", 500.0);
      declare_parameter<double>(p + "light_ms", 200.0);
      declare_parameter<double>(p + "light_to_close_ms", 200.0);
      declare_parameter<double>(p + "cooldown_ms", 200.0);
    }
    else
    {
      throw std::invalid_argument("Sequence " + name + " has an unknown recipe \"" + recipe + "\": expected \"" +
                                  kFlashRecipe + "\" or \"" + kTimedLightRecipe + "\".");
    }
  }
}

std::unique_ptr<freezer_driver::Driver> FreezerNode::create_driver()
{
  if (get_parameter("use_fake").as_bool())
  {
    RCLCPP_INFO(get_logger(), "Using a fake Freezer controller.");
    return std::make_unique<freezer_driver::FakeDriver>();
  }
  const auto port = get_parameter("usb_port").as_string();
  RCLCPP_INFO(get_logger(), "Using the Freezer controller on %s.", port.c_str());
  auto serial = std::make_unique<framed_serial::DefaultSerial>();
  serial->set_port(port);
  serial->set_baudrate(static_cast<uint32_t>(get_parameter("baudrate").as_int()));
  serial->set_timeout(seconds(get_parameter("timeout").as_double()));
  auto framed_serial = std::make_unique<framed_serial::DefaultFramedSerial>(std::move(serial));
  return std::make_unique<freezer_driver::DefaultDriver>(std::move(framed_serial),
                                                         seconds(get_parameter("connect_delay").as_double()));
}

void FreezerNode::connect()
{
  try
  {
    if (!driver_->connect())
    {
      RCLCPP_ERROR(get_logger(), "No Freezer controller: every shot will be rejected.");
      return;
    }
    const auto info = driver_->get_info();
    if (!info.success())
    {
      RCLCPP_ERROR(get_logger(), "The Freezer controller did not report its limits.");
      return;
    }
    limits_ = info.limits;
    connected_ = true;
    RCLCPP_INFO(get_logger(), "Sequences of up to %d steps, holds of at least %u us, up to %u us in total.",
                limits_.max_steps, limits_.min_hold_us, limits_.max_duration_us);
  }
  catch (const std::exception& ex)
  {
    RCLCPP_ERROR(get_logger(), "Cannot connect to the Freezer controller: %s", ex.what());
  }
}

Sequence FreezerNode::build_sequence(const Shoot::Goal& goal)
{
  if (!goal.steps.empty())
  {
    std::vector<Step> steps;
    for (const auto& step : goal.steps)
    {
      steps.push_back(Step{ Step::Type{ step.type }, step.outputs, step.hold_us });
    }
    return Sequence{ steps };
  }

  const std::string name = goal.sequence.empty() ? get_parameter("default_sequence").as_string() : goal.sequence;
  const auto names = get_parameter("sequence_names").as_string_array();
  if (std::find(names.begin(), names.end(), name) == names.end())
  {
    throw std::invalid_argument("Unknown sequence \"" + name + "\".");
  }

  const std::string p = prefix(name);
  const auto recipe = get_parameter(p + "recipe").as_string();
  if (recipe == kFlashRecipe)
  {
    return freezer_driver::FlashShotRecipe{
      get_parameter(p + "cameras").as_integer_array(), get_parameter(p + "flashes").as_integer_array(),
      get_parameter(p + "focus_ms").as_double(),       get_parameter(p + "shutter_to_flash_ms").as_double(),
      get_parameter(p + "flash_ms").as_double(),       get_parameter(p + "cooldown_ms").as_double(),
    }
        .build();
  }
  return freezer_driver::TimedLightRecipe{
    get_parameter(p + "cameras").as_integer_array(), get_parameter(p + "lights").as_integer_array(),
    get_parameter(p + "focus_ms").as_double(),       get_parameter(p + "shutter_to_light_ms").as_double(),
    get_parameter(p + "light_ms").as_double(),       get_parameter(p + "light_to_close_ms").as_double(),
    get_parameter(p + "cooldown_ms").as_double(),
  }
      .build();
}

rclcpp_action::GoalResponse FreezerNode::handle_goal(const rclcpp_action::GoalUUID&,
                                                     std::shared_ptr<const Shoot::Goal> goal)
{
  // A rejection carries no reason in ROS2, so the reason goes to the log.
  if (!connected_)
  {
    RCLCPP_ERROR(get_logger(), "Shot rejected: no Freezer controller.");
    return rclcpp_action::GoalResponse::REJECT;
  }
  bool idle = false;
  if (!busy_.compare_exchange_strong(idle, true))
  {
    RCLCPP_WARN(get_logger(), "Shot rejected: another shot is running.");
    return rclcpp_action::GoalResponse::REJECT;
  }
  try
  {
    // The limits are known since the handshake: no serial traffic here.
    if (const auto error = freezer_driver::validate(build_sequence(*goal), limits_))
    {
      throw std::invalid_argument(*error);
    }
  }
  catch (const std::exception& ex)
  {
    busy_ = false;
    RCLCPP_ERROR(get_logger(), "Shot rejected: %s", ex.what());
    return rclcpp_action::GoalResponse::REJECT;
  }
  return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
}

rclcpp_action::CancelResponse FreezerNode::handle_cancel(std::shared_ptr<GoalHandle>)
{
  std::lock_guard lock{ start_mutex_ };
  if (started_)
  {
    // A shot that has started always runs to the end.
    return rclcpp_action::CancelResponse::REJECT;
  }
  cancel_requested_ = true;
  return rclcpp_action::CancelResponse::ACCEPT;
}

void FreezerNode::handle_accepted(std::shared_ptr<GoalHandle> goal_handle)
{
  // The previous shot is over, since busy_ let this one in: its thread is
  // ending or has ended.
  if (worker_.joinable())
  {
    worker_.join();
  }
  worker_ = std::thread{ [this, goal_handle] { execute(goal_handle); } };
}

void FreezerNode::load(const Sequence& sequence)
{
  const uint16_t checksum = sequence.checksum();
  if (loaded_checksum_ == checksum)
  {
    return;
  }
  loaded_checksum_.reset();
  const auto response = driver_->load_sequence(sequence);
  if (!response.success())
  {
    throw std::runtime_error("The controller refused the sequence: " + to_string(response.reason()) + ".");
  }
  if (response.checksum != checksum)
  {
    throw std::runtime_error("The controller computed the checksum " + std::to_string(response.checksum) +
                             " for a sequence whose checksum is " + std::to_string(checksum) + ".");
  }
  loaded_checksum_ = checksum;
}

void FreezerNode::execute(const std::shared_ptr<GoalHandle>& goal_handle)
{
  auto result = std::make_shared<Shoot::Result>();
  auto feedback = std::make_shared<Shoot::Feedback>();
  try
  {
    feedback->state = Shoot::Feedback::LOADING;
    goal_handle->publish_feedback(feedback);

    const Sequence sequence = build_sequence(*goal_handle->get_goal());
    const uint16_t checksum = sequence.checksum();

    freezer_driver::ShootResponse shot;
    for (int attempt = 0;; ++attempt)
    {
      load(sequence);
      {
        std::lock_guard lock{ start_mutex_ };
        if (cancel_requested_)
        {
          // The server marks the goal as canceling right after handle_cancel
          // returns; wait for it before reporting the cancellation.
          while (!goal_handle->is_canceling())
          {
            std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
          }
          result->message = "Canceled before the shot started.";
          goal_handle->canceled(result);
          cancel_requested_ = false;
          busy_ = false;
          return;
        }
        started_ = true;
      }
      shot = driver_->shoot(checksum);
      if (shot.success())
      {
        break;
      }
      // The controller lost the sequence, e.g. after a reset: load it again,
      // once.
      const bool lost = shot.reason() == Response::Reason::NoTable || shot.reason() == Response::Reason::WrongTable;
      loaded_checksum_.reset();
      if (!lost || attempt > 0)
      {
        throw std::runtime_error("The controller refused the shot: " + to_string(shot.reason()) + ".");
      }
      std::lock_guard lock{ start_mutex_ };
      started_ = false;
    }

    result->started = now();
    result->shot_id = shot.shot_id;
    result->duration_us = shot.duration_us;
    feedback->state = Shoot::Feedback::RUNNING;
    feedback->shot_id = shot.shot_id;
    feedback->duration_us = shot.duration_us;
    goal_handle->publish_feedback(feedback);

    // The controller is the authority on the end of the shot. The host knows
    // its duration, but only as a deadline: the controller clock, the USB
    // latency or a reset can each make the prediction wrong.
    const auto poll_period = seconds(get_parameter("poll_period").as_double());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds{ shot.duration_us } +
                          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              seconds(get_parameter("end_margin").as_double()));
    while (true)
    {
      std::this_thread::sleep_for(poll_period);
      const StatusResponse status = driver_->get_status();
      if (!status.success())
      {
        throw std::runtime_error("The controller refused the status query: " + to_string(status.reason()) + ".");
      }
      if (status.state == StatusResponse::State::Running)
      {
        feedback->step = status.step;
        feedback->elapsed_us = status.elapsed_us;
        goal_handle->publish_feedback(feedback);
      }
      else if (status.last_shot_id == shot.shot_id)
      {
        result->worst_lateness_us = status.worst_lateness_us;
        goal_handle->succeed(result);
        break;
      }
      else
      {
        loaded_checksum_.reset();
        throw std::runtime_error("The controller lost shot " + std::to_string(shot.shot_id) + ": it may have reset.");
      }
      if (std::chrono::steady_clock::now() > deadline)
      {
        throw std::runtime_error("Shot " + std::to_string(shot.shot_id) + " did not end within its " +
                                 std::to_string(shot.duration_us) + " us and the margin.");
      }
    }
  }
  catch (const std::exception& ex)
  {
    RCLCPP_ERROR(get_logger(), "Shot failed: %s", ex.what());
    result->message = ex.what();
    goal_handle->abort(result);
  }

  std::lock_guard lock{ start_mutex_ };
  started_ = false;
  cancel_requested_ = false;
  busy_ = false;
}
}  // namespace freezer_node
