// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_client_session.h"

#include <vector>

#include "net/base/ip_endpoint.h"
#include "net/quic/crypto/aes_128_gcm_12_encrypter.h"
#include "net/quic/quic_flags.h"
#include "net/quic/test_tools/crypto_test_utils.h"
#include "net/quic/test_tools/quic_session_peer.h"
#include "net/quic/test_tools/quic_test_utils.h"
#include "net/tools/quic/quic_spdy_client_stream.h"
#include "testing/gtest/include/gtest/gtest.h"

using net::test::CryptoTestUtils;
using net::test::DefaultQuicConfig;
using net::test::MockConnection;
using net::test::PacketSavingConnection;
using net::test::QuicSessionPeer;
using net::test::SupportedVersions;
using net::test::TestPeerIPAddress;
using net::test::ValueRestore;
using net::test::kTestPort;
using testing::Invoke;
using testing::_;

namespace net {
namespace tools {
namespace test {
namespace {

const char kServerHostname[] = "www.example.org";
const uint16 kPort = 80;

class ToolsQuicClientSessionTest
    : public ::testing::TestWithParam<QuicVersion> {
 protected:
  ToolsQuicClientSessionTest()
      : connection_(
            new PacketSavingConnection(false, SupportedVersions(GetParam()))) {
    session_.reset(new QuicClientSession(DefaultQuicConfig(), connection_));
    session_->InitializeSession(
        QuicServerId(kServerHostname, kPort, false, PRIVACY_MODE_DISABLED),
        &crypto_config_);
    // Advance the time, because timers do not like uninitialized times.
    connection_->AdvanceTime(QuicTime::Delta::FromSeconds(1));
  }

  void CompleteCryptoHandshake() {
    session_->CryptoConnect();
    CryptoTestUtils::HandshakeWithFakeServer(
        connection_, session_->GetCryptoStream());
  }

  PacketSavingConnection* connection_;
  scoped_ptr<QuicClientSession> session_;
  QuicCryptoClientConfig crypto_config_;
};

INSTANTIATE_TEST_CASE_P(Tests, ToolsQuicClientSessionTest,
                        ::testing::ValuesIn(QuicSupportedVersions()));

TEST_P(ToolsQuicClientSessionTest, CryptoConnect) {
  CompleteCryptoHandshake();
}

TEST_P(ToolsQuicClientSessionTest, MaxNumStreams) {
  session_->config()->SetMaxStreamsPerConnection(1, 1);
  // FLAGS_max_streams_per_connection = 1;
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();

  QuicSpdyClientStream* stream = session_->CreateOutgoingDataStream();
  ASSERT_TRUE(stream);
  EXPECT_FALSE(session_->CreateOutgoingDataStream());

  // Close a stream and ensure I can now open a new one.
  session_->CloseStream(stream->id());
  stream = session_->CreateOutgoingDataStream();
  EXPECT_TRUE(stream);
}

TEST_P(ToolsQuicClientSessionTest, GoAwayReceived) {
  CompleteCryptoHandshake();

  // After receiving a GoAway, I should no longer be able to create outgoing
  // streams.
  session_->OnGoAway(QuicGoAwayFrame(QUIC_PEER_GOING_AWAY, 1u, "Going away."));
  EXPECT_EQ(nullptr, session_->CreateOutgoingDataStream());
}

TEST_P(ToolsQuicClientSessionTest, SetFecProtectionFromConfig) {
  ValueRestore<bool> old_flag(&FLAGS_enable_quic_fec, true);

  // Set FEC config in client's connection options.
  QuicTagVector copt;
  copt.push_back(kFHDR);
  session_->config()->SetConnectionOptionsToSend(copt);

  // Doing the handshake should set up FEC config correctly.
  CompleteCryptoHandshake();

  // Verify that headers stream is always protected and data streams are
  // optionally protected.
  EXPECT_EQ(FEC_PROTECT_ALWAYS,
            QuicSessionPeer::GetHeadersStream(session_.get())->fec_policy());
  QuicSpdyClientStream* stream = session_->CreateOutgoingDataStream();
  ASSERT_TRUE(stream);
  EXPECT_EQ(FEC_PROTECT_OPTIONAL, stream->fec_policy());
}

// Regression test for b/17206611.
TEST_P(ToolsQuicClientSessionTest, InvalidPacketReceived) {
  // Create Packet with 0 length.
  QuicEncryptedPacket invalid_packet(nullptr, 0, false);
  IPEndPoint server_address(TestPeerIPAddress(), kTestPort);
  IPEndPoint client_address(TestPeerIPAddress(), kTestPort);

  EXPECT_CALL(*reinterpret_cast<MockConnection*>(session_->connection()),
              ProcessUdpPacket(server_address, client_address, _))
      .WillRepeatedly(
          Invoke(reinterpret_cast<MockConnection*>(session_->connection()),
                 &MockConnection::ReallyProcessUdpPacket));

  // Validate that empty packets don't close the connection.
  EXPECT_CALL(*connection_, SendConnectionCloseWithDetails(_, _)).Times(0);
  session_->connection()->ProcessUdpPacket(client_address, server_address,
                                           invalid_packet);

  // Verifiy that small, invalid packets don't close the connection.
  char buf[2] = {0x00, 0x01};
  QuicEncryptedPacket valid_packet(buf, 2, false);
  // Close connection shouldn't be called.
  EXPECT_CALL(*connection_, SendConnectionCloseWithDetails(_, _)).Times(0);
  session_->connection()->ProcessUdpPacket(client_address, server_address,
                                           valid_packet);
}

}  // namespace
}  // namespace test
}  // namespace tools
}  // namespace net
