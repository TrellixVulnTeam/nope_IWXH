// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/websocket_transport_client_socket_pool.h"

#include <queue>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "net/base/capturing_net_log.h"
#include "net/base/ip_endpoint.h"
#include "net/base/load_timing_info.h"
#include "net/base/load_timing_info_test_util.h"
#include "net/base/net_errors.h"
#include "net/base/net_util.h"
#include "net/base/test_completion_callback.h"
#include "net/dns/mock_host_resolver.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool_histograms.h"
#include "net/socket/socket_test_util.h"
#include "net/socket/stream_socket.h"
#include "net/socket/transport_client_socket_pool_test_util.h"
#include "net/socket/websocket_endpoint_lock_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

const int kMaxSockets = 32;
const int kMaxSocketsPerGroup = 6;
const RequestPriority kDefaultPriority = LOW;

// RunLoop doesn't support this natively but it is easy to emulate.
void RunLoopForTimePeriod(base::TimeDelta period) {
  base::RunLoop run_loop;
  base::Closure quit_closure(run_loop.QuitClosure());
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE, quit_closure, period);
  run_loop.Run();
}

class WebSocketTransportClientSocketPoolTest : public ::testing::Test {
 protected:
  WebSocketTransportClientSocketPoolTest()
      : params_(new TransportSocketParams(
            HostPortPair("www.google.com", 80),
            false,
            false,
            OnHostResolutionCallback(),
            TransportSocketParams::COMBINE_CONNECT_AND_WRITE_DEFAULT)),
        histograms_(new ClientSocketPoolHistograms("TCPUnitTest")),
        host_resolver_(new MockHostResolver),
        client_socket_factory_(&net_log_),
        pool_(kMaxSockets,
              kMaxSocketsPerGroup,
              histograms_.get(),
              host_resolver_.get(),
              &client_socket_factory_,
              NULL) {}

  ~WebSocketTransportClientSocketPoolTest() override {
    RunUntilIdle();
    // ReleaseAllConnections() calls RunUntilIdle() after releasing each
    // connection.
    ReleaseAllConnections(ClientSocketPoolTest::NO_KEEP_ALIVE);
    EXPECT_TRUE(WebSocketEndpointLockManager::GetInstance()->IsEmpty());
  }

  static void RunUntilIdle() { base::RunLoop().RunUntilIdle(); }

  int StartRequest(const std::string& group_name, RequestPriority priority) {
    scoped_refptr<TransportSocketParams> params(
        new TransportSocketParams(
            HostPortPair("www.google.com", 80),
            false,
            false,
            OnHostResolutionCallback(),
            TransportSocketParams::COMBINE_CONNECT_AND_WRITE_DEFAULT));
    return test_base_.StartRequestUsingPool(
        &pool_, group_name, priority, params);
  }

  int GetOrderOfRequest(size_t index) {
    return test_base_.GetOrderOfRequest(index);
  }

  bool ReleaseOneConnection(ClientSocketPoolTest::KeepAlive keep_alive) {
    return test_base_.ReleaseOneConnection(keep_alive);
  }

  void ReleaseAllConnections(ClientSocketPoolTest::KeepAlive keep_alive) {
    test_base_.ReleaseAllConnections(keep_alive);
  }

  TestSocketRequest* request(int i) { return test_base_.request(i); }

  ScopedVector<TestSocketRequest>* requests() { return test_base_.requests(); }
  size_t completion_count() const { return test_base_.completion_count(); }

  CapturingNetLog net_log_;
  scoped_refptr<TransportSocketParams> params_;
  scoped_ptr<ClientSocketPoolHistograms> histograms_;
  scoped_ptr<MockHostResolver> host_resolver_;
  MockTransportClientSocketFactory client_socket_factory_;
  WebSocketTransportClientSocketPool pool_;
  ClientSocketPoolTest test_base_;
  ScopedWebSocketEndpointZeroUnlockDelay zero_unlock_delay_;

 private:
  DISALLOW_COPY_AND_ASSIGN(WebSocketTransportClientSocketPoolTest);
};

TEST_F(WebSocketTransportClientSocketPoolTest, Basic) {
  TestCompletionCallback callback;
  ClientSocketHandle handle;
  int rv = handle.Init(
      "a", params_, LOW, callback.callback(), &pool_, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle.is_initialized());
  EXPECT_FALSE(handle.socket());

  EXPECT_EQ(OK, callback.WaitForResult());
  EXPECT_TRUE(handle.is_initialized());
  EXPECT_TRUE(handle.socket());
  TestLoadTimingInfoConnectedNotReused(handle);
}

// Make sure that WebSocketTransportConnectJob passes on its priority to its
// HostResolver request on Init.
TEST_F(WebSocketTransportClientSocketPoolTest, SetResolvePriorityOnInit) {
  for (int i = MINIMUM_PRIORITY; i <= MAXIMUM_PRIORITY; ++i) {
    RequestPriority priority = static_cast<RequestPriority>(i);
    TestCompletionCallback callback;
    ClientSocketHandle handle;
    EXPECT_EQ(ERR_IO_PENDING,
              handle.Init("a",
                          params_,
                          priority,
                          callback.callback(),
                          &pool_,
                          BoundNetLog()));
    EXPECT_EQ(priority, host_resolver_->last_request_priority());
  }
}

TEST_F(WebSocketTransportClientSocketPoolTest, InitHostResolutionFailure) {
  host_resolver_->rules()->AddSimulatedFailure("unresolvable.host.name");
  TestCompletionCallback callback;
  ClientSocketHandle handle;
  HostPortPair host_port_pair("unresolvable.host.name", 80);
  scoped_refptr<TransportSocketParams> dest(new TransportSocketParams(
      host_port_pair, false, false, OnHostResolutionCallback(),
      TransportSocketParams::COMBINE_CONNECT_AND_WRITE_DEFAULT));
  EXPECT_EQ(ERR_IO_PENDING,
            handle.Init("a",
                        dest,
                        kDefaultPriority,
                        callback.callback(),
                        &pool_,
                        BoundNetLog()));
  EXPECT_EQ(ERR_NAME_NOT_RESOLVED, callback.WaitForResult());
}

