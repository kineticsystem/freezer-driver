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

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <freezer_driver/driver.hpp>
#include <freezer_driver/sequence.hpp>
#include <freezer_driver/shot_runner.hpp>
#include <freezer_driver/synchronized_driver.hpp>
#include <freezer_msgs/action/shoot.hpp>
#include <freezer_msgs/msg/controller_status.hpp>
#include <freezer_msgs/msg/outputs.hpp>
#include <freezer_msgs/msg/shot.hpp>
#include <freezer_msgs/srv/set_outputs.hpp>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <std_srvs/srv/trigger.hpp>

namespace freezer_node
{
/**
 * @brief The ROS2 node of the Freezer board: a Shoot action server, the
 * services set_outputs and stop, and the topics outputs and shots.
 *
 * A goal names a sequence of the node parameters, or carries a raw table. The
 * node checks it, then hands it to a ShotRunner, which loads it into the
 * controller, fires it and polls the controller until the shot has ended, and
 * turns what the runner reports into feedback and a result. One shot runs at
 * a time, on a thread of its own.
 *
 * set_outputs latches a pattern outside a shot, e.g. the lights; stop ends
 * the running shot, whose goal then aborts, and switches every output off.
 * They call the controller from the executor's thread, between two queries of
 * the shot's thread: the driver is shared, one call at a time.
 *
 * outputs and shots tell what the board does, for a client that shows it.
 * While no goal runs, the node polls the controller every watch_period, to see
 * the shots of the remote trigger, IN1. With a fake controller, the service
 * fake/press_trigger presses IN1.
 *
 * The controller can come and go: a Nano missing when the node starts, or
 * unplugged later, is connected again once it is back. Every reconnect_period,
 * a thread of its own tries to connect while there is no controller; watch()
 * lets the controller go when it stops answering. Meanwhile, every shot and
 * every service is refused, as with no controller at all.
 *
 * status tells whether the controller is connected, and why not: latched,
 * published when it changes and every second.
 */
class FreezerNode : public rclcpp::Node
{
public:
  using Shoot = freezer_msgs::action::Shoot;
  using GoalHandle = rclcpp_action::ServerGoalHandle<Shoot>;
  using SetOutputs = freezer_msgs::srv::SetOutputs;
  using Trigger = std_srvs::srv::Trigger;
  using OutputsMsg = freezer_msgs::msg::Outputs;
  using ShotMsg = freezer_msgs::msg::Shot;
  using StatusMsg = freezer_msgs::msg::ControllerStatus;

  /**
   * @brief Create the node with the driver chosen by the parameter use_fake.
   */
  explicit FreezerNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions{});

  /**
   * @brief Create the node with the given driver, e.g. a fake one a test
   * controls.
   */
  FreezerNode(const rclcpp::NodeOptions& options, std::unique_ptr<freezer_driver::Driver> driver);

  ~FreezerNode() override;

  /** True when the handshake with the controller succeeded, and it still answers. */
  bool connected() const;

private:
  void declare_sequence_parameters();
  std::unique_ptr<freezer_driver::Driver> create_driver();

  /** Connect to the controller, and read its limits: true when it answered. */
  bool connect();

  /** Close the port, and refuse every shot until connect() succeeds again; message says why. */
  void disconnect(const std::string& message);

  /** Record whether the controller is connected, and publish it when it changed. */
  void set_status(bool connected, const std::string& message);
  void publish_status();

  /** Try connect() every reconnect_period while there is no controller, until the node ends. */
  void reconnect_loop(std::chrono::duration<double> period);

  /** The limits of the controller, read when it connected. */
  freezer_driver::SequenceLimits limits() const;

  /**
   * Build the table of a goal, from its raw steps or from the sequence it
   * names.
   * @throw std::invalid_argument for an unknown sequence or a bad parameter.
   */
  freezer_driver::Sequence build_sequence(const Shoot::Goal& goal);

