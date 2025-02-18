// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_interceptor.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/message_loop/message_loop.h"
#include "base/prefs/pref_service.h"
#include "base/run_loop.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_config_test_utils.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_configurator.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_io_data.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_test_utils.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_params.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_params_test_utils.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_pref_names.h"
#include "net/base/capturing_net_log.h"
#include "net/base/net_errors.h"
#include "net/base/request_priority.h"
#include "net/http/http_response_headers.h"
#include "net/proxy/proxy_server.h"
#include "net/socket/socket_test_util.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context_storage.h"
#include "net/url_request/url_request_intercepting_job_factory.h"
#include "net/url_request/url_request_interceptor.h"
#include "net/url_request/url_request_job.h"
#include "net/url_request/url_request_job_factory.h"
#include "net/url_request/url_request_job_factory_impl.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

using net::MockRead;

namespace data_reduction_proxy {

namespace {

class CountingURLRequestInterceptor : public net::URLRequestInterceptor {
 public:
  CountingURLRequestInterceptor()
      : request_count_(0), redirect_count_(0), response_count_(0) {
  }

  // URLRequestInterceptor implementation:
  net::URLRequestJob* MaybeInterceptRequest(
      net::URLRequest* request,
      net::NetworkDelegate* network_delegate) const override {
    request_count_++;
    return nullptr;
  }

  net::URLRequestJob* MaybeInterceptRedirect(
      net::URLRequest* request,
      net::NetworkDelegate* network_delegate,
      const GURL& location) const override {
    redirect_count_++;
    return nullptr;
  }

  net::URLRequestJob* MaybeInterceptResponse(
      net::URLRequest* request,
      net::NetworkDelegate* network_delegate) const override {
    response_count_++;
    return nullptr;
  }

  int request_count() const {
    return request_count_;
  }

  int redirect_count() const {
    return redirect_count_;
  }

  int response_count() const {
    return response_count_;
  }

 private:
  mutable int request_count_;
  mutable int redirect_count_;
  mutable int response_count_;
};

class TestURLRequestContextWithDataReductionProxy
    : public net::TestURLRequestContext {
 public:
  TestURLRequestContextWithDataReductionProxy(DataReductionProxyConfig* config,
                                              net::NetworkDelegate* delegate)
      : net::TestURLRequestContext(true) {
    std::string proxy = config->Origin().ToURI();
    context_storage_.set_proxy_service(net::ProxyService::CreateFixed(proxy));
    set_network_delegate(delegate);
  }

  ~TestURLRequestContextWithDataReductionProxy() override {}
};

class DataReductionProxyInterceptorTest : public testing::Test {
 public:
  DataReductionProxyInterceptorTest() {
    test_context_ =
        DataReductionProxyTestContext::Builder()
            .WithParamsFlags(DataReductionProxyParams::kAllowed)
            .WithParamsDefinitions(TestDataReductionProxyParams::HAS_EVERYTHING)
            .Build();
    default_context_.reset(
        new TestURLRequestContextWithDataReductionProxy(
            test_context_->config(), &default_network_delegate_));
    default_context_->set_network_delegate(&default_network_delegate_);
    default_context_->set_net_log(test_context_->net_log());
  }

  ~DataReductionProxyInterceptorTest() override {
    // URLRequestJobs may post clean-up tasks on destruction.
    base::RunLoop().RunUntilIdle();
  }

  void Init(scoped_ptr<net::URLRequestJobFactory> factory) {
    job_factory_ = factory.Pass();
    default_context_->set_job_factory(job_factory_.get());
    default_context_->Init();
  }

  scoped_ptr<DataReductionProxyTestContext> test_context_;
  net::TestNetworkDelegate default_network_delegate_;
  scoped_ptr<net::URLRequestJobFactory> job_factory_;
  scoped_ptr<net::TestURLRequestContext> default_context_;
};

TEST_F(DataReductionProxyInterceptorTest, TestJobFactoryChaining) {
  // Verifies that job factories can be chained.
  scoped_ptr<net::URLRequestJobFactory> impl(
      new net::URLRequestJobFactoryImpl());

  CountingURLRequestInterceptor* interceptor2 =
      new CountingURLRequestInterceptor();
  scoped_ptr<net::URLRequestJobFactory> factory2(
      new net::URLRequestInterceptingJobFactory(
          impl.Pass(), make_scoped_ptr(interceptor2)));

  CountingURLRequestInterceptor* interceptor1 =
      new CountingURLRequestInterceptor();
  scoped_ptr<net::URLRequestJobFactory> factory1(
      new net::URLRequestInterceptingJobFactory(
          factory2.Pass(), make_scoped_ptr(interceptor1)));

  Init(factory1.Pass());

  net::TestDelegate d;
  scoped_ptr<net::URLRequest> req(default_context_->CreateRequest(
      GURL("http://foo"), net::DEFAULT_PRIORITY, &d, nullptr));

  req->Start();
  base::RunLoop().Run();
  EXPECT_EQ(1, interceptor1->request_count());
  EXPECT_EQ(0, interceptor1->redirect_count());
  EXPECT_EQ(1, interceptor1->response_count());
  EXPECT_EQ(1, interceptor2->request_count());
  EXPECT_EQ(0, interceptor2->redirect_count());
  EXPECT_EQ(1, interceptor2->response_count());
}

class DataReductionProxyInterceptorWithServerTest : public testing::Test {
 public:
  DataReductionProxyInterceptorWithServerTest()
      : context_(true) {
    context_.set_network_delegate(&network_delegate_);
    context_.set_net_log(&net_log_);
  }