TEST_F(WebSocketTransportClientSocketPoolTest, InitConnectionFailure) {
  client_socket_factory_.set_default_client_socket_type(
      MockTransportClientSocketFactory::MOCK_FAILING_CLIENT_SOCKET);
  TestCompletionCallback callback;
  ClientSocketHandle handle;
  EXPECT_EQ(ERR_IO_PENDING,
            handle.Init("a",
                        params_,
                        kDefaultPriority,
                        callback.callback(),
                        &pool_,
                        BoundNetLog()));
  EXPECT_EQ(ERR_CONNECTION_FAILED, callback.WaitForResult());

  // Make the host resolutions complete synchronously this time.
  host_resolver_->set_synchronous_mode(true);
  EXPECT_EQ(ERR_CONNECTION_FAILED,
            handle.Init("a",
                        params_,
                        kDefaultPriority,
                        callback.callback(),
                        &pool_,
                        BoundNetLog()));
}

TEST_F(WebSocketTransportClientSocketPoolTest, PendingRequestsFinishFifo) {
  // First request finishes asynchronously.
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, request(0)->WaitForResult());

  // Make all subsequent host resolutions complete synchronously.
  host_resolver_->set_synchronous_mode(true);

  // Rest of them wait for the first socket to be released.
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));

  ReleaseAllConnections(ClientSocketPoolTest::KEEP_ALIVE);

  EXPECT_EQ(6, client_socket_factory_.allocation_count());

  // One initial asynchronous request and then 5 pending requests.
  EXPECT_EQ(6U, completion_count());

  // The requests finish in FIFO order.
  EXPECT_EQ(1, GetOrderOfRequest(1));
  EXPECT_EQ(2, GetOrderOfRequest(2));
  EXPECT_EQ(3, GetOrderOfRequest(3));
  EXPECT_EQ(4, GetOrderOfRequest(4));
  EXPECT_EQ(5, GetOrderOfRequest(5));
  EXPECT_EQ(6, GetOrderOfRequest(6));

  // Make sure we test order of all requests made.
  EXPECT_EQ(ClientSocketPoolTest::kIndexOutOfBounds, GetOrderOfRequest(7));
}

TEST_F(WebSocketTransportClientSocketPoolTest, PendingRequests_NoKeepAlive) {
  // First request finishes asynchronously.
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, request(0)->WaitForResult());

  // Make all subsequent host resolutions complete synchronously.
  host_resolver_->set_synchronous_mode(true);

  // Rest of them wait for the first socket to be released.
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));

  ReleaseAllConnections(ClientSocketPoolTest::NO_KEEP_ALIVE);

  // The pending requests should finish successfully.
  EXPECT_EQ(OK, request(1)->WaitForResult());
  EXPECT_EQ(OK, request(2)->WaitForResult());
  EXPECT_EQ(OK, request(3)->WaitForResult());
  EXPECT_EQ(OK, request(4)->WaitForResult());
  EXPECT_EQ(OK, request(5)->WaitForResult());

  EXPECT_EQ(static_cast<int>(requests()->size()),
            client_socket_factory_.allocation_count());

  // First asynchronous request, and then last 5 pending requests.
  EXPECT_EQ(6U, completion_count());
}

// This test will start up a RequestSocket() and then immediately Cancel() it.
// The pending host resolution will eventually complete, and destroy the
// ClientSocketPool which will crash if the group was not cleared properly.
TEST_F(WebSocketTransportClientSocketPoolTest, CancelRequestClearGroup) {
  TestCompletionCallback callback;
  ClientSocketHandle handle;
  EXPECT_EQ(ERR_IO_PENDING,
            handle.Init("a",
                        params_,
                        kDefaultPriority,
                        callback.callback(),
                        &pool_,
                        BoundNetLog()));
  handle.Reset();
}

TEST_F(WebSocketTransportClientSocketPoolTest, TwoRequestsCancelOne) {
  ClientSocketHandle handle;
  TestCompletionCallback callback;
  ClientSocketHandle handle2;
  TestCompletionCallback callback2;

  EXPECT_EQ(ERR_IO_PENDING,
            handle.Init("a",
                        params_,
                        kDefaultPriority,
                        callback.callback(),
                        &pool_,
                        BoundNetLog()));
  EXPECT_EQ(ERR_IO_PENDING,
            handle2.Init("a",
                         params_,
                         kDefaultPriority,
                         callback2.callback(),
                         &pool_,
                         BoundNetLog()));

  handle.Reset();

  EXPECT_EQ(OK, callback2.WaitForResult());
  handle2.Reset();
}

TEST_F(WebSocketTransportClientSocketPoolTest, ConnectCancelConnect) {
  client_socket_factory_.set_default_client_socket_type(
      MockTransportClientSocketFactory::MOCK_PENDING_CLIENT_SOCKET);
  ClientSocketHandle handle;
  TestCompletionCallback callback;
  EXPECT_EQ(ERR_IO_PENDING,
            handle.Init("a",
                        params_,
                        kDefaultPriority,
                        callback.callback(),
                        &pool_,
                        BoundNetLog()));

  handle.Reset();

  TestCompletionCallback callback2;
  EXPECT_EQ(ERR_IO_PENDING,
            handle.Init("a",
                        params_,
                        kDefaultPriority,
                        callback2.callback(),
                        &pool_,
                        BoundNetLog()));

  host_resolver_->set_synchronous_mode(true);
  // At this point, handle has two ConnectingSockets out for it.  Due to the
  // setting the mock resolver into synchronous mode, the host resolution for
  // both will return in the same loop of the MessageLoop.  The client socket
  // is a pending socket, so the Connect() will asynchronously complete on the
  // next loop of the MessageLoop.  That means that the first
  // ConnectingSocket will enter OnIOComplete, and then the second one will.
  // If the first one is not cancelled, it will advance the load state, and
  // then the second one will crash.

  EXPECT_EQ(OK, callback2.WaitForResult());
  EXPECT_FALSE(callback.have_result());

  handle.Reset();
}

