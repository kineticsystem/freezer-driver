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
  return true;
}

void FakeDriver::disconnect()
{
}

InfoResponse FakeDriver::get_info()
{
  InfoResponse response{ Response::Status::Success };
  response.version = Version{ 1, 1, 0 };
  response.limits = kLimits;
  response.name = "FREEZER";
  return response;
}

LoadSequenceResponse FakeDriver::load_sequence(const Sequence& sequence)
{
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
  response.checksum = sequence.checksum();
  response.duration_us = static_cast<uint32_t>(sequence.duration_us());
  return response;
}

ShootResponse FakeDriver::shoot(uint16_t checksum)
{
  update();
  if (running_)
  {
    return ShootResponse{ Response::Status::Failure, Response::Reason::Busy };
  }
  if (!loaded_)
  {
    return ShootResponse{ Response::Status::Failure, Response::Reason::NoTable };
  }
  if (loaded_->checksum() != checksum)
  {
    return ShootResponse{ Response::Status::Failure, Response::Reason::WrongTable };
  }

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

  ShootResponse response{ Response::Status::Success };
  response.shot_id = last_shot_id_;
  response.duration_us = static_cast<uint32_t>(loaded_->duration_us());
  return response;
}

StatusResponse FakeDriver::get_status()
{
  if (running_ && failure_ == Failure::ResetDuringShot)
  {
    // A reset forgets everything: the sequence, the shot and the shot ids.
    running_ = false;
    loaded_.reset();
    next_shot_id_ = 1;
    last_shot_id_ = 0;
    worst_lateness_us_ = 0;
  }
  update();

  StatusResponse response{ Response::Status::Success };
  response.checksum = loaded_ ? loaded_->checksum() : 0;
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

void FakeDriver::set_lateness(uint32_t lateness_us)
{
  lateness_us_ = lateness_us;
}

const std::vector<FakeDriver::Latch>& FakeDriver::timeline() const
{
  return timeline_;
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