  ~DataReductionProxyInterceptorWithServerTest() override {
    test_context_->io_data()->ShutdownOnUIThread();
    // URLRequestJobs may post clean-up tasks on destruction.
    test_context_->RunUntilIdle();
  }

  void SetUp() override {
    base::FilePath root_path, proxy_file_path, direct_file_path;
    PathService::Get(base::DIR_SOURCE_ROOT, &root_path);
    proxy_file_path = root_path.AppendASCII(
        "components/test/data/data_reduction_proxy/proxy");
    direct_file_path = root_path.AppendASCII(
        "components/test/data/data_reduction_proxy/direct");
    proxy_.ServeFilesFromDirectory(proxy_file_path);
    direct_.ServeFilesFromDirectory(direct_file_path);
    ASSERT_TRUE(proxy_.InitializeAndWaitUntilReady());
    ASSERT_TRUE(direct_.InitializeAndWaitUntilReady());

    test_context_ =
        DataReductionProxyTestContext::Builder()
            .WithParamsFlags(DataReductionProxyParams::kAllowed)
            .WithParamsDefinitions(
                TestDataReductionProxyParams::HAS_EVERYTHING &
                    ~TestDataReductionProxyParams::HAS_DEV_ORIGIN &
                    ~TestDataReductionProxyParams::HAS_DEV_FALLBACK_ORIGIN)
            .WithURLRequestContext(&context_)
            .Build();
    std::string spec;
    base::TrimString(proxy_.GetURL("/").spec(), "/", &spec);
    test_context_->config()->test_params()->set_origin(
        net::ProxyServer::FromURI(spec, net::ProxyServer::SCHEME_HTTP));
    std::string proxy_name =
        test_context_->config()->Origin().ToURI();
    proxy_service_.reset(
        net::ProxyService::CreateFixedFromPacResult(
            "PROXY " + proxy_name + "; DIRECT"));

    context_.set_proxy_service(proxy_service_.get());

    scoped_ptr<net::URLRequestJobFactoryImpl> job_factory_impl(
        new net::URLRequestJobFactoryImpl());
    job_factory_.reset(new net::URLRequestInterceptingJobFactory(
        job_factory_impl.Pass(),
        test_context_->io_data()->CreateInterceptor()));
    context_.set_job_factory(job_factory_.get());
    context_.Init();
  }

  const net::TestURLRequestContext& context() {
    return context_;
  }

  const net::test_server::EmbeddedTestServer& direct() {
    return direct_;
  }

 private:
  net::CapturingNetLog net_log_;
  net::TestNetworkDelegate network_delegate_;
  net::TestURLRequestContext context_;
  net::test_server::EmbeddedTestServer proxy_;
  net::test_server::EmbeddedTestServer direct_;
  scoped_ptr<net::ProxyService> proxy_service_;
  scoped_ptr<net::URLRequestJobFactory> job_factory_;
  scoped_ptr<DataReductionProxyTestContext> test_context_;
};

TEST_F(DataReductionProxyInterceptorWithServerTest, TestBypass) {
  // Tests the mechanics of proxy bypass work with a "real" server. For tests
  // that cover every imaginable response that could trigger a bypass, see:
  // DataReductionProxyProtocolTest.
  net::TestDelegate delegate;
  scoped_ptr<net::URLRequest> request(
      context().CreateRequest(direct().GetURL("/block10.html"),
                             net::DEFAULT_PRIORITY, &delegate, NULL));
  request->Start();
  EXPECT_TRUE(request->is_pending());
  base::RunLoop().Run();

  EXPECT_EQ(net::URLRequestStatus::SUCCESS, request->status().status());
  EXPECT_EQ(net::OK, request->status().error());
  EXPECT_EQ("hello", delegate.data_received());
}

TEST_F(DataReductionProxyInterceptorWithServerTest, TestNoBypass) {
  net::TestDelegate delegate;
  scoped_ptr<net::URLRequest> request(
      context().CreateRequest(direct().GetURL("/noblock.html"),
                             net::DEFAULT_PRIORITY, &delegate, NULL));
  request->Start();
  EXPECT_TRUE(request->is_pending());
  base::RunLoop().Run();

  EXPECT_EQ(net::URLRequestStatus::SUCCESS, request->status().status());
  EXPECT_EQ(net::OK, request->status().error());
  EXPECT_EQ("hello", delegate.data_received());
}

class DataReductionProxyInterceptorEndToEndTest : public testing::Test {
 public:
  DataReductionProxyInterceptorEndToEndTest()
      : context_(true), context_storage_(&context_) {}