TEST_F(WebSocketTransportClientSocketPoolTest, CancelRequest) {
  // First request finishes asynchronously.
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, request(0)->WaitForResult());

  // Make all subsequent host resolutions complete synchronously.
  host_resolver_->set_synchronous_mode(true);

  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));

  // Cancel a request.
  const size_t index_to_cancel = 2;
  EXPECT_FALSE(request(index_to_cancel)->handle()->is_initialized());
  request(index_to_cancel)->handle()->Reset();

  ReleaseAllConnections(ClientSocketPoolTest::KEEP_ALIVE);

  EXPECT_EQ(5, client_socket_factory_.allocation_count());

  EXPECT_EQ(1, GetOrderOfRequest(1));
  EXPECT_EQ(2, GetOrderOfRequest(2));
  EXPECT_EQ(ClientSocketPoolTest::kRequestNotFound,
            GetOrderOfRequest(3));  // Canceled request.
  EXPECT_EQ(3, GetOrderOfRequest(4));
  EXPECT_EQ(4, GetOrderOfRequest(5));
  EXPECT_EQ(5, GetOrderOfRequest(6));

  // Make sure we test order of all requests made.
  EXPECT_EQ(ClientSocketPoolTest::kIndexOutOfBounds, GetOrderOfRequest(7));
}

class RequestSocketCallback : public TestCompletionCallbackBase {
 public:
  RequestSocketCallback(ClientSocketHandle* handle,
                        WebSocketTransportClientSocketPool* pool)
      : handle_(handle),
        pool_(pool),
        within_callback_(false),
        callback_(base::Bind(&RequestSocketCallback::OnComplete,
                             base::Unretained(this))) {}

  ~RequestSocketCallback() override {}

  const CompletionCallback& callback() const { return callback_; }

 private:
  void OnComplete(int result) {
    SetResult(result);
    ASSERT_EQ(OK, result);

    if (!within_callback_) {
      // Don't allow reuse of the socket.  Disconnect it and then release it and
      // run through the MessageLoop once to get it completely released.
      handle_->socket()->Disconnect();
      handle_->Reset();
      {
        base::MessageLoop::ScopedNestableTaskAllower allow(
            base::MessageLoop::current());
        base::MessageLoop::current()->RunUntilIdle();
      }
      within_callback_ = true;
      scoped_refptr<TransportSocketParams> dest(
          new TransportSocketParams(
              HostPortPair("www.google.com", 80),
              false,
              false,
              OnHostResolutionCallback(),
              TransportSocketParams::COMBINE_CONNECT_AND_WRITE_DEFAULT));
      int rv =
          handle_->Init("a", dest, LOWEST, callback(), pool_, BoundNetLog());
      EXPECT_EQ(OK, rv);
    }
  }

  ClientSocketHandle* const handle_;
  WebSocketTransportClientSocketPool* const pool_;
  bool within_callback_;
  CompletionCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(RequestSocketCallback);
};

TEST_F(WebSocketTransportClientSocketPoolTest, RequestTwice) {
  ClientSocketHandle handle;
  RequestSocketCallback callback(&handle, &pool_);
  scoped_refptr<TransportSocketParams> dest(
      new TransportSocketParams(
          HostPortPair("www.google.com", 80),
          false,
          false,
          OnHostResolutionCallback(),
          TransportSocketParams::COMBINE_CONNECT_AND_WRITE_DEFAULT));
  int rv = handle.Init(
      "a", dest, LOWEST, callback.callback(), &pool_, BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, rv);

  // The callback is going to request "www.google.com". We want it to complete
  // synchronously this time.
  host_resolver_->set_synchronous_mode(true);

  EXPECT_EQ(OK, callback.WaitForResult());

  handle.Reset();
}

// Make sure that pending requests get serviced after active requests get
// cancelled.
TEST_F(WebSocketTransportClientSocketPoolTest,
       CancelActiveRequestWithPendingRequests) {
  client_socket_factory_.set_default_client_socket_type(
      MockTransportClientSocketFactory::MOCK_PENDING_CLIENT_SOCKET);

  // Queue up all the requests
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));

  // Now, kMaxSocketsPerGroup requests should be active.  Let's cancel them.
  ASSERT_LE(kMaxSocketsPerGroup, static_cast<int>(requests()->size()));
  for (int i = 0; i < kMaxSocketsPerGroup; i++)
    request(i)->handle()->Reset();

  // Let's wait for the rest to complete now.
  for (size_t i = kMaxSocketsPerGroup; i < requests()->size(); ++i) {
    EXPECT_EQ(OK, request(i)->WaitForResult());
    request(i)->handle()->Reset();
  }

  EXPECT_EQ(requests()->size() - kMaxSocketsPerGroup, completion_count());
}

// Make sure that pending requests get serviced after active requests fail.
TEST_F(WebSocketTransportClientSocketPoolTest,
       FailingActiveRequestWithPendingRequests) {
  client_socket_factory_.set_default_client_socket_type(
      MockTransportClientSocketFactory::MOCK_PENDING_FAILING_CLIENT_SOCKET);

  const int kNumRequests = 2 * kMaxSocketsPerGroup + 1;
  ASSERT_LE(kNumRequests, kMaxSockets);  // Otherwise the test will hang.

  // Queue up all the requests
  for (int i = 0; i < kNumRequests; i++)
    EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));

  for (int i = 0; i < kNumRequests; i++)
    EXPECT_EQ(ERR_CONNECTION_FAILED, request(i)->WaitForResult());
}

