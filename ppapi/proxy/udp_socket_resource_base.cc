// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/udp_socket_resource_base.h"

#include <algorithm>
#include <cstring>

#include "base/logging.h"
#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/proxy/error_conversion.h"
#include "ppapi/proxy/plugin_globals.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/shared_impl/socket_option_data.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/resource_creation_api.h"

namespace ppapi {
namespace proxy {

const int32_t UDPSocketResourceBase::kMaxReadSize = 128 * 1024;
const int32_t UDPSocketResourceBase::kMaxWriteSize = 128 * 1024;
const int32_t UDPSocketResourceBase::kMaxSendBufferSize =
    1024 * UDPSocketResourceBase::kMaxWriteSize;
const int32_t UDPSocketResourceBase::kMaxReceiveBufferSize =
    1024 * UDPSocketResourceBase::kMaxReadSize;
const size_t UDPSocketResourceBase::kPluginReceiveBufferSlots = 32u;
const size_t UDPSocketResourceBase::kPluginSendBufferSlots = 8u;

UDPSocketResourceBase::UDPSocketResourceBase(Connection connection,
                                             PP_Instance instance,
                                             bool private_api)
    : PluginResource(connection, instance),
      private_api_(private_api),
      bind_called_(false),
      bound_(false),
      closed_(false),
      read_buffer_(NULL),
      bytes_to_read_(-1),
      recvfrom_addr_resource_(NULL) {
  recvfrom_addr_.size = 0;
  memset(recvfrom_addr_.data, 0,
         arraysize(recvfrom_addr_.data) * sizeof(*recvfrom_addr_.data));
  bound_addr_.size = 0;
  memset(bound_addr_.data, 0,
         arraysize(bound_addr_.data) * sizeof(*bound_addr_.data));

  if (private_api)
    SendCreate(BROWSER, PpapiHostMsg_UDPSocket_CreatePrivate());
  else
    SendCreate(BROWSER, PpapiHostMsg_UDPSocket_Create());

  PluginGlobals::Get()->resource_reply_thread_registrar()->HandleOnIOThread(
      PpapiPluginMsg_UDPSocket_PushRecvResult::ID);
}

UDPSocketResourceBase::~UDPSocketResourceBase() {
}

int32_t UDPSocketResourceBase::SetOptionImpl(
    PP_UDPSocket_Option name,
    const PP_Var& value,
    bool check_bind_state,
    scoped_refptr<TrackedCallback> callback) {
  if (closed_)
    return PP_ERROR_FAILED;

  // Check if socket is expected to be bound or not according to the option.
  switch (name) {
    case PP_UDPSOCKET_OPTION_ADDRESS_REUSE:
    case PP_UDPSOCKET_OPTION_BROADCAST:
    case PP_UDPSOCKET_OPTION_MULTICAST_LOOP:
    case PP_UDPSOCKET_OPTION_MULTICAST_TTL: {
      if ((check_bind_state || name == PP_UDPSOCKET_OPTION_ADDRESS_REUSE) &&
          bind_called_) {
        // SetOption should fail in this case in order to give predictable
        // behavior while binding. Note that we use |bind_called_| rather
        // than |bound_| since the latter is only set on successful completion
        // of Bind().
        return PP_ERROR_FAILED;
      }
      break;
    }
    case PP_UDPSOCKET_OPTION_SEND_BUFFER_SIZE:
    case PP_UDPSOCKET_OPTION_RECV_BUFFER_SIZE: {
      if (check_bind_state && !bound_)
        return PP_ERROR_FAILED;
      break;
    }
  }

  SocketOptionData option_data;
  switch (name) {
    case PP_UDPSOCKET_OPTION_ADDRESS_REUSE:
    case PP_UDPSOCKET_OPTION_BROADCAST:
    case PP_UDPSOCKET_OPTION_MULTICAST_LOOP: {
      if (value.type != PP_VARTYPE_BOOL)
        return PP_ERROR_BADARGUMENT;
      option_data.SetBool(PP_ToBool(value.value.as_bool));
      break;
    }
    case PP_UDPSOCKET_OPTION_SEND_BUFFER_SIZE:
    case PP_UDPSOCKET_OPTION_RECV_BUFFER_SIZE: {
      if (value.type != PP_VARTYPE_INT32)
        return PP_ERROR_BADARGUMENT;
      option_data.SetInt32(value.value.as_int);
      break;
    }
    case PP_UDPSOCKET_OPTION_MULTICAST_TTL: {
      int32_t ival = value.value.as_int;
      if (value.type != PP_VARTYPE_INT32 && (ival < 0 || ival > 255))
        return PP_ERROR_BADARGUMENT;
      option_data.SetInt32(ival);
      break;
    }
    default: {
      NOTREACHED();
      return PP_ERROR_BADARGUMENT;
    }
  }

  Call<PpapiPluginMsg_UDPSocket_SetOptionReply>(
      BROWSER,
      PpapiHostMsg_UDPSocket_SetOption(name, option_data),
      base::Bind(&UDPSocketResourceBase::OnPluginMsgGeneralReply,
                 base::Unretained(this),
                 callback),
      callback);
  return PP_OK_COMPLETIONPENDING;
}

int32_t UDPSocketResourceBase::BindImpl(
    const PP_NetAddress_Private* addr,
    scoped_refptr<TrackedCallback> callback) {
  if (!addr)
    return PP_ERROR_BADARGUMENT;
  if (bound_ || closed_)
    return PP_ERROR_FAILED;
  if (TrackedCallback::IsPending(bind_callback_))
    return PP_ERROR_INPROGRESS;

  bind_called_ = true;
  bind_callback_ = callback;

  // Send the request, the browser will call us back via BindReply.
  Call<PpapiPluginMsg_UDPSocket_BindReply>(
      BROWSER,
      PpapiHostMsg_UDPSocket_Bind(*addr),
      base::Bind(&UDPSocketResourceBase::OnPluginMsgBindReply,
                 base::Unretained(this)),
      callback);
  return PP_OK_COMPLETIONPENDING;
}

PP_Bool UDPSocketResourceBase::GetBoundAddressImpl(
    PP_NetAddress_Private* addr) {
  if (!addr || !bound_ || closed_)
    return PP_FALSE;

  *addr = bound_addr_;
  return PP_TRUE;
}

int32_t UDPSocketResourceBase::RecvFromImpl(
    char* buffer,
    int32_t num_bytes,
    PP_Resource* addr,
    scoped_refptr<TrackedCallback> callback) {
  if (!buffer || num_bytes <= 0)
    return PP_ERROR_BADARGUMENT;
  if (!bound_)
    return PP_ERROR_FAILED;
  if (TrackedCallback::IsPending(recvfrom_callback_))
    return PP_ERROR_INPROGRESS;

  if (recv_buffers_.empty()) {
    read_buffer_ = buffer;
    bytes_to_read_ = std::min(num_bytes, kMaxReadSize);
    recvfrom_addr_resource_ = addr;
    recvfrom_callback_ = callback;

    return PP_OK_COMPLETIONPENDING;
  } else {
    RecvBuffer& front = recv_buffers_.front();

    if (num_bytes < static_cast<int32_t>(front.data.size()))
      return PP_ERROR_MESSAGE_TOO_BIG;

    int32_t result = SetRecvFromOutput(front.result, front.data, front.addr,
                                       buffer, num_bytes, addr);

    recv_buffers_.pop();
    Post(BROWSER, PpapiHostMsg_UDPSocket_RecvSlotAvailable());

    return result;
  }
}

PP_Bool UDPSocketResourceBase::GetRecvFromAddressImpl(
    PP_NetAddress_Private* addr) {
  if (!addr)
    return PP_FALSE;
  *addr = recvfrom_addr_;
  return PP_TRUE;
}

int32_t UDPSocketResourceBase::SendToImpl(
    const char* buffer,
    int32_t num_bytes,
    const PP_NetAddress_Private* addr,
    scoped_refptr<TrackedCallback> callback) {
  if (!buffer || num_bytes <= 0 || !addr)
    return PP_ERROR_BADARGUMENT;
  if (!bound_)
    return PP_ERROR_FAILED;
  if (sendto_callbacks_.size() == kPluginSendBufferSlots)
    return PP_ERROR_INPROGRESS;

  if (num_bytes > kMaxWriteSize)
    num_bytes = kMaxWriteSize;

  sendto_callbacks_.push(callback);

  // Send the request, the browser will call us back via SendToReply.
  Call<PpapiPluginMsg_UDPSocket_SendToReply>(
      BROWSER,
      PpapiHostMsg_UDPSocket_SendTo(std::string(buffer, num_bytes), *addr),
      base::Bind(&UDPSocketResourceBase::OnPluginMsgSendToReply,
                 base::Unretained(this)),
      callback);
  return PP_OK_COMPLETIONPENDING;
}

void UDPSocketResourceBase::CloseImpl() {
  if(closed_)
    return;

  bound_ = false;
  closed_ = true;

  Post(BROWSER, PpapiHostMsg_UDPSocket_Close());

  PostAbortIfNecessary(&bind_callback_);
  PostAbortIfNecessary(&recvfrom_callback_);
  while (!sendto_callbacks_.empty()) {
    scoped_refptr<TrackedCallback> callback = sendto_callbacks_.front();
    sendto_callbacks_.pop();
    PostAbortIfNecessary(&callback);
  }

  read_buffer_ = NULL;
  bytes_to_read_ = -1;
}

int32_t UDPSocketResourceBase::JoinGroupImpl(
    const PP_NetAddress_Private *group,
    scoped_refptr<TrackedCallback> callback) {
  DCHECK(group);

  Call<PpapiPluginMsg_UDPSocket_JoinGroupReply>(
      BROWSER,
      PpapiHostMsg_UDPSocket_JoinGroup(*group),
      base::Bind(&UDPSocketResourceBase::OnPluginMsgGeneralReply,
                 base::Unretained(this),
                 callback),
      callback);
  return PP_OK_COMPLETIONPENDING;
}

int32_t UDPSocketResourceBase::LeaveGroupImpl(
    const PP_NetAddress_Private *group,
    scoped_refptr<TrackedCallback> callback) {
  DCHECK(group);

  Call<PpapiPluginMsg_UDPSocket_LeaveGroupReply>(
      BROWSER,
      PpapiHostMsg_UDPSocket_LeaveGroup(*group),
      base::Bind(&UDPSocketResourceBase::OnPluginMsgGeneralReply,
                 base::Unretained(this),
                 callback),
      callback);
  return PP_OK_COMPLETIONPENDING;
}

void UDPSocketResourceBase::OnReplyReceived(
    const ResourceMessageReplyParams& params,
    const IPC::Message& msg) {
  PPAPI_BEGIN_MESSAGE_MAP(UDPSocketResourceBase, msg)
    PPAPI_DISPATCH_PLUGIN_RESOURCE_CALL(
        PpapiPluginMsg_UDPSocket_PushRecvResult,
        OnPluginMsgPushRecvResult)
    PPAPI_DISPATCH_PLUGIN_RESOURCE_CALL_UNHANDLED(
        PluginResource::OnReplyReceived(params, msg))
  PPAPI_END_MESSAGE_MAP()
}

void UDPSocketResourceBase::PostAbortIfNecessary(
    scoped_refptr<TrackedCallback>* callback) {
  if (TrackedCallback::IsPending(*callback))
    (*callback)->PostAbort();
}

void UDPSocketResourceBase::OnPluginMsgGeneralReply(
    scoped_refptr<TrackedCallback> callback,
    const ResourceMessageReplyParams& params) {
  if (TrackedCallback::IsPending(callback))
    RunCallback(callback, params.result());
}

void UDPSocketResourceBase::OnPluginMsgBindReply(
    const ResourceMessageReplyParams& params,
    const PP_NetAddress_Private& bound_addr) {
  // It is possible that |bind_callback_| is pending while |closed_| is true:
  // CloseImpl() has been called, but a BindReply came earlier than the task to
  // abort |bind_callback_|. We don't want to update |bound_| or |bound_addr_|
  // in that case.
  if (!TrackedCallback::IsPending(bind_callback_) || closed_)
    return;

  if (params.result() == PP_OK)
    bound_ = true;
  bound_addr_ = bound_addr;
  RunCallback(bind_callback_, params.result());
}

void UDPSocketResourceBase::OnPluginMsgPushRecvResult(
    const ResourceMessageReplyParams& params,
    int32_t result,
    const std::string& data,
    const PP_NetAddress_Private& addr) {
  // TODO(yzshen): Support passing in a non-const string ref, so that we can
  // eliminate one copy when storing the data in the buffer.

  DCHECK_LT(recv_buffers_.size(), kPluginReceiveBufferSlots);

  if (!TrackedCallback::IsPending(recvfrom_callback_) || !read_buffer_) {
    recv_buffers_.push(RecvBuffer());
    RecvBuffer& back = recv_buffers_.back();
    back.result = result;
    back.data = data;
    back.addr = addr;

    return;
  }

  DCHECK_EQ(recv_buffers_.size(), 0u);

  if (bytes_to_read_ < static_cast<int32_t>(data.size())) {
    recv_buffers_.push(RecvBuffer());
    RecvBuffer& back = recv_buffers_.back();
    back.result = result;
    back.data = data;
    back.addr = addr;

    result = PP_ERROR_MESSAGE_TOO_BIG;
  } else {
    result = SetRecvFromOutput(result, data, addr, read_buffer_, bytes_to_read_,
                               recvfrom_addr_resource_);
    Post(BROWSER, PpapiHostMsg_UDPSocket_RecvSlotAvailable());
  }

  read_buffer_ = NULL;
  bytes_to_read_ = -1;
  recvfrom_addr_resource_ = NULL;

  RunCallback(recvfrom_callback_, result);
}

void UDPSocketResourceBase::OnPluginMsgSendToReply(
    const ResourceMessageReplyParams& params,
    int32_t bytes_written) {
  // This can be empty if the socket was closed, but there are still tasks
  // to be posted for this resource.
  if (sendto_callbacks_.empty())
    return;

  scoped_refptr<TrackedCallback> callback = sendto_callbacks_.front();
  sendto_callbacks_.pop();
  if (!TrackedCallback::IsPending(callback))
    return;

  if (params.result() == PP_OK)
    RunCallback(callback, bytes_written);
  else
    RunCallback(callback, params.result());
}

void UDPSocketResourceBase::RunCallback(scoped_refptr<TrackedCallback> callback,
                                        int32_t pp_result) {
  callback->Run(ConvertNetworkAPIErrorForCompatibility(pp_result,
                                                       private_api_));
}

int32_t UDPSocketResourceBase::SetRecvFromOutput(
    int32_t browser_result,
    const std::string& data,
    const PP_NetAddress_Private& addr,
    char* output_buffer,
    int32_t num_bytes,
    PP_Resource* output_addr) {
  DCHECK_GE(num_bytes, static_cast<int32_t>(data.size()));

  int32_t result = browser_result;
  if (result == PP_OK && output_addr) {
    thunk::EnterResourceCreationNoLock enter(pp_instance());
    if (enter.succeeded()) {
      *output_addr = enter.functions()->CreateNetAddressFromNetAddressPrivate(
          pp_instance(), addr);
    } else {
      result = PP_ERROR_FAILED;
    }
  }

  if (result == PP_OK && !data.empty())
    memcpy(output_buffer, data.c_str(), data.size());

  recvfrom_addr_ = addr;

  return result == PP_OK ? static_cast<int32_t>(data.size()) : result;
}

}  // namespace proxy
}  // namespace ppapi
