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

#include <stdexcept>
#include <thread>

#include <freezer_driver/default_driver.hpp>

#include <framed_serial/data_utils.hpp>

#include <rclcpp/logging.hpp>

namespace freezer_driver
{
const auto kLogger = rclcpp::get_logger("freezer_default_driver");

constexpr uint8_t kStatusQueryId = 0x75;
constexpr uint8_t kInfoQueryId = 0x76;
constexpr uint8_t kLoadSequenceCommandId = 0x7B;
constexpr uint8_t kShootCommandId = 0x7C;

constexpr int kMaxConnectionTrials = 5;

constexpr auto kExpectedControllerName = "FREEZER";

constexpr uint8_t kExpectedProtocolVersion = 1;

using framed_serial::data_utils::to_hex;

namespace
{
uint16_t to_uint16(const std::vector<uint8_t>& bytes, std::size_t i)
{
  return static_cast<uint16_t>((bytes[i] << 8) | bytes[i + 1]);
}

uint32_t to_uint32(const std::vector<uint8_t>& bytes, std::size_t i)
{
  return (static_cast<uint32_t>(bytes[i]) << 24) | (static_cast<uint32_t>(bytes[i + 1]) << 16) |
         (static_cast<uint32_t>(bytes[i + 2]) << 8) | static_cast<uint32_t>(bytes[i + 3]);
}

/**
 * Read the status byte of a response and, for an error, its reason.
 * @throw std::runtime_error for an empty response or an unknown status byte.
 */
Response read_header(const std::vector<uint8_t>& out)
{
  if (out.empty())
  {
    throw std::runtime_error("The response is empty.");
  }
  if (out[0] == static_cast<uint8_t>(Response::Status::Success))
  {
    return Response{ Response::Status::Success };
  }
  if (out[0] == static_cast<uint8_t>(Response::Status::Failure))
  {
    // A firmware that sends no reason still reports a failure.
    const auto reason = out.size() > 1 ? Response::Reason{ out[1] } : Response::Reason::Malformed;
    return Response{ Response::Status::Failure, reason };
  }
  throw std::runtime_error("Unexpected status byte " + to_hex(std::vector<uint8_t>{ out[0] }) + ".");
}

void check_length(const std::vector<uint8_t>& out, std::size_t length, const char* what)
{
  if (out.size() < length)
  {
    throw std::runtime_error(std::string{ what } + " response is too short: " + to_hex(out) + ".");
  }
}
}  // namespace

DefaultDriver::DefaultDriver(std::unique_ptr<framed_serial::FramedSerial> framed_serial,
                             std::chrono::duration<double> connect_delay)
  : framed_serial_{ std::move(framed_serial) }, connect_delay_{ connect_delay }
{
}

bool DefaultDriver::connect()
{
  framed_serial_->open();

  // The Nano resets when the port opens, and its bootloader listens for an
  // upload before the firmware starts: a frame sent meanwhile is lost.
  std::this_thread::sleep_for(connect_delay_);

  // Send an info query multiple times until an answer comes back, then
  // verify that the device on the other end identifies itself as a Freezer
  // controller: the serial port path alone does not tell us what is attached.
  for (int trial = 0; trial < kMaxConnectionTrials; ++trial)
  {
    try
    {
      RCLCPP_INFO(kLogger, "Connecting (%d of %d)...", trial + 1, kMaxConnectionTrials);
      const InfoResponse response = get_info();
      if (!response.success())
      {
        throw std::runtime_error("Info query failed: " + to_string(response.reason()) + ".");
      }
      if (response.name != kExpectedControllerName)
      {
        // The device answered: it is the wrong one, and asking again will not
        // change that.
        RCLCPP_ERROR(kLogger, "Unexpected device on serial port: expected \"%s\", got \"%s\".", kExpectedControllerName,
                     response.name.c_str());
        return false;
      }
      if (response.version.major() != kExpectedProtocolVersion)
      {
        RCLCPP_ERROR(kLogger,
                     "The Freezer controller runs firmware %s, which speaks version %d of the protocol; this driver "
                     "speaks version %d. Flash the firmware that matches this workspace.",
                     response.version.to_string().c_str(), response.version.major(), kExpectedProtocolVersion);
        return false;
      }
      RCLCPP_INFO(kLogger, "Connection established with %s controller, firmware %s.", response.name.c_str(),
                  response.version.to_string().c_str());
      return true;
    }
    catch (const std::exception& ex)
    {
      RCLCPP_WARN(kLogger, "Connection failed: %s", ex.what());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return false;
}

void DefaultDriver::disconnect()
{
  framed_serial_->close();
}

InfoResponse DefaultDriver::get_info()
{
  const std::vector<uint8_t> in{ kInfoQueryId };
  RCLCPP_DEBUG(kLogger, "Info query: %s", to_hex(in).c_str());
  framed_serial_->write(in);
  const std::vector<uint8_t> out = framed_serial_->read();
  RCLCPP_DEBUG(kLogger, "Info response: %s", to_hex(out).c_str());

  // The data array contains the following information.
  //
  // status               - 1 byte
  // version              - 3 bytes: major, minor and patch
  // max steps            - 1 byte
  // min hold             - 4 bytes, in µs
  // max duration         - 4 bytes, in µs
  // name                 - the remaining bytes, in ASCII

  const Response header = read_header(out);
  InfoResponse response{ header.status(), header.reason() };
  if (!header.success())
  {
    return response;
  }
  check_length(out, 13, "Info");
  response.version = Version{ out[1], out[2], out[3] };
  response.limits = SequenceLimits{ out[4], to_uint32(out, 5), to_uint32(out, 9) };
  response.name = std::string{ out.begin() + 13, out.end() };
  return response;
}

LoadSequenceResponse DefaultDriver::load_sequence(const Sequence& sequence)
{
  std::vector<uint8_t> in{ kLoadSequenceCommandId };
  const std::vector<uint8_t> payload = sequence.encode();
  in.insert(in.end(), payload.begin(), payload.end());
  RCLCPP_DEBUG(kLogger, "Load sequence command: %s", to_hex(in).c_str());
  framed_serial_->write(in);
  const std::vector<uint8_t> out = framed_serial_->read();
  RCLCPP_DEBUG(kLogger, "Load sequence response: %s", to_hex(out).c_str());

  // status - 1 byte, checksum - 2 bytes, duration - 4 bytes in µs.
  const Response header = read_header(out);
  LoadSequenceResponse response{ header.status(), header.reason() };
  if (!header.success())
  {
    return response;
  }
  check_length(out, 7, "Load sequence");
  response.checksum = to_uint16(out, 1);
  response.duration_us = to_uint32(out, 3);
  return response;
}

ShootResponse DefaultDriver::shoot(uint16_t checksum)
{
  const std::vector<uint8_t> in{ kShootCommandId, static_cast<uint8_t>(checksum >> 8),
                                 static_cast<uint8_t>(checksum & 0xFF) };
  RCLCPP_DEBUG(kLogger, "Shoot command: %s", to_hex(in).c_str());
  framed_serial_->write(in);
  const std::vector<uint8_t> out = framed_serial_->read();
  RCLCPP_DEBUG(kLogger, "Shoot response: %s", to_hex(out).c_str());

  // status - 1 byte, shot id - 2 bytes, duration - 4 bytes in µs.
  const Response header = read_header(out);
  ShootResponse response{ header.status(), header.reason() };
  if (!header.success())
  {
    return response;
  }
  check_length(out, 7, "Shoot");
  response.shot_id = to_uint16(out, 1);
  response.duration_us = to_uint32(out, 3);
  return response;
}

StatusResponse DefaultDriver::get_status()
{
  const std::vector<uint8_t> in{ kStatusQueryId };
  RCLCPP_DEBUG(kLogger, "Status query: %s", to_hex(in).c_str());
  framed_serial_->write(in);
  const std::vector<uint8_t> out = framed_serial_->read();
  RCLCPP_DEBUG(kLogger, "Status response: %s", to_hex(out).c_str());

  // status            - 1 byte
  // state             - 1 byte: 0 idle, 1 running
  // checksum          - 2 bytes, of the loaded sequence
  // last shot id      - 2 bytes
  // step              - 1 byte
  // elapsed           - 4 bytes, in µs
  // worst lateness    - 4 bytes, in µs, of the last shot

  const Response header = read_header(out);
  StatusResponse response{ header.status(), header.reason() };
  if (!header.success())
  {
    return response;
  }
  check_length(out, 15, "Status");
  if (out[1] > static_cast<uint8_t>(StatusResponse::State::Running))
  {
    throw std::runtime_error("Unknown controller state " + to_hex(std::vector<uint8_t>{ out[1] }) + ".");
  }
  response.state = StatusResponse::State{ out[1] };
  response.checksum = to_uint16(out, 2);
  response.last_shot_id = to_uint16(out, 4);
  response.step = out[6];
  response.elapsed_us = to_uint32(out, 7);
  response.worst_lateness_us = to_uint32(out, 11);
  return response;
}
}  // namespace freezer_driver
