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

#include <freezer_driver/fake/fake_driver.hpp>

namespace freezer_driver
{
FakeDriver::FakeDriver(Clock clock) : clock_{ std::move(clock) }
{
}

std::chrono::microseconds FakeDriver::steady_clock()
{
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

bool FakeDriver::connect()
{
  check_plugged();
  return true;
}

void FakeDriver::disconnect()
{
}

InfoResponse FakeDriver::get_info()
{
  check_plugged();
  InfoResponse response{ Response::Status::Success };
  response.version = Version{ 2, 0, 0 };
  response.limits = kLimits;
  response.name = "FREEZER";
  return response;
}

LoadSequenceResponse FakeDriver::load_sequence(const Sequence& sequence)
{
  check_plugged();
  ++loads_;
  update();
  if (running_)
  {
    return LoadSequenceResponse{ Response::Status::Failure, Response::Reason::Busy };
  }
  if (validate(sequence, kLimits))
  {
    return LoadSequenceResponse{ Response::Status::Failure, Response::Reason::InvalidTable };
  }
  loaded_ = sequence;
  LoadSequenceResponse response{ Response::Status::Success };
  response.duration_us = static_cast<uint32_t>(sequence.duration_us());
  return response;
}

ShootResponse FakeDriver::shoot()
{
  check_plugged();
  update();
  if (running_)
  {
    return ShootResponse{ Response::Status::Failure, Response::Reason::Busy };
  }
  if (!loaded_)
  {
    return ShootResponse{ Response::Status::Failure, Response::Reason::NoTable };
  }

  start();
  ShootResponse response{ Response::Status::Success };
  response.shot_id = last_shot_id_;
  response.duration_us = static_cast<uint32_t>(loaded_->duration_us());
  return response;
}

Response FakeDriver::set_outputs(uint16_t outputs)
{
  check_plugged();
  update();
  if (running_)
  {
    return Response{ Response::Status::Failure, Response::Reason::Busy };
  }
  latch_now(outputs);
  return Response{ Response::Status::Success };
}

Response FakeDriver::stop()
{
  check_plugged();
  update();
  running_ = false;
  latch_now(0);
  return Response{ Response::Status::Success };
}

bool FakeDriver::trigger()
{
  update();
  if (running_ || !loaded_)
  {
    return false;
  }
  start();
  return true;
}

uint16_t FakeDriver::outputs() const
{
  const auto now = clock_();
  uint16_t outputs = 0;
  for (const auto& latch : timeline_)
  {
    if (latch.time > now)
    {
      break;
    }
    outputs = latch.outputs;
  }
  return outputs;
}

StatusResponse FakeDriver::get_status()
{
  check_plugged();
  if (running_ && failure_ == Failure::ResetDuringShot)
  {
    reset();
  }
  update();

  StatusResponse response{ Response::Status::Success };
  response.last_shot_id = last_shot_id_;
  response.worst_lateness_us = worst_lateness_us_;
  if (running_)
  {
    response.state = StatusResponse::State::Running;
    const auto elapsed = clock_() - start_;
    response.elapsed_us = static_cast<uint32_t>(elapsed.count());
    uint64_t end = 0;
    for (const auto& step : loaded_->steps())
    {
      end += step.hold_us;
      if (static_cast<uint64_t>(elapsed.count()) < end)
      {
        break;
      }
      ++response.step;
    }
  }
  return response;
}

void FakeDriver::set_failure(Failure failure)
{
  failure_ = failure;
}

void FakeDriver::set_plugged(bool plugged)
{
  if (plugged && !plugged_)
  {
    reset();
  }
  plugged_ = plugged;
}

void FakeDriver::check_plugged() const
{
  if (!plugged_)
  {
    throw std::runtime_error("No controller on the serial port.");
  }
}

void FakeDriver::set_lateness(uint32_t lateness_us)
{
  lateness_us_ = lateness_us;
}

const std::vector<FakeDriver::Latch>& FakeDriver::timeline() const
{
  return timeline_;
}

int FakeDriver::loads() const
{
  return loads_;
}

void FakeDriver::reset()
{
  running_ = false;
  loaded_.reset();
  next_shot_id_ = 1;
  last_shot_id_ = 0;
  worst_lateness_us_ = 0;
}

void FakeDriver::start()
{
  running_ = true;
  start_ = clock_();
  last_shot_id_ = next_shot_id_;
  next_shot_id_ = static_cast<uint16_t>(next_shot_id_ == 0xFFFF ? 1 : next_shot_id_ + 1);

  auto time = start_;
  for (const auto& step : loaded_->steps())
  {
    timeline_.push_back(Latch{ time, step.outputs });
    time += std::chrono::microseconds{ step.hold_us };
  }
}

void FakeDriver::latch_now(uint16_t outputs)
{
  const auto now = clock_();
  while (!timeline_.empty() && timeline_.back().time > now)
  {
    timeline_.pop_back();
  }
  timeline_.push_back(Latch{ now, outputs });
}

void FakeDriver::update()
{
  if (!running_ || failure_ == Failure::NeverFinish)
  {
    return;
  }
  const auto duration = std::chrono::microseconds{ loaded_->duration_us() };
  if (clock_() - start_ >= duration)
  {
    running_ = false;
    worst_lateness_us_ = lateness_us_;
  }
}
}  // namespace freezer_driver