// The lock on the endpoint is released when a ClientSocketHandle is reset.
TEST_F(WebSocketTransportClientSocketPoolTest, LockReleasedOnHandleReset) {
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, request(0)->WaitForResult());
  EXPECT_FALSE(request(1)->handle()->is_initialized());
  request(0)->handle()->Reset();
  RunUntilIdle();
  EXPECT_TRUE(request(1)->handle()->is_initialized());
}

// The lock on the endpoint is released when a ClientSocketHandle is deleted.
TEST_F(WebSocketTransportClientSocketPoolTest, LockReleasedOnHandleDelete) {
  TestCompletionCallback callback;
  scoped_ptr<ClientSocketHandle> handle(new ClientSocketHandle);
  int rv = handle->Init(
      "a", params_, LOW, callback.callback(), &pool_, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);

  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, callback.WaitForResult());
  EXPECT_FALSE(request(0)->handle()->is_initialized());
  handle.reset();
  RunUntilIdle();
  EXPECT_TRUE(request(0)->handle()->is_initialized());
}

// A new connection is performed when the lock on the previous connection is
// explicitly released.
TEST_F(WebSocketTransportClientSocketPoolTest,
       ConnectionProceedsOnExplicitRelease) {
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, request(0)->WaitForResult());
  EXPECT_FALSE(request(1)->handle()->is_initialized());
  WebSocketTransportClientSocketPool::UnlockEndpoint(request(0)->handle());
  RunUntilIdle();
  EXPECT_TRUE(request(1)->handle()->is_initialized());
}

// A connection which is cancelled before completion does not block subsequent
// connections.
TEST_F(WebSocketTransportClientSocketPoolTest,
       CancelDuringConnectionReleasesLock) {
  MockTransportClientSocketFactory::ClientSocketType case_types[] = {
      MockTransportClientSocketFactory::MOCK_STALLED_CLIENT_SOCKET,
      MockTransportClientSocketFactory::MOCK_PENDING_CLIENT_SOCKET};

  client_socket_factory_.set_client_socket_types(case_types,
                                                 arraysize(case_types));

  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  RunUntilIdle();
  pool_.CancelRequest("a", request(0)->handle());
  EXPECT_EQ(OK, request(1)->WaitForResult());
}

// Test the case of the IPv6 address stalling, and falling back to the IPv4
// socket which finishes first.
TEST_F(WebSocketTransportClientSocketPoolTest,
       IPv6FallbackSocketIPv4FinishesFirst) {
  WebSocketTransportClientSocketPool pool(kMaxSockets,
                                          kMaxSocketsPerGroup,
                                          histograms_.get(),
                                          host_resolver_.get(),
                                          &client_socket_factory_,
                                          NULL);

  MockTransportClientSocketFactory::ClientSocketType case_types[] = {
      // This is the IPv6 socket.
      MockTransportClientSocketFactory::MOCK_STALLED_CLIENT_SOCKET,
      // This is the IPv4 socket.
      MockTransportClientSocketFactory::MOCK_PENDING_CLIENT_SOCKET};

  client_socket_factory_.set_client_socket_types(case_types, 2);

  // Resolve an AddressList with an IPv6 address first and then an IPv4 address.
  host_resolver_->rules()->AddIPLiteralRule(
      "*", "2:abcd::3:4:ff,2.2.2.2", std::string());

  TestCompletionCallback callback;
  ClientSocketHandle handle;
  int rv =
      handle.Init("a", params_, LOW, callback.callback(), &pool, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle.is_initialized());
  EXPECT_FALSE(handle.socket());

  EXPECT_EQ(OK, callback.WaitForResult());
  EXPECT_TRUE(handle.is_initialized());
  EXPECT_TRUE(handle.socket());
  IPEndPoint endpoint;
  handle.socket()->GetLocalAddress(&endpoint);
  EXPECT_EQ(kIPv4AddressSize, endpoint.address().size());
  EXPECT_EQ(2, client_socket_factory_.allocation_count());
}

// Test the case of the IPv6 address being slow, thus falling back to trying to
// connect to the IPv4 address, but having the connect to the IPv6 address
// finish first.
TEST_F(WebSocketTransportClientSocketPoolTest,
       IPv6FallbackSocketIPv6FinishesFirst) {
  WebSocketTransportClientSocketPool pool(kMaxSockets,
                                          kMaxSocketsPerGroup,
                                          histograms_.get(),
                                          host_resolver_.get(),
                                          &client_socket_factory_,
                                          NULL);

  MockTransportClientSocketFactory::ClientSocketType case_types[] = {
      // This is the IPv6 socket.
      MockTransportClientSocketFactory::MOCK_DELAYED_CLIENT_SOCKET,
      // This is the IPv4 socket.
      MockTransportClientSocketFactory::MOCK_STALLED_CLIENT_SOCKET};

  client_socket_factory_.set_client_socket_types(case_types, 2);
  client_socket_factory_.set_delay(base::TimeDelta::FromMilliseconds(
      TransportConnectJobHelper::kIPv6FallbackTimerInMs + 50));

  // Resolve an AddressList with an IPv6 address first and then an IPv4 address.
  host_resolver_->rules()->AddIPLiteralRule(
      "*", "2:abcd::3:4:ff,2.2.2.2", std::string());

  TestCompletionCallback callback;
  ClientSocketHandle handle;
  int rv =
      handle.Init("a", params_, LOW, callback.callback(), &pool, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle.is_initialized());
  EXPECT_FALSE(handle.socket());

  EXPECT_EQ(OK, callback.WaitForResult());
  EXPECT_TRUE(handle.is_initialized());
  EXPECT_TRUE(handle.socket());
  IPEndPoint endpoint;
  handle.socket()->GetLocalAddress(&endpoint);
  EXPECT_EQ(kIPv6AddressSize, endpoint.address().size());
  EXPECT_EQ(2, client_socket_factory_.allocation_count());
}

