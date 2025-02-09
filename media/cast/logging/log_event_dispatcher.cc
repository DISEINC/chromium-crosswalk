// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cast/logging/log_event_dispatcher.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/synchronization/waitable_event.h"
#include "media/cast/cast_environment.h"

namespace media {
namespace cast {

LogEventDispatcher::LogEventDispatcher(CastEnvironment* env)
    : env_(env), impl_(new Impl()) {
  DCHECK(env_);
}

LogEventDispatcher::~LogEventDispatcher() {}

void LogEventDispatcher::DispatchFrameEvent(
    std::unique_ptr<FrameEvent> event) const {
  if (env_->CurrentlyOn(CastEnvironment::MAIN)) {
    impl_->DispatchFrameEvent(std::move(event));
  } else {
    env_->PostTask(CastEnvironment::MAIN, FROM_HERE,
                   base::Bind(&LogEventDispatcher::Impl::DispatchFrameEvent,
                              impl_, base::Passed(&event)));
  }
}

void LogEventDispatcher::DispatchPacketEvent(
    std::unique_ptr<PacketEvent> event) const {
  if (env_->CurrentlyOn(CastEnvironment::MAIN)) {
    impl_->DispatchPacketEvent(std::move(event));
  } else {
    env_->PostTask(CastEnvironment::MAIN, FROM_HERE,
                   base::Bind(&LogEventDispatcher::Impl::DispatchPacketEvent,
                              impl_, base::Passed(&event)));
  }
}

void LogEventDispatcher::DispatchBatchOfEvents(
    std::unique_ptr<std::vector<FrameEvent>> frame_events,
    std::unique_ptr<std::vector<PacketEvent>> packet_events) const {
  if (env_->CurrentlyOn(CastEnvironment::MAIN)) {
    impl_->DispatchBatchOfEvents(std::move(frame_events),
                                 std::move(packet_events));
  } else {
    env_->PostTask(
        CastEnvironment::MAIN, FROM_HERE,
        base::Bind(&LogEventDispatcher::Impl::DispatchBatchOfEvents, impl_,
                   base::Passed(&frame_events), base::Passed(&packet_events)));
  }
}

void LogEventDispatcher::Subscribe(RawEventSubscriber* subscriber) {
  if (env_->CurrentlyOn(CastEnvironment::MAIN)) {
    impl_->Subscribe(subscriber);
  } else {
    env_->PostTask(
        CastEnvironment::MAIN, FROM_HERE,
        base::Bind(&LogEventDispatcher::Impl::Subscribe, impl_, subscriber));
  }
}

void LogEventDispatcher::Unsubscribe(RawEventSubscriber* subscriber) {
  if (env_->CurrentlyOn(CastEnvironment::MAIN)) {
    impl_->Unsubscribe(subscriber);
  } else {
    // This method, once it returns, guarantees |subscriber| will not receive
    // any more events.  Therefore, when called on a thread other than the
    // CastEnvironment's MAIN thread, block until the unsubscribe task
    // completes.
    struct Helper {
      static void UnsubscribeAndSignal(const scoped_refptr<Impl>& impl,
                                       RawEventSubscriber* subscriber,
                                       base::WaitableEvent* done) {
        impl->Unsubscribe(subscriber);
        done->Signal();
      }
    };
    base::WaitableEvent done(true, false);
    CHECK(env_->PostTask(
        CastEnvironment::MAIN, FROM_HERE,
        base::Bind(&Helper::UnsubscribeAndSignal, impl_, subscriber, &done)));
    done.Wait();
  }
}

LogEventDispatcher::Impl::Impl() {}

LogEventDispatcher::Impl::~Impl() {
  DCHECK(subscribers_.empty());
}

void LogEventDispatcher::Impl::DispatchFrameEvent(
    std::unique_ptr<FrameEvent> event) const {
  for (RawEventSubscriber* s : subscribers_)
    s->OnReceiveFrameEvent(*event);
}

void LogEventDispatcher::Impl::DispatchPacketEvent(
    std::unique_ptr<PacketEvent> event) const {
  for (RawEventSubscriber* s : subscribers_)
    s->OnReceivePacketEvent(*event);
}

void LogEventDispatcher::Impl::DispatchBatchOfEvents(
    std::unique_ptr<std::vector<FrameEvent>> frame_events,
    std::unique_ptr<std::vector<PacketEvent>> packet_events) const {
  for (RawEventSubscriber* s : subscribers_) {
    for (const FrameEvent& e : *frame_events)
      s->OnReceiveFrameEvent(e);
    for (const PacketEvent& e : *packet_events)
      s->OnReceivePacketEvent(e);
  }
}

void LogEventDispatcher::Impl::Subscribe(RawEventSubscriber* subscriber) {
  DCHECK(std::find(subscribers_.begin(), subscribers_.end(), subscriber) ==
         subscribers_.end());
  subscribers_.push_back(subscriber);
}

void LogEventDispatcher::Impl::Unsubscribe(RawEventSubscriber* subscriber) {
  const auto it =
      std::find(subscribers_.begin(), subscribers_.end(), subscriber);
  DCHECK(it != subscribers_.end());
  subscribers_.erase(it);
}

}  // namespace cast
}  // namespace media