  rclcpp_action::GoalResponse handle_goal(const rclcpp_action::GoalUUID& uuid, std::shared_ptr<const Shoot::Goal> goal);
  rclcpp_action::CancelResponse handle_cancel(std::shared_ptr<GoalHandle> goal_handle);
  void handle_accepted(std::shared_ptr<GoalHandle> goal_handle);

  void execute(const std::shared_ptr<GoalHandle>& goal_handle);

  void handle_set_outputs(const std::shared_ptr<SetOutputs::Request>& request,
                          const std::shared_ptr<SetOutputs::Response>& response);
  void handle_stop(const std::shared_ptr<Trigger::Response>& response);
  void handle_press_trigger(const std::shared_ptr<Trigger::Response>& response);

  /** Poll the controller while no goal runs, for the shots of the trigger. */
  void watch();

  void publish_outputs(uint16_t outputs, uint16_t shot_id = 0, uint8_t step = 0);

  /** A shot event, with the steps of the sequence when known. */
  ShotMsg shot_message(uint8_t event, uint8_t source, uint16_t shot_id, const rclcpp::Time& started,
                       const std::optional<freezer_driver::Sequence>& sequence);

  std::unique_ptr<freezer_driver::Driver> driver_;
  // The same driver as driver_, as the SynchronizedDriver it is.
  freezer_driver::SynchronizedDriver* synchronized_ = nullptr;
  std::unique_ptr<freezer_driver::ShotRunner> runner_;
  // Written by the thread that connects, read by the services and the shot's thread.
  mutable std::mutex limits_mutex_;
  freezer_driver::SequenceLimits limits_;
  std::atomic<bool> connected_{ false };
  // A failed connection was logged: the next ones go to the debug log only.
  bool failure_reported_ = false;
  // The status queries of watch() that failed in a row.
  int failed_polls_ = 0;

  // Connects again while there is no controller; ends with the node.
  std::thread reconnector_;
  std::mutex reconnect_mutex_;
  std::condition_variable reconnect_condition_;
  bool closing_ = false;

  std::atomic<bool> busy_{ false };

  // A goal can be canceled until the shot starts, never after; it can be
  // stopped at any time.
  std::mutex start_mutex_;
  bool started_ = false;
  bool cancel_requested_ = false;
  bool stop_requested_ = false;
  // When the last stop was sent, to tell how long a stopped shot ran.
  std::optional<rclcpp::Time> stop_time_;
  // The id of the last shot the node knows of, its own or the trigger's.
  uint16_t known_shot_id_ = 0;

  /** A shot of the trigger, followed by watch() until it ends. */
  struct WatchedShot
  {
    uint16_t id;
    rclcpp::Time started;
    std::optional<freezer_driver::Sequence> sequence;
    uint8_t step;
  };
  // Used by the executor's thread only.
  std::optional<WatchedShot> watched_;

  std::thread worker_;
  rclcpp_action::Server<Shoot>::SharedPtr action_server_;
  rclcpp::Service<SetOutputs>::SharedPtr set_outputs_service_;
  rclcpp::Service<Trigger>::SharedPtr stop_service_;
  rclcpp::Service<Trigger>::SharedPtr press_trigger_service_;
  rclcpp::Publisher<OutputsMsg>::SharedPtr outputs_publisher_;
  rclcpp::Publisher<ShotMsg>::SharedPtr shots_publisher_;
  rclcpp::TimerBase::SharedPtr watch_timer_;
  rclcpp::Publisher<StatusMsg>::SharedPtr status_publisher_;
  rclcpp::TimerBase::SharedPtr status_timer_;
  // The serial port of the controller, or "fake".
  std::string device_;
  // The status last set, and why the controller is not connected; written by
  // the thread that connects and by the executor's.
  std::mutex status_mutex_;
  bool status_connected_ = false;
  std::string status_message_;
};
}  // namespace freezer_node