TEST_F(WebSocketTransportClientSocketPoolTest,
       IPv6NoIPv4AddressesToFallbackTo) {
  WebSocketTransportClientSocketPool pool(kMaxSockets,
                                          kMaxSocketsPerGroup,
                                          histograms_.get(),
                                          host_resolver_.get(),
                                          &client_socket_factory_,
                                          NULL);

  client_socket_factory_.set_default_client_socket_type(
      MockTransportClientSocketFactory::MOCK_DELAYED_CLIENT_SOCKET);

  // Resolve an AddressList with only IPv6 addresses.
  host_resolver_->rules()->AddIPLiteralRule(
      "*", "2:abcd::3:4:ff,3:abcd::3:4:ff", std::string());

  TestCompletionCallback callback;
  ClientSocketHandle handle;
  int rv =
      handle.Init("a", params_, LOW, callback.callback(), &pool, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle.is_initialized());
  EXPECT_FALSE(handle.socket());

  EXPECT_EQ(OK, callback.WaitForResult());
  EXPECT_TRUE(handle.is_initialized());
  EXPECT_TRUE(handle.socket());
  IPEndPoint endpoint;
  handle.socket()->GetLocalAddress(&endpoint);
  EXPECT_EQ(kIPv6AddressSize, endpoint.address().size());
  EXPECT_EQ(1, client_socket_factory_.allocation_count());
}

TEST_F(WebSocketTransportClientSocketPoolTest, IPv4HasNoFallback) {
  WebSocketTransportClientSocketPool pool(kMaxSockets,
                                          kMaxSocketsPerGroup,
                                          histograms_.get(),
                                          host_resolver_.get(),
                                          &client_socket_factory_,
                                          NULL);

  client_socket_factory_.set_default_client_socket_type(
      MockTransportClientSocketFactory::MOCK_DELAYED_CLIENT_SOCKET);

  // Resolve an AddressList with only IPv4 addresses.
  host_resolver_->rules()->AddIPLiteralRule("*", "1.1.1.1", std::string());

  TestCompletionCallback callback;
  ClientSocketHandle handle;
  int rv =
      handle.Init("a", params_, LOW, callback.callback(), &pool, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle.is_initialized());
  EXPECT_FALSE(handle.socket());

  EXPECT_EQ(OK, callback.WaitForResult());
  EXPECT_TRUE(handle.is_initialized());
  EXPECT_TRUE(handle.socket());
  IPEndPoint endpoint;
  handle.socket()->GetLocalAddress(&endpoint);
  EXPECT_EQ(kIPv4AddressSize, endpoint.address().size());
  EXPECT_EQ(1, client_socket_factory_.allocation_count());
}

// If all IPv6 addresses fail to connect synchronously, then IPv4 connections
// proceeed immediately.
TEST_F(WebSocketTransportClientSocketPoolTest, IPv6InstantFail) {
  WebSocketTransportClientSocketPool pool(kMaxSockets,
                                          kMaxSocketsPerGroup,
                                          histograms_.get(),
                                          host_resolver_.get(),
                                          &client_socket_factory_,
                                          NULL);

  MockTransportClientSocketFactory::ClientSocketType case_types[] = {
      // First IPv6 socket.
      MockTransportClientSocketFactory::MOCK_FAILING_CLIENT_SOCKET,
      // Second IPv6 socket.
      MockTransportClientSocketFactory::MOCK_FAILING_CLIENT_SOCKET,
      // This is the IPv4 socket.
      MockTransportClientSocketFactory::MOCK_CLIENT_SOCKET};

  client_socket_factory_.set_client_socket_types(case_types,
                                                 arraysize(case_types));

  // Resolve an AddressList with two IPv6 addresses and then an IPv4 address.
  host_resolver_->rules()->AddIPLiteralRule(
      "*", "2:abcd::3:4:ff,2:abcd::3:5:ff,2.2.2.2", std::string());
  host_resolver_->set_synchronous_mode(true);
  TestCompletionCallback callback;
  ClientSocketHandle handle;
  int rv =
      handle.Init("a", params_, LOW, callback.callback(), &pool, BoundNetLog());
  EXPECT_EQ(OK, rv);
  ASSERT_TRUE(handle.socket());

  IPEndPoint endpoint;
  handle.socket()->GetPeerAddress(&endpoint);
  EXPECT_EQ("2.2.2.2", endpoint.ToStringWithoutPort());
}

