// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api/vpn_provider/vpn_provider_api.h"

#include <vector>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "extensions/browser/api/vpn_provider/vpn_service.h"
#include "extensions/browser/api/vpn_provider/vpn_service_factory.h"
#include "extensions/common/api/vpn_provider.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace extensions {

namespace {

namespace api_vpn = extensions::core_api::vpn_provider;

const char kCIDRSeperator[] = "/";

bool CheckIPCIDRSanity(const std::string& value, bool cidr, bool ipv6) {
  int dots = ipv6 ? 0 : 3;
  int sep = cidr ? 1 : 0;
  int colon = ipv6 ? 7 : 0;
  bool hex_allowed = ipv6;
  int counter = 0;

  for (const auto& elem : value) {
    if (IsAsciiDigit(elem)) {
      counter++;
      continue;
    }
    if (elem == '.') {
      if (!dots)
        return false;
      dots--;
    } else if (elem == kCIDRSeperator[0]) {
      if (!sep || dots || colon == 7 || !counter)
        return false;
      // Separator observed, no more dots and colons, only digits are allowed
      // after observing separator. So setting hex_allowed to false.
      sep--;
      counter = 0;
      colon = 0;
      hex_allowed = false;
    } else if (elem == ':') {
      if (!colon)
        return false;
      colon--;
    } else if (!hex_allowed || !IsHexDigit(elem)) {
      return false;
    } else {
      counter++;
    }
  }
  return !sep && !dots && (colon < 7) && counter;
}

bool CheckIPCIDRSanityList(const std::vector<std::string>& list,
                           bool cidr,
                           bool ipv6) {
  for (const auto& address : list) {
    if (!CheckIPCIDRSanity(address, cidr, ipv6)) {
      return false;
    }
  }
  return true;
}

void ConvertParameters(const api_vpn::Parameters& parameters,
                       base::DictionaryValue* parameter_value,
                       std::string* error) {
  if (!CheckIPCIDRSanity(parameters.address, true /* CIDR */,
                         false /*IPV4 */)) {
    *error = "Address CIDR sanity check failed.";
    return;
  }

  if (!CheckIPCIDRSanityList(parameters.exclusion_list, true /* CIDR */,
                             false /*IPV4 */)) {
    *error = "Exclusion list CIDR sanity check failed.";
    return;
  }

  if (!CheckIPCIDRSanityList(parameters.inclusion_list, true /* CIDR */,
                             false /*IPV4 */)) {
    *error = "Inclusion list CIDR sanity check failed.";
    return;
  }

  if (!CheckIPCIDRSanityList(parameters.dns_servers, false /* Not CIDR */,
                             false /*IPV4 */)) {
    *error = "DNS server IP sanity check failed.";
    return;
  }

  std::vector<std::string> cidr_parts;
  CHECK(Tokenize(parameters.address, kCIDRSeperator, &cidr_parts) == 2);

  parameter_value->SetStringWithoutPathExpansion(
      shill::kAddressParameterThirdPartyVpn, cidr_parts[0]);

  parameter_value->SetStringWithoutPathExpansion(
      shill::kSubnetPrefixParameterThirdPartyVpn, cidr_parts[1]);

  parameter_value->SetStringWithoutPathExpansion(
      shill::kExclusionListParameterThirdPartyVpn,
      JoinString(parameters.exclusion_list, shill::kIPDelimiter));

  parameter_value->SetStringWithoutPathExpansion(
      shill::kInclusionListParameterThirdPartyVpn,
      JoinString(parameters.inclusion_list, shill::kIPDelimiter));

  if (parameters.mtu) {
    parameter_value->SetStringWithoutPathExpansion(
        shill::kMtuParameterThirdPartyVpn, *parameters.mtu);
  }

  if (parameters.broadcast_address) {
    parameter_value->SetStringWithoutPathExpansion(
        shill::kBroadcastAddressParameterThirdPartyVpn,
        *parameters.broadcast_address);
  }

  if (parameters.domain_search) {
    parameter_value->SetStringWithoutPathExpansion(
        shill::kDomainSearchParameterThirdPartyVpn,
        JoinString(*parameters.domain_search, shill::kNonIPDelimiter));
  }

  parameter_value->SetStringWithoutPathExpansion(
      shill::kDnsServersParameterThirdPartyVpn,
      JoinString(parameters.dns_servers, shill::kIPDelimiter));

  return;
}

}  // namespace

VpnThreadExtensionFunction::~VpnThreadExtensionFunction() {
}

void VpnThreadExtensionFunction::SignalCallCompletionSuccess() {
  Respond(NoArguments());
}

void VpnThreadExtensionFunction::SignalCallCompletionSuccessWithId(
    const std::string& configuration_id) {
  Respond(OneArgument(new base::StringValue(configuration_id)));
}

void VpnThreadExtensionFunction::SignalCallCompletionFailure(
    const std::string& error_name,
    const std::string& error_message) {
  if (!error_name.empty() && !error_message.empty()) {
    Respond(Error(error_name + ": " + error_message));
  } else if (!error_name.empty()) {
    Respond(Error(error_name));
  } else {
    Respond(Error(error_message));
  }
}

