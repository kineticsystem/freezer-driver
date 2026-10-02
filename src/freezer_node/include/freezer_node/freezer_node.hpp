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
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <freezer_driver/driver.hpp>
#include <freezer_driver/sequence.hpp>
#include <freezer_msgs/action/shoot.hpp>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

namespace freezer_node
{
/**
 * @brief The ROS2 node of the Freezer board: a Shoot action server.
 *
 * A goal names a sequence of the node parameters, or carries a raw table. The
 * node loads the table into the controller when it is not already there,
 * fires it, and polls the controller until the shot has ended. One shot runs
 * at a time, on a thread that owns the driver.
 */
class FreezerNode : public rclcpp::Node
{
public:
  using Shoot = freezer_msgs::action::Shoot;
  using GoalHandle = rclcpp_action::ServerGoalHandle<Shoot>;

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

  /** True when the handshake with the controller succeeded. */
  bool connected() const;

private:
  void declare_sequence_parameters();
  std::unique_ptr<freezer_driver::Driver> create_driver();
  void connect();

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

  /** Load the sequence into the controller, unless it is already there. */
  void load(const freezer_driver::Sequence& sequence);

  std::unique_ptr<freezer_driver::Driver> driver_;
  freezer_driver::SequenceLimits limits_;
  bool connected_ = false;

  // What the node believes is loaded in the controller. Reset whenever the
  // controller may have lost it, so the next shot loads it again.
  std::optional<uint16_t> loaded_checksum_;

  std::atomic<bool> busy_{ false };

  // A goal can be canceled until the shot starts, never after.
  std::mutex start_mutex_;
  bool started_ = false;
  bool cancel_requested_ = false;

  std::thread worker_;
  rclcpp_action::Server<Shoot>::SharedPtr action_server_;
};
}  // namespace freezer_node