// If all IPv6 addresses fail before the IPv4 fallback timeout, then the IPv4
// connections proceed immediately.
TEST_F(WebSocketTransportClientSocketPoolTest, IPv6RapidFail) {
  WebSocketTransportClientSocketPool pool(kMaxSockets,
                                          kMaxSocketsPerGroup,
                                          histograms_.get(),
                                          host_resolver_.get(),
                                          &client_socket_factory_,
                                          NULL);

  MockTransportClientSocketFactory::ClientSocketType case_types[] = {
      // First IPv6 socket.
      MockTransportClientSocketFactory::MOCK_PENDING_FAILING_CLIENT_SOCKET,
      // Second IPv6 socket.
      MockTransportClientSocketFactory::MOCK_PENDING_FAILING_CLIENT_SOCKET,
      // This is the IPv4 socket.
      MockTransportClientSocketFactory::MOCK_CLIENT_SOCKET};

  client_socket_factory_.set_client_socket_types(case_types,
                                                 arraysize(case_types));

  // Resolve an AddressList with two IPv6 addresses and then an IPv4 address.
  host_resolver_->rules()->AddIPLiteralRule(
      "*", "2:abcd::3:4:ff,2:abcd::3:5:ff,2.2.2.2", std::string());

  TestCompletionCallback callback;
  ClientSocketHandle handle;
  int rv =
      handle.Init("a", params_, LOW, callback.callback(), &pool, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle.socket());

  base::TimeTicks start(base::TimeTicks::Now());
  EXPECT_EQ(OK, callback.WaitForResult());
  EXPECT_LT(base::TimeTicks::Now() - start,
            base::TimeDelta::FromMilliseconds(
                TransportConnectJobHelper::kIPv6FallbackTimerInMs));
  ASSERT_TRUE(handle.socket());

  IPEndPoint endpoint;
  handle.socket()->GetPeerAddress(&endpoint);
  EXPECT_EQ("2.2.2.2", endpoint.ToStringWithoutPort());
}