VpnProviderCreateConfigFunction::~VpnProviderCreateConfigFunction() {
}

ExtensionFunction::ResponseAction VpnProviderCreateConfigFunction::Run() {
  scoped_ptr<api_vpn::CreateConfig::Params> params(
      api_vpn::CreateConfig::Params::Create(*args_));
  if (!params) {
    return RespondNow(Error("Invalid arguments."));
  }

  chromeos::VpnService* service =
      chromeos::VpnServiceFactory::GetForBrowserContext(browser_context());
  if (!service) {
    return RespondNow(Error("Invalid profile."));
  }

  // Use the configuration name as ID. In the future, a different ID scheme may
  // be used, requiring a mapping between the two.
  service->CreateConfiguration(
      extension_id(), extension()->name(), params->name,
      base::Bind(
          &VpnProviderCreateConfigFunction::SignalCallCompletionSuccessWithId,
          this, params->name),
      base::Bind(&VpnProviderNotifyConnectionStateChangedFunction::
                     SignalCallCompletionFailure,
                 this));

  return RespondLater();
}

VpnProviderDestroyConfigFunction::~VpnProviderDestroyConfigFunction() {
}

ExtensionFunction::ResponseAction VpnProviderDestroyConfigFunction::Run() {
  scoped_ptr<api_vpn::DestroyConfig::Params> params(
      api_vpn::DestroyConfig::Params::Create(*args_));
  if (!params) {
    return RespondNow(Error("Invalid arguments."));
  }

  chromeos::VpnService* service =
      chromeos::VpnServiceFactory::GetForBrowserContext(browser_context());
  if (!service) {
    return RespondNow(Error("Invalid profile."));
  }

  service->DestroyConfiguration(
      extension_id(), params->id,
      base::Bind(&VpnProviderDestroyConfigFunction::SignalCallCompletionSuccess,
                 this),
      base::Bind(&VpnProviderNotifyConnectionStateChangedFunction::
                     SignalCallCompletionFailure,
                 this));

  return RespondLater();
}

VpnProviderSetParametersFunction::~VpnProviderSetParametersFunction() {
}

ExtensionFunction::ResponseAction VpnProviderSetParametersFunction::Run() {
  scoped_ptr<api_vpn::SetParameters::Params> params(
      api_vpn::SetParameters::Params::Create(*args_));
  if (!params) {
    return RespondNow(Error("Invalid arguments."));
  }

  chromeos::VpnService* service =
      chromeos::VpnServiceFactory::GetForBrowserContext(browser_context());
  if (!service) {
    return RespondNow(Error("Invalid profile."));
  }

  base::DictionaryValue parameter_value;
  std::string error;
  ConvertParameters(params->parameters, &parameter_value, &error);
  if (!error.empty()) {
    return RespondNow(Error(error));
  }

  service->SetParameters(
      extension_id(), parameter_value,
      base::Bind(&VpnProviderSetParametersFunction::SignalCallCompletionSuccess,
                 this),
      base::Bind(&VpnProviderNotifyConnectionStateChangedFunction::
                     SignalCallCompletionFailure,
                 this));

  return RespondLater();
}

VpnProviderSendPacketFunction::~VpnProviderSendPacketFunction() {
}

ExtensionFunction::ResponseAction VpnProviderSendPacketFunction::Run() {
  scoped_ptr<api_vpn::SendPacket::Params> params(
      api_vpn::SendPacket::Params::Create(*args_));
  if (!params) {
    return RespondNow(Error("Invalid arguments."));
  }

  chromeos::VpnService* service =
      chromeos::VpnServiceFactory::GetForBrowserContext(browser_context());
  if (!service) {
    return RespondNow(Error("Invalid profile."));
  }

  service->SendPacket(
      extension_id(), params->data,
      base::Bind(&VpnProviderSendPacketFunction::SignalCallCompletionSuccess,
                 this),
      base::Bind(&VpnProviderNotifyConnectionStateChangedFunction::
                     SignalCallCompletionFailure,
                 this));

  return RespondLater();
}

VpnProviderNotifyConnectionStateChangedFunction::
    ~VpnProviderNotifyConnectionStateChangedFunction() {
}

ExtensionFunction::ResponseAction
VpnProviderNotifyConnectionStateChangedFunction::Run() {
  scoped_ptr<api_vpn::NotifyConnectionStateChanged::Params> params(
      api_vpn::NotifyConnectionStateChanged::Params::Create(*args_));
  if (!params) {
    return RespondNow(Error("Invalid arguments."));
  }

  chromeos::VpnService* service =
      chromeos::VpnServiceFactory::GetForBrowserContext(browser_context());
  if (!service) {
    return RespondNow(Error("Invalid profile."));
  }

  service->NotifyConnectionStateChanged(
      extension_id(), params->state,
      base::Bind(&VpnProviderNotifyConnectionStateChangedFunction::
                     SignalCallCompletionSuccess,
                 this),
      base::Bind(&VpnProviderNotifyConnectionStateChangedFunction::
                     SignalCallCompletionFailure,
                 this));

  return RespondLater();
}

}  // namespace extensions
