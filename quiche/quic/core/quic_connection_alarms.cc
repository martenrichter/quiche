// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_connection_alarms.h"

#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_context.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

namespace {

// Base class of all alarms owned by a QuicConnection.
class QuicConnectionAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit QuicConnectionAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  QuicConnectionAlarmDelegate(const QuicConnectionAlarmDelegate&) = delete;
  QuicConnectionAlarmDelegate& operator=(const QuicConnectionAlarmDelegate&) =
      delete;

  QuicConnectionContext* GetConnectionContext() override {
    return (connection_ == nullptr) ? nullptr : connection_->context();
  }

 protected:
  QuicConnection* connection_;
};

// An alarm that is scheduled to send an ack if a timeout occurs.
class AckAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->ack_frame_updated());
    QUICHE_DCHECK(connection_->connected());
    QuicConnection::ScopedPacketFlusher flusher(connection_);
    if (connection_->SupportsMultiplePacketNumberSpaces()) {
      connection_->SendAllPendingAcks();
    } else {
      connection_->SendAck();
    }
  }
};

// This alarm will be scheduled any time a data-bearing packet is sent out.
// When the alarm goes off, the connection checks to see if the oldest packets
// have been acked, and retransmit them if they have not.
class RetransmissionAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->OnRetransmissionTimeout();
  }
};

// An alarm that is scheduled when the SentPacketManager requires a delay
// before sending packets and fires when the packet may be sent.
class SendAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->OnSendAlarm();
  }
};

class MtuDiscoveryAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->DiscoverMtu();
  }
};

class ProcessUndecryptablePacketsAlarmDelegate
    : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    QuicConnection::ScopedPacketFlusher flusher(connection_);
    connection_->MaybeProcessUndecryptablePackets();
  }
};

class DiscardPreviousOneRttKeysAlarmDelegate
    : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->DiscardPreviousOneRttKeys();
  }
};

class DiscardZeroRttDecryptionKeysAlarmDelegate
    : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    QUIC_DLOG(INFO) << "0-RTT discard alarm fired";
    connection_->RemoveDecrypter(ENCRYPTION_ZERO_RTT);
    connection_->RetireOriginalDestinationConnectionId();
  }
};

class MultiPortProbingAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    QUIC_DLOG(INFO) << "Alternative path probing alarm fired";
    connection_->MaybeProbeMultiPortPath();
  }
};

}  // namespace

QuicConnectionAlarms::QuicConnectionAlarms(QuicConnection* connection,
                                           QuicAlarmFactory& alarm_factory,
                                           QuicConnectionArena& arena)
    : ack_alarm_(alarm_factory.CreateAlarm(
          arena.New<AckAlarmDelegate>(connection), &arena)),
      retransmission_alarm_(alarm_factory.CreateAlarm(
          arena.New<RetransmissionAlarmDelegate>(connection), &arena)),
      send_alarm_(alarm_factory.CreateAlarm(
          arena.New<SendAlarmDelegate>(connection), &arena)),
      mtu_discovery_alarm_(alarm_factory.CreateAlarm(
          arena.New<MtuDiscoveryAlarmDelegate>(connection), &arena)),
      process_undecryptable_packets_alarm_(alarm_factory.CreateAlarm(
          arena.New<ProcessUndecryptablePacketsAlarmDelegate>(connection),
          &arena)),
      discard_previous_one_rtt_keys_alarm_(alarm_factory.CreateAlarm(
          arena.New<DiscardPreviousOneRttKeysAlarmDelegate>(connection),
          &arena)),
      discard_zero_rtt_decryption_keys_alarm_(alarm_factory.CreateAlarm(
          arena.New<DiscardZeroRttDecryptionKeysAlarmDelegate>(connection),
          &arena)),
      multi_port_probing_alarm_(alarm_factory.CreateAlarm(
          arena.New<MultiPortProbingAlarmDelegate>(connection), &arena)) {}

}  // namespace quic
