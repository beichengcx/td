//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/TransparentProxy.h"

#include "td/utils/logging.h"
#include "td/utils/port/Fd.h"

namespace td {

int VERBOSITY_NAME(proxy) = VERBOSITY_NAME(DEBUG);

TransparentProxy::TransparentProxy(SocketFd socket_fd, IPAddress ip_address, string username, string password,
                                   std::unique_ptr<Callback> callback, ActorShared<> parent)
    : fd_(std::move(socket_fd))
    , ip_address_(std::move(ip_address))
    , username_(std::move(username))
    , password_(std::move(password))
    , callback_(std::move(callback))
    , parent_(std::move(parent)) {
}

void TransparentProxy::on_error(Status status) {
  CHECK(status.is_error());
  VLOG(proxy) << "Receive " << status;
  if (callback_) {
    callback_->set_result(std::move(status));
    callback_.reset();
  }
  stop();
}

void TransparentProxy::tear_down() {
  VLOG(proxy) << "Finish to connect to proxy";
  unsubscribe(fd_.get_fd());
  fd_.get_fd().set_observer(nullptr);
  if (callback_) {
    if (!fd_.input_buffer().empty()) {
      LOG(ERROR) << "Have " << fd_.input_buffer().size() << " unread bytes";
      callback_->set_result(Status::Error("Proxy has sent to much data"));
    } else {
      callback_->set_result(std::move(fd_));
    }
    callback_.reset();
  }
}

void TransparentProxy::hangup() {
  on_error(Status::Error("Cancelled"));
}

void TransparentProxy::start_up() {
  VLOG(proxy) << "Begin to connect to proxy";
  fd_.get_fd().set_observer(this);
  subscribe(fd_.get_fd());
  set_timeout_in(10);
  if (can_write(fd_)) {
    loop();
  }
}

void TransparentProxy::loop() {
  auto status = [&] {
    TRY_STATUS(fd_.flush_read());
    TRY_STATUS(loop_impl());
    TRY_STATUS(fd_.flush_write());
    return Status::OK();
  }();
  if (status.is_error()) {
    on_error(std::move(status));
  }
  if (can_close(fd_)) {
    on_error(Status::Error("Connection closed"));
  }
}

void TransparentProxy::timeout_expired() {
  on_error(Status::Error("Timeout expired"));
}

}  // namespace td