// If two sockets connect successfully, the one which connected first wins (this
// can only happen if the sockets are different types, since sockets of the same
// type do not race).
TEST_F(WebSocketTransportClientSocketPoolTest, FirstSuccessWins) {
  WebSocketTransportClientSocketPool pool(kMaxSockets,
                                          kMaxSocketsPerGroup,
                                          histograms_.get(),
                                          host_resolver_.get(),
                                          &client_socket_factory_,
                                          NULL);

  client_socket_factory_.set_default_client_socket_type(
      MockTransportClientSocketFactory::MOCK_TRIGGERABLE_CLIENT_SOCKET);

  // Resolve an AddressList with an IPv6 addresses and an IPv4 address.
  host_resolver_->rules()->AddIPLiteralRule(
      "*", "2:abcd::3:4:ff,2.2.2.2", std::string());

  TestCompletionCallback callback;
  ClientSocketHandle handle;
  int rv =
      handle.Init("a", params_, LOW, callback.callback(), &pool, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  ASSERT_FALSE(handle.socket());

  base::Closure ipv6_connect_trigger =
      client_socket_factory_.WaitForTriggerableSocketCreation();
  base::Closure ipv4_connect_trigger =
      client_socket_factory_.WaitForTriggerableSocketCreation();

  ipv4_connect_trigger.Run();
  ipv6_connect_trigger.Run();

  EXPECT_EQ(OK, callback.WaitForResult());
  ASSERT_TRUE(handle.socket());

  IPEndPoint endpoint;
  handle.socket()->GetPeerAddress(&endpoint);
  EXPECT_EQ("2.2.2.2", endpoint.ToStringWithoutPort());
}

// We should not report failure until all connections have failed.
TEST_F(WebSocketTransportClientSocketPoolTest, LastFailureWins) {
  WebSocketTransportClientSocketPool pool(kMaxSockets,
                                          kMaxSocketsPerGroup,
                                          histograms_.get(),
                                          host_resolver_.get(),
                                          &client_socket_factory_,
                                          NULL);

  client_socket_factory_.set_default_client_socket_type(
      MockTransportClientSocketFactory::MOCK_DELAYED_FAILING_CLIENT_SOCKET);
  base::TimeDelta delay = base::TimeDelta::FromMilliseconds(
      TransportConnectJobHelper::kIPv6FallbackTimerInMs / 3);
  client_socket_factory_.set_delay(delay);

  // Resolve an AddressList with 4 IPv6 addresses and 2 IPv4 addresses.
  host_resolver_->rules()->AddIPLiteralRule("*",
                                            "1:abcd::3:4:ff,2:abcd::3:4:ff,"
                                            "3:abcd::3:4:ff,4:abcd::3:4:ff,"
                                            "1.1.1.1,2.2.2.2",
                                            std::string());

  // Expected order of events:
  // After 100ms: Connect to 1:abcd::3:4:ff times out
  // After 200ms: Connect to 2:abcd::3:4:ff times out
  // After 300ms: Connect to 3:abcd::3:4:ff times out, IPv4 fallback starts
  // After 400ms: Connect to 4:abcd::3:4:ff and 1.1.1.1 time out
  // After 500ms: Connect to 2.2.2.2 times out

  TestCompletionCallback callback;
  ClientSocketHandle handle;
  base::TimeTicks start(base::TimeTicks::Now());
  int rv =
      handle.Init("a", params_, LOW, callback.callback(), &pool, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);

  EXPECT_EQ(ERR_CONNECTION_FAILED, callback.WaitForResult());

  EXPECT_GE(base::TimeTicks::Now() - start, delay * 5);
}

// Global timeout for all connects applies. This test is disabled by default
// because it takes 4 minutes. Run with --gtest_also_run_disabled_tests if you
// want to run it.
TEST_F(WebSocketTransportClientSocketPoolTest, DISABLED_OverallTimeoutApplies) {
  WebSocketTransportClientSocketPool pool(kMaxSockets,
                                          kMaxSocketsPerGroup,
                                          histograms_.get(),
                                          host_resolver_.get(),
                                          &client_socket_factory_,
                                          NULL);
  const base::TimeDelta connect_job_timeout = pool.ConnectionTimeout();

  client_socket_factory_.set_default_client_socket_type(
      MockTransportClientSocketFactory::MOCK_DELAYED_FAILING_CLIENT_SOCKET);
  client_socket_factory_.set_delay(base::TimeDelta::FromSeconds(1) +
                                   connect_job_timeout / 6);

  // Resolve an AddressList with 6 IPv6 addresses and 6 IPv4 addresses.
  host_resolver_->rules()->AddIPLiteralRule("*",
                                            "1:abcd::3:4:ff,2:abcd::3:4:ff,"
                                            "3:abcd::3:4:ff,4:abcd::3:4:ff,"
                                            "5:abcd::3:4:ff,6:abcd::3:4:ff,"
                                            "1.1.1.1,2.2.2.2,3.3.3.3,"
                                            "4.4.4.4,5.5.5.5,6.6.6.6",
                                            std::string());

  TestCompletionCallback callback;
  ClientSocketHandle handle;

  int rv =
      handle.Init("a", params_, LOW, callback.callback(), &pool, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);

  EXPECT_EQ(ERR_TIMED_OUT, callback.WaitForResult());
}

TEST_F(WebSocketTransportClientSocketPoolTest, MaxSocketsEnforced) {
  host_resolver_->set_synchronous_mode(true);
  for (int i = 0; i < kMaxSockets; ++i) {
    ASSERT_EQ(OK, StartRequest("a", kDefaultPriority));
    WebSocketTransportClientSocketPool::UnlockEndpoint(request(i)->handle());
    RunUntilIdle();
  }
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
}

TEST_F(WebSocketTransportClientSocketPoolTest, MaxSocketsEnforcedWhenPending) {
  for (int i = 0; i < kMaxSockets + 1; ++i) {
    EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  }
  // Now there are 32 sockets waiting to connect, and one stalled.
  for (int i = 0; i < kMaxSockets; ++i) {
    RunUntilIdle();
    EXPECT_TRUE(request(i)->handle()->is_initialized());
    EXPECT_TRUE(request(i)->handle()->socket());
    WebSocketTransportClientSocketPool::UnlockEndpoint(request(i)->handle());
  }
  // Now there are 32 sockets connected, and one stalled.
  RunUntilIdle();
  EXPECT_FALSE(request(kMaxSockets)->handle()->is_initialized());
  EXPECT_FALSE(request(kMaxSockets)->handle()->socket());
}

TEST_F(WebSocketTransportClientSocketPoolTest, StalledSocketReleased) {
  host_resolver_->set_synchronous_mode(true);
  for (int i = 0; i < kMaxSockets; ++i) {
    ASSERT_EQ(OK, StartRequest("a", kDefaultPriority));
    WebSocketTransportClientSocketPool::UnlockEndpoint(request(i)->handle());
    RunUntilIdle();
  }

  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  ReleaseOneConnection(ClientSocketPoolTest::NO_KEEP_ALIVE);
  EXPECT_TRUE(request(kMaxSockets)->handle()->is_initialized());
  EXPECT_TRUE(request(kMaxSockets)->handle()->socket());
}

TEST_F(WebSocketTransportClientSocketPoolTest, IsStalledTrueWhenStalled) {
  for (int i = 0; i < kMaxSockets + 1; ++i) {
    EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  }
  EXPECT_EQ(OK, request(0)->WaitForResult());
  EXPECT_TRUE(pool_.IsStalled());
}

TEST_F(WebSocketTransportClientSocketPoolTest,
       CancellingPendingSocketUnstallsStalledSocket) {
  for (int i = 0; i < kMaxSockets + 1; ++i) {
    EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  }
  EXPECT_EQ(OK, request(0)->WaitForResult());
  request(1)->handle()->Reset();
  RunUntilIdle();
  EXPECT_FALSE(pool_.IsStalled());
}

TEST_F(WebSocketTransportClientSocketPoolTest,
       LoadStateOfStalledSocketIsWaitingForAvailableSocket) {
  for (int i = 0; i < kMaxSockets + 1; ++i) {
    EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  }
  EXPECT_EQ(LOAD_STATE_WAITING_FOR_AVAILABLE_SOCKET,
            pool_.GetLoadState("a", request(kMaxSockets)->handle()));
}

TEST_F(WebSocketTransportClientSocketPoolTest,
       CancellingStalledSocketUnstallsPool) {
  for (int i = 0; i < kMaxSockets + 1; ++i) {
    EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  }
  request(kMaxSockets)->handle()->Reset();
  RunUntilIdle();
  EXPECT_FALSE(pool_.IsStalled());
}

TEST_F(WebSocketTransportClientSocketPoolTest,
       FlushWithErrorFlushesPendingConnections) {
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  pool_.FlushWithError(ERR_FAILED);
  EXPECT_EQ(ERR_FAILED, request(0)->WaitForResult());
}

TEST_F(WebSocketTransportClientSocketPoolTest,
       FlushWithErrorFlushesStalledConnections) {
  for (int i = 0; i < kMaxSockets + 1; ++i) {
    EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  }
  pool_.FlushWithError(ERR_FAILED);
  EXPECT_EQ(ERR_FAILED, request(kMaxSockets)->WaitForResult());
}

TEST_F(WebSocketTransportClientSocketPoolTest,
       AfterFlushWithErrorCanMakeNewConnections) {
  for (int i = 0; i < kMaxSockets + 1; ++i) {
    EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  }
  pool_.FlushWithError(ERR_FAILED);
  host_resolver_->set_synchronous_mode(true);
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
}

// Deleting pending connections can release the lock on the endpoint, which can
// in principle lead to other pending connections succeeding. However, when we
// call FlushWithError(), everything should fail.
TEST_F(WebSocketTransportClientSocketPoolTest,
       FlushWithErrorDoesNotCauseSuccessfulConnections) {
  host_resolver_->set_synchronous_mode(true);
  MockTransportClientSocketFactory::ClientSocketType first_type[] = {
    // First socket
    MockTransportClientSocketFactory::MOCK_PENDING_CLIENT_SOCKET
  };
  client_socket_factory_.set_client_socket_types(first_type,
                                                 arraysize(first_type));
  // The rest of the sockets will connect synchronously.
  client_socket_factory_.set_default_client_socket_type(
      MockTransportClientSocketFactory::MOCK_CLIENT_SOCKET);
  for (int i = 0; i < kMaxSockets; ++i) {
    EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  }
  // Now we have one socket in STATE_TRANSPORT_CONNECT and the rest in
  // STATE_OBTAIN_LOCK. If any of the sockets in STATE_OBTAIN_LOCK is given the
  // lock, they will synchronously connect.
  pool_.FlushWithError(ERR_FAILED);
  for (int i = 0; i < kMaxSockets; ++i) {
    EXPECT_EQ(ERR_FAILED, request(i)->WaitForResult());
  }
}

// This is a regression test for the first attempted fix for
// FlushWithErrorDoesNotCauseSuccessfulConnections. Because a ConnectJob can
// have both IPv4 and IPv6 subjobs, it can be both connecting and waiting for
// the lock at the same time.
TEST_F(WebSocketTransportClientSocketPoolTest,
       FlushWithErrorDoesNotCauseSuccessfulConnectionsMultipleAddressTypes) {
  host_resolver_->set_synchronous_mode(true);
  // The first |kMaxSockets| sockets to connect will be IPv6. Then we will have
  // one IPv4.
  std::vector<MockTransportClientSocketFactory::ClientSocketType> socket_types(
      kMaxSockets + 1,
      MockTransportClientSocketFactory::MOCK_STALLED_CLIENT_SOCKET);
  client_socket_factory_.set_client_socket_types(&socket_types[0],
                                                 socket_types.size());
  // The rest of the sockets will connect synchronously.
  client_socket_factory_.set_default_client_socket_type(
      MockTransportClientSocketFactory::MOCK_CLIENT_SOCKET);
  for (int i = 0; i < kMaxSockets; ++i) {
    host_resolver_->rules()->ClearRules();
    // Each connect job has a different IPv6 address but the same IPv4 address.
    // So the IPv6 connections happen in parallel but the IPv4 ones are
    // serialised.
    host_resolver_->rules()->AddIPLiteralRule("*",
                                              base::StringPrintf(
                                                  "%x:abcd::3:4:ff,"
                                                  "1.1.1.1",
                                                  i + 1),
                                              std::string());
    EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  }
  // Now we have |kMaxSockets| IPv6 sockets stalled in connect. No IPv4 sockets
  // are started yet.
  RunLoopForTimePeriod(base::TimeDelta::FromMilliseconds(
      TransportConnectJobHelper::kIPv6FallbackTimerInMs));
  // Now we have |kMaxSockets| IPv6 sockets and one IPv4 socket stalled in
  // connect, and |kMaxSockets - 1| IPv4 sockets waiting for the endpoint lock.
  pool_.FlushWithError(ERR_FAILED);
  for (int i = 0; i < kMaxSockets; ++i) {
    EXPECT_EQ(ERR_FAILED, request(i)->WaitForResult());
  }
}

// Sockets that have had ownership transferred to a ClientSocketHandle should
// not be affected by FlushWithError.
TEST_F(WebSocketTransportClientSocketPoolTest,
       FlushWithErrorDoesNotAffectHandedOutSockets) {
  host_resolver_->set_synchronous_mode(true);
  MockTransportClientSocketFactory::ClientSocketType socket_types[] = {
      MockTransportClientSocketFactory::MOCK_CLIENT_SOCKET,
      MockTransportClientSocketFactory::MOCK_STALLED_CLIENT_SOCKET};
  client_socket_factory_.set_client_socket_types(socket_types,
                                                 arraysize(socket_types));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  // Socket has been "handed out".
  EXPECT_TRUE(request(0)->handle()->socket());

  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  // Now we have one socket handed out, and one pending.
  pool_.FlushWithError(ERR_FAILED);
  EXPECT_EQ(ERR_FAILED, request(1)->WaitForResult());
  // Socket owned by ClientSocketHandle is unaffected:
  EXPECT_TRUE(request(0)->handle()->socket());
  // Return it to the pool (which deletes it).
  request(0)->handle()->Reset();
}

// Sockets should not be leaked if CancelRequest() is called in between
// SetSocket() being called on the ClientSocketHandle and InvokeUserCallback().
TEST_F(WebSocketTransportClientSocketPoolTest, CancelRequestReclaimsSockets) {
  host_resolver_->set_synchronous_mode(true);
  MockTransportClientSocketFactory::ClientSocketType socket_types[] = {
      MockTransportClientSocketFactory::MOCK_TRIGGERABLE_CLIENT_SOCKET,
      MockTransportClientSocketFactory::MOCK_CLIENT_SOCKET};

  client_socket_factory_.set_client_socket_types(socket_types,
                                                 arraysize(socket_types));

  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));

  base::Closure connect_trigger =
      client_socket_factory_.WaitForTriggerableSocketCreation();

  connect_trigger.Run();  // Calls InvokeUserCallbackLater()

  request(0)->handle()->Reset();  // calls CancelRequest()

  RunUntilIdle();
  // We should now be able to create a new connection without blocking on the
  // endpoint lock.
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
}

// A handshake completing and then the WebSocket closing should only release one
// Endpoint, not two.
TEST_F(WebSocketTransportClientSocketPoolTest, EndpointLockIsOnlyReleasedOnce) {
  host_resolver_->set_synchronous_mode(true);
  ASSERT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  // First socket completes handshake.
  WebSocketTransportClientSocketPool::UnlockEndpoint(request(0)->handle());
  RunUntilIdle();
  // First socket is closed.
  request(0)->handle()->Reset();
  // Second socket should have been released.
  EXPECT_EQ(OK, request(1)->WaitForResult());
  // Third socket should still be waiting for endpoint.
  ASSERT_FALSE(request(2)->handle()->is_initialized());
  EXPECT_EQ(LOAD_STATE_WAITING_FOR_AVAILABLE_SOCKET,
            request(2)->handle()->GetLoadState());
}

}  // namespace

}  // namespace net