  ~DataReductionProxyInterceptorEndToEndTest() override {}

  void SetUp() override {
    drp_test_context_ =
        DataReductionProxyTestContext::Builder()
            .WithParamsFlags(DataReductionProxyParams::kAllowed |
                             DataReductionProxyParams::kFallbackAllowed)
            .WithParamsDefinitions(
                 TestDataReductionProxyParams::HAS_EVERYTHING &
                 ~TestDataReductionProxyParams::HAS_DEV_ORIGIN &
                 ~TestDataReductionProxyParams::HAS_DEV_FALLBACK_ORIGIN)
            .WithURLRequestContext(&context_)
            .WithMockClientSocketFactory(&mock_socket_factory_)
            .Build();
    drp_test_context_->AttachToURLRequestContext(&context_storage_);
    context_.set_client_socket_factory(&mock_socket_factory_);
    context_.Init();
    drp_test_context_->EnableDataReductionProxyWithSecureProxyCheckSuccess();

    // Three proxies should be available for use: primary, fallback, and direct.
    const net::ProxyConfig& proxy_config =
        drp_test_context_->configurator()->GetProxyConfigOnIOThread();
    EXPECT_EQ(3U, proxy_config.proxy_rules().proxies_for_http.size());
  }

  // Creates a URLRequest using the test's TestURLRequestContext and executes
  // it. Returns the created URLRequest.
  scoped_ptr<net::URLRequest> CreateAndExecuteRequest(const GURL& url) {
    scoped_ptr<net::URLRequest> request(
        context_.CreateRequest(url, net::IDLE, &delegate_, NULL));
    request->Start();
    drp_test_context_->RunUntilIdle();
    return request.Pass();
  }

  const net::TestDelegate& delegate() const {
    return delegate_;
  }

  net::MockClientSocketFactory* mock_socket_factory() {
    return &mock_socket_factory_;
  }

  DataReductionProxyConfig* config() const {
    return drp_test_context_->config();
  }

 private:
  net::TestDelegate delegate_;
  net::MockClientSocketFactory mock_socket_factory_;
  net::TestURLRequestContext context_;
  net::URLRequestContextStorage context_storage_;
  scoped_ptr<DataReductionProxyTestContext> drp_test_context_;
};

const std::string kBody = "response body";

TEST_F(DataReductionProxyInterceptorEndToEndTest, ResponseWithoutRetry) {
  // The response comes through the proxy and should not be retried.
  MockRead mock_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\nVia: 1.1 Chrome-Compression-Proxy\r\n\r\n"),
      MockRead(kBody.c_str()),
      MockRead(net::SYNCHRONOUS, net::OK),
  };
  net::StaticSocketDataProvider socket_data_provider(
      mock_reads, arraysize(mock_reads), nullptr, 0);
  mock_socket_factory()->AddSocketDataProvider(&socket_data_provider);

  scoped_ptr<net::URLRequest> request =
      CreateAndExecuteRequest(GURL("http://foo.com"));

  EXPECT_EQ(net::URLRequestStatus::SUCCESS, request->status().status());
  EXPECT_EQ(200, request->GetResponseCode());
  EXPECT_EQ(kBody, delegate().data_received());
  EXPECT_EQ(config()->Origin().host_port_pair().ToString(),
            request->proxy_server().ToString());
}

TEST_F(DataReductionProxyInterceptorEndToEndTest, RedirectWithoutRetry) {
  // The redirect comes through the proxy and should not be retried.
  MockRead redirect_mock_reads[] = {
      MockRead("HTTP/1.1 302 Found\r\n"
               "Via: 1.1 Chrome-Compression-Proxy\r\n"
               "Location: http://bar.com/\r\n\r\n"),
      MockRead(""),
      MockRead(net::SYNCHRONOUS, net::OK),
  };
  net::StaticSocketDataProvider redirect_socket_data_provider(
      redirect_mock_reads, arraysize(redirect_mock_reads), nullptr, 0);
  mock_socket_factory()->AddSocketDataProvider(&redirect_socket_data_provider);

  // The response after the redirect comes through proxy and should not be
  // retried.
  MockRead response_mock_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\nVia: 1.1 Chrome-Compression-Proxy\r\n\r\n"),
      MockRead(kBody.c_str()),
      MockRead(net::SYNCHRONOUS, net::OK),
  };
  net::StaticSocketDataProvider response_socket_data_provider(
      response_mock_reads, arraysize(response_mock_reads), nullptr, 0);
  mock_socket_factory()->AddSocketDataProvider(&response_socket_data_provider);

