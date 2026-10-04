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
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include <freezer_node/freezer_node.hpp>

#include <framed_serial/default_framed_serial.hpp>
#include <framed_serial/default_serial.hpp>
#include <freezer_driver/default_driver.hpp>
#include <freezer_driver/fake/fake_driver.hpp>
#include <freezer_driver/recipes.hpp>
#include <freezer_driver/synchronized_driver.hpp>

namespace freezer_node
{
using freezer_driver::Response;
using freezer_driver::Sequence;
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

/**
 * The topics keep their last message for a client that subscribes late. One
 * only: rosbridge passes a new client the first message a topic kept, so with
 * more a page would open on the oldest.
 */
rclcpp::QoS latched()
{
  return rclcpp::QoS{ 1 }.reliable().transient_local();
}

uint32_t microseconds_between(const rclcpp::Time& from, const rclcpp::Time& to)
{
  const int64_t ns = (to - from).nanoseconds();
  return ns <= 0 ? 0 : static_cast<uint32_t>(std::min<int64_t>(ns / 1000, UINT32_MAX));
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
  declare_parameter<double>("watch_period", 0.2);
  declare_parameter<std::string>("default_sequence", "flash_shot");
  declare_parameter<std::vector<std::string>>("sequence_names", { "flash_shot" });
  declare_sequence_parameters();

  if (!driver_)
  {
    driver_ = create_driver();
  }
  // The services call the driver while the shot's thread polls it.
  auto synchronized = std::make_unique<freezer_driver::SynchronizedDriver>(std::move(driver_));
  synchronized_ = synchronized.get();
  driver_ = std::move(synchronized);

  outputs_publisher_ = create_publisher<OutputsMsg>("~/outputs", latched());
  shots_publisher_ = create_publisher<ShotMsg>("~/shots", latched());

  connect();
  // The Nano resets when the port opens: every output is off.
  publish_outputs(0);

  // poll_period and end_margin are read here, once.
  const freezer_driver::ShotRunner::Config config{
    std::chrono::duration_cast<std::chrono::microseconds>(seconds(get_parameter("poll_period").as_double())),
    std::chrono::duration_cast<std::chrono::microseconds>(seconds(get_parameter("end_margin").as_double())),
  };
  runner_ = std::make_unique<freezer_driver::ShotRunner>(
      *driver_, config,
      [] {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
      },
      [](std::chrono::microseconds period) { std::this_thread::sleep_for(period); });

  action_server_ = rclcpp_action::create_server<Shoot>(
      this, "~/shoot",
      [this](const rclcpp_action::GoalUUID& uuid, std::shared_ptr<const Shoot::Goal> goal) {
        return handle_goal(uuid, std::move(goal));
      },
      [this](std::shared_ptr<GoalHandle> goal_handle) { return handle_cancel(std::move(goal_handle)); },
      [this](std::shared_ptr<GoalHandle> goal_handle) { handle_accepted(std::move(goal_handle)); });

  set_outputs_service_ =
      create_service<SetOutputs>("~/set_outputs", [this](const std::shared_ptr<SetOutputs::Request> request,
                                                         std::shared_ptr<SetOutputs::Response> response) {
        handle_set_outputs(request, response);
      });
  stop_service_ =
      create_service<Trigger>("~/stop", [this](const std::shared_ptr<Trigger::Request>,
                                               std::shared_ptr<Trigger::Response> response) { handle_stop(response); });

  if (dynamic_cast<const freezer_driver::FakeDriver*>(&synchronized_->driver()))
  {
    press_trigger_service_ =
        create_service<Trigger>("~/fake/press_trigger", [this](const std::shared_ptr<Trigger::Request>,
                                                               std::shared_ptr<Trigger::Response> response) {
          handle_press_trigger(response);
        });
  }

  const double watch_period = get_parameter("watch_period").as_double();
  if (watch_period > 0.0)
  {
    watch_timer_ = create_wall_timer(std::chrono::duration_cast<std::chrono::nanoseconds>(seconds(watch_period)),
                                     [this] { watch(); });
  }
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
  // rest. The defaults are the shots of the Freezer sketch and of the old firmware.
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

void FreezerNode::execute(const std::shared_ptr<GoalHandle>& goal_handle)
{
  using Outcome = freezer_driver::ShotRunner::Result::Outcome;

  auto result = std::make_shared<Shoot::Result>();
  auto feedback = std::make_shared<Shoot::Feedback>();
  feedback->state = Shoot::Feedback::LOADING;
  goal_handle->publish_feedback(feedback);

  freezer_driver::ShotRunner::Result shot;
  std::optional<Sequence> sequence;
  std::optional<rclcpp::Time> started;
  uint8_t last_step = 0;
  try
  {
    sequence = build_sequence(*goal_handle->get_goal());

    freezer_driver::ShotRunner::Callbacks callbacks;
    callbacks.start = [this] {
      std::lock_guard lock{ start_mutex_ };
      started_ = !cancel_requested_ && !stop_requested_;
      return started_;
    };
    callbacks.started = [&](uint16_t shot_id, uint32_t duration_us) {
      {
        // A stop that came between the check above and Shoot found nothing
        // to stop: stop the shot now.
        std::lock_guard lock{ start_mutex_ };
        if (stop_requested_)
        {
          driver_->stop();
        }
      }
      result->started = now();
      started = result->started;
      shots_publisher_->publish(
          shot_message(ShotMsg::STARTED, ShotMsg::SOURCE_NODE, shot_id, result->started, sequence));
      publish_outputs(sequence->steps().front().outputs, shot_id, 0);
      feedback->state = Shoot::Feedback::RUNNING;
      feedback->shot_id = shot_id;
      feedback->duration_us = duration_us;
      goal_handle->publish_feedback(feedback);
    };
    callbacks.progress = [&](uint8_t step, uint32_t elapsed_us) {
      if (step != last_step && step < sequence->steps().size())
      {
        last_step = step;
        publish_outputs(sequence->steps()[step].outputs, feedback->shot_id, step);
      }
      feedback->step = step;
      feedback->elapsed_us = elapsed_us;
      goal_handle->publish_feedback(feedback);
    };
    shot = runner_->run(*sequence, callbacks);
  }
  catch (const std::exception& ex)
  {
    // The parameters of the sequence changed since the goal was accepted.
    shot.outcome = Outcome::Aborted;
    shot.message = ex.what();
  }

  bool stopped = false;
  std::optional<rclcpp::Time> stop_time;
  {
    // A stopped shot ends as one that ran to its end, or as one canceled
    // before it started: the node alone knows it was stopped.
    std::lock_guard lock{ start_mutex_ };
    stop_time = stop_time_;
    if (stop_requested_ && shot.outcome == Outcome::Succeeded)
    {
      shot.outcome = Outcome::Aborted;
      shot.message = "Stopped.";
      stopped = true;
    }
    else if (stop_requested_ && shot.outcome == Outcome::Canceled && !cancel_requested_)
    {
      shot.outcome = Outcome::Aborted;
      shot.message = "Stopped before the shot started.";
    }
  }

  if (started)
  {
    // The shot started: tell how it ended.
    auto message = shot_message(ShotMsg::ENDED, ShotMsg::SOURCE_NODE, shot.shot_id, *started, sequence);
    if (stopped)
    {
      message.event = ShotMsg::STOPPED;
      message.elapsed_us = std::min(message.duration_us, stop_time ? microseconds_between(*started, *stop_time) : 0u);
    }
    else if (shot.outcome == Outcome::Succeeded)
    {
      message.elapsed_us = message.duration_us;
      message.worst_lateness_us = shot.worst_lateness_us;
    }
    else
    {
      message.event = ShotMsg::FAILED;
      message.message = shot.message;
    }
    shots_publisher_->publish(message);
    if (message.event != ShotMsg::FAILED)
    {
      publish_outputs(0);
    }
  }

  result->message = shot.message;
  result->shot_id = shot.shot_id;
  result->duration_us = shot.duration_us;
  result->worst_lateness_us = shot.worst_lateness_us;
  switch (shot.outcome)
  {
    case Outcome::Succeeded:
      goal_handle->succeed(result);
      break;
    case Outcome::Canceled:
      // The server marks the goal as canceling right after handle_cancel
      // returns; wait for it before reporting the cancellation.
      while (!goal_handle->is_canceling())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
      }
      goal_handle->canceled(result);
      break;
    case Outcome::Aborted:
      RCLCPP_ERROR(get_logger(), "Shot failed: %s", shot.message.c_str());
      goal_handle->abort(result);
      break;
  }

  std::lock_guard lock{ start_mutex_ };
  if (shot.shot_id != 0)
  {
    known_shot_id_ = shot.shot_id;
  }
  started_ = false;
  cancel_requested_ = false;
  stop_requested_ = false;
  busy_ = false;
}

void FreezerNode::handle_set_outputs(const std::shared_ptr<SetOutputs::Request>& request,
                                     const std::shared_ptr<SetOutputs::Response>& response)
{
  if (!connected_)
  {
    response->message = "No Freezer controller.";
    return;
  }
  try
  {
    const Response answer = driver_->set_outputs(request->outputs);
    response->success = answer.success();
    if (answer.success())
    {
      publish_outputs(request->outputs);
    }
    if (!answer.success())
    {
      response->message = "The controller refused the outputs: " + to_string(answer.reason()) + ".";
    }
  }
  catch (const std::exception& ex)
  {
    response->message = ex.what();
  }
  if (!response->success)
  {
    RCLCPP_WARN(get_logger(), "Outputs not set: %s", response->message.c_str());
  }
}

void FreezerNode::handle_stop(const std::shared_ptr<Trigger::Response>& response)
{
  if (!connected_)
  {
    response->message = "No Freezer controller.";
    return;
  }
  {
    // The goal running, if any, aborts; a goal accepted after the stop runs.
    std::lock_guard lock{ start_mutex_ };
    if (busy_)
    {
      stop_requested_ = true;
    }
    stop_time_ = now();
  }
  try
  {
    const Response answer = driver_->stop();
    response->success = answer.success();
    if (answer.success())
    {
      publish_outputs(0);
    }
    if (!answer.success())
    {
      response->message = "The controller refused to stop: " + to_string(answer.reason()) + ".";
    }
  }
  catch (const std::exception& ex)
  {
    response->message = ex.what();
  }
  if (!response->success)
  {
    RCLCPP_ERROR(get_logger(), "Stop failed: %s", response->message.c_str());
  }
}
void FreezerNode::handle_press_trigger(const std::shared_ptr<Trigger::Response>& response)
{
  response->success = synchronized_->with_driver(
      [](freezer_driver::Driver& driver) { return dynamic_cast<freezer_driver::FakeDriver&>(driver).trigger(); });
  if (!response->success)
  {
    response->message = "Nothing fired: a shot is running, or no sequence is loaded.";
  }
}

void FreezerNode::publish_outputs(uint16_t outputs, uint16_t shot_id, uint8_t step)
{
  OutputsMsg message;
  message.stamp = now();
  message.outputs = outputs;
  message.shot_id = shot_id;
  message.step = step;
  outputs_publisher_->publish(message);
}

FreezerNode::ShotMsg FreezerNode::shot_message(uint8_t event, uint8_t source, uint16_t shot_id,
                                               const rclcpp::Time& started, const std::optional<Sequence>& sequence)
{
  ShotMsg message;
  message.event = event;
  message.source = source;
  message.shot_id = shot_id;
  message.started = started;
  if (sequence)
  {
    for (const auto& step : sequence->steps())
    {
      freezer_msgs::msg::Step out;
      out.type = static_cast<uint8_t>(step.type);
      out.outputs = step.outputs;
      out.hold_us = step.hold_us;
      message.steps.push_back(out);
    }
    message.duration_us = static_cast<uint32_t>(sequence->duration_us());
  }
  return message;
}

void FreezerNode::watch()
{
  if (!connected_ || busy_)
  {
    return;
  }
  freezer_driver::StatusResponse status;
  try
  {
    status = driver_->get_status();
  }
  catch (const std::exception& ex)
  {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10'000, "Cannot read the controller's status: %s", ex.what());
    return;
  }
  if (!status.success())
  {
    return;
  }

  uint16_t known = 0;
  std::optional<rclcpp::Time> stop_time;
  {
    std::lock_guard lock{ start_mutex_ };
    if (busy_)
    {
      // A goal started meanwhile: the shot the status tells of is its own.
      return;
    }
    known = known_shot_id_;
    stop_time = stop_time_;
  }
  const bool running = status.state == freezer_driver::StatusResponse::State::Running;
  const uint16_t id = status.last_shot_id;
  const rclcpp::Time time = now();

  const auto end_watched = [&](uint8_t event, const std::string& why) {
    auto message = shot_message(event, ShotMsg::SOURCE_TRIGGER, watched_->id, watched_->started, watched_->sequence);
    if (event == ShotMsg::ENDED)
    {
      message.elapsed_us = message.duration_us;
      message.worst_lateness_us = status.worst_lateness_us;
      // A stop sent while the shot ran ended it.
      if (stop_time && *stop_time > watched_->started)
      {
        message.event = ShotMsg::STOPPED;
        message.elapsed_us = std::min(message.duration_us, microseconds_between(watched_->started, *stop_time));
        message.worst_lateness_us = 0;
      }
    }
    message.message = why;
    shots_publisher_->publish(message);
    publish_outputs(0);
    watched_.reset();
  };

  if (watched_ && watched_->id == id)
  {
    if (running)
    {
      if (status.step != watched_->step && watched_->sequence && status.step < watched_->sequence->steps().size())
      {
        watched_->step = status.step;
        publish_outputs(watched_->sequence->steps()[status.step].outputs, id, status.step);
      }
      return;
    }
    end_watched(ShotMsg::ENDED, "");
  }
  else if (watched_)
  {
    end_watched(ShotMsg::FAILED, "The controller lost the shot: it may have reset.");
  }

  if (id == known)
  {
    return;
  }
  {
    std::lock_guard lock{ start_mutex_ };
    known_shot_id_ = id;
  }
  if (id == 0)
  {
    // The controller reset: its shot ids start again.
    return;
  }

  // A shot the node did not fire: the trigger's, of the sequence loaded last.
  const auto sequence = runner_->loaded();
  if (running)
  {
    const rclcpp::Time started = time - rclcpp::Duration::from_nanoseconds(int64_t{ status.elapsed_us } * 1000);
    watched_ = WatchedShot{ id, started, sequence, status.step };
    shots_publisher_->publish(shot_message(ShotMsg::STARTED, ShotMsg::SOURCE_TRIGGER, id, started, sequence));
    if (sequence && status.step < sequence->steps().size())
    {
      publish_outputs(sequence->steps()[status.step].outputs, id, status.step);
    }
    return;
  }
  // It started and ended between two polls: it ended about now.
  const int64_t duration_ns = sequence ? static_cast<int64_t>(sequence->duration_us()) * 1000 : 0;
  const rclcpp::Time started = time - rclcpp::Duration::from_nanoseconds(duration_ns);
  shots_publisher_->publish(shot_message(ShotMsg::STARTED, ShotMsg::SOURCE_TRIGGER, id, started, sequence));
  watched_ = WatchedShot{ id, started, sequence, 0 };
  end_watched(ShotMsg::ENDED, "");
}
}  // namespace freezer_node
