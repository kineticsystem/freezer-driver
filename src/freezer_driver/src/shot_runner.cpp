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

#include <freezer_driver/shot_runner.hpp>

namespace freezer_driver
{
ShotRunner::ShotRunner(Driver& driver, Config config, Clock clock, Sleep sleep)
  : driver_{ driver }, config_{ config }, clock_{ std::move(clock) }, sleep_{ std::move(sleep) }
{
}

void ShotRunner::load(const Sequence& sequence)
{
  if (loaded_ == sequence)
  {
    return;
  }
  loaded_.reset();
  const auto response = driver_.load_sequence(sequence);
  if (!response.success())
  {
    throw std::runtime_error("The controller refused the sequence: " + to_string(response.reason()) + ".");
  }
  loaded_ = sequence;
}

const std::optional<Sequence>& ShotRunner::loaded() const
{
  return loaded_;
}

ShotRunner::Result ShotRunner::run(const Sequence& sequence)
{
  return run(sequence, Callbacks{});
}

ShotRunner::Result ShotRunner::run(const Sequence& sequence, const Callbacks& callbacks)
{
  Result result;
  try
  {
    ShootResponse shot;
    for (int attempt = 0;; ++attempt)
    {
      load(sequence);
      if (attempt == 0 && !callbacks.start())
      {
        result.outcome = Result::Outcome::Canceled;
        result.message = "Canceled before the shot started.";
        return result;
      }
      shot = driver_.shoot();
      if (shot.success())
      {
        break;
      }
      // The controller lost the sequence, e.g. after a reset: load it again,
      // once.
      const bool lost = shot.reason() == Response::Reason::NoTable;
      loaded_.reset();
      if (!lost || attempt > 0)
      {
        throw std::runtime_error("The controller refused the shot: " + to_string(shot.reason()) + ".");
      }
    }

    result.shot_id = shot.shot_id;
    result.duration_us = shot.duration_us;
    callbacks.started(shot.shot_id, shot.duration_us);

    const auto deadline = clock_() + std::chrono::microseconds{ shot.duration_us } + config_.end_margin;
    while (true)
    {
      sleep_(config_.poll_period);
      const StatusResponse status = driver_.get_status();
      if (!status.success())
      {
        throw std::runtime_error("The controller refused the status query: " + to_string(status.reason()) + ".");
      }
      if (status.state == StatusResponse::State::Running)
      {
        callbacks.progress(status.step, status.elapsed_us);
      }
      else if (status.last_shot_id == shot.shot_id)
      {
        result.outcome = Result::Outcome::Succeeded;
        result.worst_lateness_us = status.worst_lateness_us;
        return result;
      }
      else
      {
        loaded_.reset();
        throw std::runtime_error("The controller lost shot " + std::to_string(shot.shot_id) + ": it may have reset.");
      }
      if (clock_() > deadline)
      {
        throw std::runtime_error("Shot " + std::to_string(shot.shot_id) + " did not end within its " +
                                 std::to_string(shot.duration_us) + " us and the margin.");
      }
    }
  }
  catch (const std::exception& ex)
  {
    result.outcome = Result::Outcome::Aborted;
    result.message = ex.what();
    return result;
  }
}
}  // namespace freezer_driver