  scoped_ptr<net::URLRequest> request =
      CreateAndExecuteRequest(GURL("http://foo.com"));

  EXPECT_EQ(net::URLRequestStatus::SUCCESS, request->status().status());
  EXPECT_EQ(200, request->GetResponseCode());
  EXPECT_EQ(kBody, delegate().data_received());
  EXPECT_EQ(config()->Origin().host_port_pair().ToString(),
            request->proxy_server().ToString());
  // The redirect should have been processed and followed normally.
  EXPECT_EQ(1, delegate().received_redirect_count());
}

TEST_F(DataReductionProxyInterceptorEndToEndTest, ResponseWithBypassAndRetry) {
  // The first try gives a bypass.
  MockRead initial_mock_reads[] = {
      MockRead("HTTP/1.1 502 Bad Gateway\r\n"
               "Via: 1.1 Chrome-Compression-Proxy\r\n"
               "Chrome-Proxy: block-once\r\n\r\n"),
      MockRead(""),
      MockRead(net::SYNCHRONOUS, net::OK),
  };
  net::StaticSocketDataProvider initial_socket_data_provider(
      initial_mock_reads, arraysize(initial_mock_reads), nullptr, 0);
  mock_socket_factory()->AddSocketDataProvider(&initial_socket_data_provider);

  // The retry after the bypass is successful.
  MockRead retry_mock_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n\r\n"),
      MockRead(kBody.c_str()),
      MockRead(net::SYNCHRONOUS, net::OK),
  };
  net::StaticSocketDataProvider retry_socket_data_provider(
      retry_mock_reads, arraysize(retry_mock_reads), nullptr, 0);
  mock_socket_factory()->AddSocketDataProvider(&retry_socket_data_provider);

  scoped_ptr<net::URLRequest> request = CreateAndExecuteRequest(
      GURL("http://foo.com"));

  EXPECT_EQ(net::URLRequestStatus::SUCCESS, request->status().status());
  EXPECT_EQ(200, request->GetResponseCode());
  EXPECT_EQ(kBody, delegate().data_received());
  EXPECT_FALSE(request->was_fetched_via_proxy());
  // The bypassed response should have been intercepted before the response was
  // processed, so only the final response after the retry should have been
  // processed.
  EXPECT_EQ(1, delegate().response_started_count());
}

TEST_F(DataReductionProxyInterceptorEndToEndTest, RedirectWithBypassAndRetry) {
  MockRead mock_reads_array[][3] = {
      // First, get a redirect without a via header, which should be retried
      // using the fallback proxy.
      {
          MockRead("HTTP/1.1 302 Found\r\n"
                   "Location: http://bar.com/\r\n\r\n"),
          MockRead(""),
          MockRead(net::SYNCHRONOUS, net::OK),
      },
      // Same as before, but through the fallback proxy. Now both proxies are
      // bypassed, and the request should be retried over direct.
      {
          MockRead("HTTP/1.1 302 Found\r\n"
                   "Location: http://baz.com/\r\n\r\n"),
          MockRead(""),
          MockRead(net::SYNCHRONOUS, net::OK),
      },
      // Finally, a successful response is received.
      {
          MockRead("HTTP/1.1 200 OK\r\n\r\n"),
          MockRead(kBody.c_str()),
          MockRead(net::SYNCHRONOUS, net::OK),
      },
  };
  ScopedVector<net::SocketDataProvider> socket_data_providers;
  for (MockRead* mock_reads : mock_reads_array) {
    socket_data_providers.push_back(
        new net::StaticSocketDataProvider(mock_reads, 3, nullptr, 0));
    mock_socket_factory()->AddSocketDataProvider(socket_data_providers.back());
  }

  scoped_ptr<net::URLRequest> request =
      CreateAndExecuteRequest(GURL("http://foo.com"));

  EXPECT_EQ(net::URLRequestStatus::SUCCESS, request->status().status());
  EXPECT_EQ(200, request->GetResponseCode());
  EXPECT_EQ(kBody, delegate().data_received());
  EXPECT_FALSE(request->was_fetched_via_proxy());

  // Each of the redirects should have been intercepted before being followed.
  EXPECT_EQ(0, delegate().received_redirect_count());
  EXPECT_EQ(std::vector<GURL>(1, GURL("http://foo.com")), request->url_chain());
}

}  // namespace

}  // namespace data_reduction_proxy
