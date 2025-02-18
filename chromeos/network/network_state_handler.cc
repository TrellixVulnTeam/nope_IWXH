// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/network_state_handler.h"

#include "base/bind.h"
#include "base/format_macros.h"
#include "base/guid.h"
#include "base/json/json_string_value_serializer.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chromeos/network/device_state.h"
#include "chromeos/network/network_event_log.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler_observer.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

bool ConnectionStateChanged(NetworkState* network,
                            const std::string& prev_connection_state,
                            bool prev_is_captive_portal) {
  return ((network->connection_state() != prev_connection_state) &&
          !((network->connection_state() == shill::kStateIdle) &&
            prev_connection_state.empty())) ||
         (network->is_captive_portal() != prev_is_captive_portal);
}

std::string GetManagedStateLogType(const ManagedState* state) {
  switch (state->managed_type()) {
    case ManagedState::MANAGED_TYPE_NETWORK:
      return "Network";
    case ManagedState::MANAGED_TYPE_DEVICE:
      return "Device";
  }
  NOTREACHED();
  return "";
}

std::string GetLogName(const ManagedState* state) {
  if (!state)
    return "None";
  return base::StringPrintf("%s (%s)", state->name().c_str(),
                            state->path().c_str());
}

std::string ValueAsString(const base::Value& value) {
  std::string vstr;
  base::JSONWriter::WriteWithOptions(
      &value, base::JSONWriter::OPTIONS_OMIT_BINARY_VALUES, &vstr);
  return vstr.empty() ? "''" : vstr;
}

}  // namespace

const char NetworkStateHandler::kDefaultCheckPortalList[] =
    "ethernet,wifi,cellular";

NetworkStateHandler::NetworkStateHandler() : network_list_sorted_(false) {
}

NetworkStateHandler::~NetworkStateHandler() {
  FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_, IsShuttingDown());
  STLDeleteContainerPointers(network_list_.begin(), network_list_.end());
  STLDeleteContainerPointers(device_list_.begin(), device_list_.end());
}

void NetworkStateHandler::InitShillPropertyHandler() {
  shill_property_handler_.reset(new internal::ShillPropertyHandler(this));
  shill_property_handler_->Init();
}

// static
NetworkStateHandler* NetworkStateHandler::InitializeForTest() {
  NetworkStateHandler* handler = new NetworkStateHandler();
  handler->InitShillPropertyHandler();
  return handler;
}

void NetworkStateHandler::AddObserver(
    NetworkStateHandlerObserver* observer,
    const tracked_objects::Location& from_here) {
  observers_.AddObserver(observer);
  device_event_log::AddEntry(from_here.file_name(), from_here.line_number(),
                             device_event_log::LOG_TYPE_NETWORK,
                             device_event_log::LOG_LEVEL_DEBUG,
                             "NetworkStateHandler::AddObserver");
}

void NetworkStateHandler::RemoveObserver(
    NetworkStateHandlerObserver* observer,
    const tracked_objects::Location& from_here) {
  observers_.RemoveObserver(observer);
  device_event_log::AddEntry(from_here.file_name(), from_here.line_number(),
                             device_event_log::LOG_TYPE_NETWORK,
                             device_event_log::LOG_LEVEL_DEBUG,
                             "NetworkStateHandler::RemoveObserver");
}

NetworkStateHandler::TechnologyState NetworkStateHandler::GetTechnologyState(
    const NetworkTypePattern& type) const {
  std::string technology = GetTechnologyForType(type);
  TechnologyState state;
  if (shill_property_handler_->IsTechnologyEnabled(technology))
    state = TECHNOLOGY_ENABLED;
  else if (shill_property_handler_->IsTechnologyEnabling(technology))
    state = TECHNOLOGY_ENABLING;
  else if (shill_property_handler_->IsTechnologyUninitialized(technology))
    state = TECHNOLOGY_UNINITIALIZED;
  else if (shill_property_handler_->IsTechnologyAvailable(technology))
    state = TECHNOLOGY_AVAILABLE;
  else
    state = TECHNOLOGY_UNAVAILABLE;
  VLOG(2) << "GetTechnologyState: " << type.ToDebugString() << " = " << state;
  return state;
}

void NetworkStateHandler::SetTechnologyEnabled(
    const NetworkTypePattern& type,
    bool enabled,
    const network_handler::ErrorCallback& error_callback) {
  ScopedVector<std::string> technologies = GetTechnologiesForType(type);
  for (ScopedVector<std::string>::iterator it = technologies.begin();
       it != technologies.end(); ++it) {
    std::string* technology = *it;
    DCHECK(technology);
    if (!shill_property_handler_->IsTechnologyAvailable(*technology))
      continue;
    NET_LOG_USER("SetTechnologyEnabled",
                 base::StringPrintf("%s:%d", technology->c_str(), enabled));
    shill_property_handler_->SetTechnologyEnabled(*technology, enabled,
                                                  error_callback);
  }
  // Signal Device/Technology state changed.
  NotifyDeviceListChanged();
}

const DeviceState* NetworkStateHandler::GetDeviceState(
    const std::string& device_path) const {
  const DeviceState* device = GetModifiableDeviceState(device_path);
  if (device && !device->update_received())
    return nullptr;
  return device;
}

const DeviceState* NetworkStateHandler::GetDeviceStateByType(
    const NetworkTypePattern& type) const {
  for (ManagedStateList::const_iterator iter = device_list_.begin();
       iter != device_list_.end(); ++iter) {
    ManagedState* device = *iter;
    if (!device->update_received())
      continue;
    if (device->Matches(type))
      return device->AsDeviceState();
  }
  return nullptr;
}

bool NetworkStateHandler::GetScanningByType(
    const NetworkTypePattern& type) const {
  for (ManagedStateList::const_iterator iter = device_list_.begin();
       iter != device_list_.end(); ++iter) {
    const DeviceState* device = (*iter)->AsDeviceState();
    DCHECK(device);
    if (!device->update_received())
      continue;
    if (device->Matches(type) && device->scanning())
      return true;
  }
  return false;
}

const NetworkState* NetworkStateHandler::GetNetworkState(
    const std::string& service_path) const {
  const NetworkState* network = GetModifiableNetworkState(service_path);
  if (network && !network->update_received())
    return nullptr;
  return network;
}

const NetworkState* NetworkStateHandler::DefaultNetwork() const {
  if (default_network_path_.empty())
    return nullptr;
  return GetNetworkState(default_network_path_);
}

const NetworkState* NetworkStateHandler::ConnectedNetworkByType(
    const NetworkTypePattern& type) const {
  // Active networks are always listed first by Shill so no need to sort.
  for (ManagedStateList::const_iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    const NetworkState* network = (*iter)->AsNetworkState();
    DCHECK(network);
    if (!network->update_received())
      continue;
    if (!network->IsConnectedState())
      break;  // Connected networks are listed first.
    if (network->Matches(type))
      return network;
  }
  return nullptr;
}

const NetworkState* NetworkStateHandler::ConnectingNetworkByType(
    const NetworkTypePattern& type) const {
  // Active networks are always listed first by Shill so no need to sort.
  for (ManagedStateList::const_iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    const NetworkState* network = (*iter)->AsNetworkState();
    DCHECK(network);
    if (!network->update_received() || network->IsConnectedState())
      continue;
    if (!network->IsConnectingState())
      break;  // Connected and connecting networks are listed first.
    if (network->Matches(type))
      return network;
  }
  return nullptr;
}

const NetworkState* NetworkStateHandler::FirstNetworkByType(
    const NetworkTypePattern& type) {
  if (!network_list_sorted_)
    SortNetworkList();  // Sort to ensure visible networks are listed first.
  for (ManagedStateList::const_iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    const NetworkState* network = (*iter)->AsNetworkState();
    DCHECK(network);
    if (!network->update_received())
      continue;
    if (!network->visible())
      break;
    if (network->Matches(type))
      return network;
  }
  return nullptr;
}

std::string NetworkStateHandler::FormattedHardwareAddressForType(
    const NetworkTypePattern& type) const {
  const DeviceState* device = nullptr;
  const NetworkState* network = ConnectedNetworkByType(type);
  if (network)
    device = GetDeviceState(network->device_path());
  else
    device = GetDeviceStateByType(type);
  if (!device)
    return std::string();
  return network_util::FormattedMacAddress(device->mac_address());
}

void NetworkStateHandler::GetVisibleNetworkListByType(
    const NetworkTypePattern& type,
    NetworkStateList* list) {
  GetNetworkListByType(type, false /* configured_only */,
                       true /* visible_only */, 0 /* no limit */, list);
}

void NetworkStateHandler::GetVisibleNetworkList(NetworkStateList* list) {
  GetVisibleNetworkListByType(NetworkTypePattern::Default(), list);
}

void NetworkStateHandler::GetNetworkListByType(const NetworkTypePattern& type,
                                               bool configured_only,
                                               bool visible_only,
                                               int limit,
                                               NetworkStateList* list) {
  DCHECK(list);
  list->clear();
  int count = 0;
  // Sort the network list if necessary.
  if (!network_list_sorted_)
    SortNetworkList();
  for (ManagedStateList::const_iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    const NetworkState* network = (*iter)->AsNetworkState();
    DCHECK(network);
    if (!network->update_received() || !network->Matches(type))
      continue;
    if (configured_only && !network->IsInProfile())
      continue;
    if (visible_only && !network->visible())
      continue;
    list->push_back(network);
    if (limit > 0 && ++count >= limit)
      break;
  }
}

const NetworkState* NetworkStateHandler::GetNetworkStateFromServicePath(
    const std::string& service_path,
    bool configured_only) const {
  ManagedState* managed =
      GetModifiableManagedState(&network_list_, service_path);
  if (!managed)
    return nullptr;
  const NetworkState* network = managed->AsNetworkState();
  DCHECK(network);
  if (!network->update_received() ||
      (configured_only && !network->IsInProfile())) {
    return nullptr;
  }
  return network;
}

const NetworkState* NetworkStateHandler::GetNetworkStateFromGuid(
    const std::string& guid) const {
  DCHECK(!guid.empty());
  for (ManagedStateList::const_iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    const NetworkState* network = (*iter)->AsNetworkState();
    if (network->guid() == guid)
      return network;
  }
  return nullptr;
}

void NetworkStateHandler::GetDeviceList(DeviceStateList* list) const {
  GetDeviceListByType(NetworkTypePattern::Default(), list);
}

void NetworkStateHandler::GetDeviceListByType(const NetworkTypePattern& type,
                                              DeviceStateList* list) const {
  DCHECK(list);
  list->clear();
  for (ManagedStateList::const_iterator iter = device_list_.begin();
       iter != device_list_.end(); ++iter) {
    const DeviceState* device = (*iter)->AsDeviceState();
    DCHECK(device);
    if (device->update_received() && device->Matches(type))
      list->push_back(device);
  }
}

void NetworkStateHandler::RequestScan() const {
  NET_LOG_USER("RequestScan", "");
  shill_property_handler_->RequestScan();
}

void NetworkStateHandler::RequestUpdateForNetwork(
    const std::string& service_path) {
  NetworkState* network = GetModifiableNetworkState(service_path);
  if (network)
    network->set_update_requested(true);
  NET_LOG_EVENT("RequestUpdate", service_path);
  shill_property_handler_->RequestProperties(ManagedState::MANAGED_TYPE_NETWORK,
                                             service_path);
}

void NetworkStateHandler::ClearLastErrorForNetwork(
    const std::string& service_path) {
  NetworkState* network = GetModifiableNetworkState(service_path);
  if (network)
    network->clear_last_error();
}

void NetworkStateHandler::SetCheckPortalList(
    const std::string& check_portal_list) {
  NET_LOG_EVENT("SetCheckPortalList", check_portal_list);
  shill_property_handler_->SetCheckPortalList(check_portal_list);
}

void NetworkStateHandler::SetWakeOnLanEnabled(bool enabled) {
  NET_LOG_EVENT("SetWakeOnLanEnabled",
                base::StringPrintf("%s", enabled ? "true" : "false"));
  shill_property_handler_->SetWakeOnLanEnabled(enabled);
}

const NetworkState* NetworkStateHandler::GetEAPForEthernet(
    const std::string& service_path) {
  const NetworkState* network = GetNetworkState(service_path);
  if (!network) {
    NET_LOG_ERROR("GetEAPForEthernet", "Unknown service path " + service_path);
    return nullptr;
  }
  if (network->type() != shill::kTypeEthernet) {
    NET_LOG_ERROR("GetEAPForEthernet", "Not of type Ethernet: " + service_path);
    return nullptr;
  }
  if (!network->IsConnectedState())
    return nullptr;

  // The same EAP service is shared for all ethernet services/devices.
  // However EAP is used/enabled per device and only if the connection was
  // successfully established.
  const DeviceState* device = GetDeviceState(network->device_path());
  if (!device) {
    NET_LOG(ERROR) << "GetEAPForEthernet: Unknown device "
                   << network->device_path()
                   << " for connected ethernet service: " << service_path;
    return nullptr;
  }
  if (!device->eap_authentication_completed())
    return nullptr;

  NetworkStateList list;
  GetNetworkListByType(NetworkTypePattern::Primitive(shill::kTypeEthernetEap),
                       true /* configured_only */, false /* visible_only */,
                       1 /* limit */, &list);
  if (list.empty()) {
    NET_LOG_ERROR("GetEAPForEthernet",
                  base::StringPrintf(
                      "Ethernet service %s connected using EAP, but no "
                      "EAP service found.",
                      service_path.c_str()));
    return nullptr;
  }
  return list.front();
}

//------------------------------------------------------------------------------
// ShillPropertyHandler::Delegate overrides

void NetworkStateHandler::UpdateManagedList(ManagedState::ManagedType type,
                                            const base::ListValue& entries) {
  ManagedStateList* managed_list = GetManagedList(type);
  NET_LOG_DEBUG("UpdateManagedList: " + ManagedState::TypeToString(type),
                base::StringPrintf("%" PRIuS, entries.GetSize()));
  // Create a map of existing entries. Assumes all entries in |managed_list|
  // are unique.
  typedef std::map<std::string, ManagedState*> ManagedMap;
  ManagedMap managed_map;
  for (ManagedStateList::iterator iter = managed_list->begin();
       iter != managed_list->end(); ++iter) {
    ManagedState* managed = *iter;
    DCHECK(!ContainsKey(managed_map, managed->path()));
    managed_map[managed->path()] = managed;
  }
  // Clear the list (pointers are temporarily owned by managed_map).
  managed_list->clear();
  // Updates managed_list and request updates for new entries.
  std::set<std::string> list_entries;
  for (base::ListValue::const_iterator iter = entries.begin();
       iter != entries.end(); ++iter) {
    std::string path;
    (*iter)->GetAsString(&path);
    if (path.empty() || path == shill::kFlimflamServicePath) {
      NET_LOG_ERROR(base::StringPrintf("Bad path in list:%d", type), path);
      continue;
    }
    ManagedMap::iterator found = managed_map.find(path);
    if (found == managed_map.end()) {
      if (list_entries.count(path) != 0) {
        NET_LOG_ERROR("Duplicate entry in list", path);
        continue;
      }
      ManagedState* managed = ManagedState::Create(type, path);
      managed_list->push_back(managed);
    } else {
      managed_list->push_back(found->second);
      managed_map.erase(found);
    }
    list_entries.insert(path);
  }
  // Delete any remaining entries in managed_map.
  STLDeleteContainerPairSecondPointers(managed_map.begin(), managed_map.end());
}

void NetworkStateHandler::ProfileListChanged() {
  NET_LOG_EVENT("ProfileListChanged", "Re-Requesting Network Properties");
  for (ManagedStateList::iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    NetworkState* network = (*iter)->AsNetworkState();
    DCHECK(network);
    shill_property_handler_->RequestProperties(
        ManagedState::MANAGED_TYPE_NETWORK, network->path());
  }
}

void NetworkStateHandler::UpdateManagedStateProperties(
    ManagedState::ManagedType type,
    const std::string& path,
    const base::DictionaryValue& properties) {
  ManagedStateList* managed_list = GetManagedList(type);
  ManagedState* managed = GetModifiableManagedState(managed_list, path);
  if (!managed) {
    // The network has been removed from the list of networks.
    NET_LOG_DEBUG("UpdateManagedStateProperties: Not found", path);
    return;
  }
  managed->set_update_received();

  std::string desc = GetManagedStateLogType(managed) + " Properties Received";
  NET_LOG_DEBUG(desc, GetLogName(managed));

  if (type == ManagedState::MANAGED_TYPE_NETWORK) {
    UpdateNetworkStateProperties(managed->AsNetworkState(), properties);
  } else {
    // Device
    for (base::DictionaryValue::Iterator iter(properties); !iter.IsAtEnd();
         iter.Advance()) {
      managed->PropertyChanged(iter.key(), iter.value());
    }
    managed->InitialPropertiesReceived(properties);
  }
  managed->set_update_requested(false);
}

void NetworkStateHandler::UpdateNetworkStateProperties(
    NetworkState* network,
    const base::DictionaryValue& properties) {
  DCHECK(network);
  bool network_property_updated = false;
  std::string prev_connection_state = network->connection_state();
  bool prev_is_captive_portal = network->is_captive_portal();
  for (base::DictionaryValue::Iterator iter(properties); !iter.IsAtEnd();
       iter.Advance()) {
    if (network->PropertyChanged(iter.key(), iter.value()))
      network_property_updated = true;
  }
  network_property_updated |= network->InitialPropertiesReceived(properties);
  UpdateGuid(network);
  network_list_sorted_ = false;

  // Notify observers of NetworkState changes.
  if (network_property_updated || network->update_requested()) {
    // Signal connection state changed after all properties have been updated.
    if (ConnectionStateChanged(network, prev_connection_state,
                               prev_is_captive_portal)) {
      OnNetworkConnectionStateChanged(network);
    }
    NET_LOG_EVENT("NetworkPropertiesUpdated", GetLogName(network));
    NotifyNetworkPropertiesUpdated(network);
  }
}

void NetworkStateHandler::UpdateNetworkServiceProperty(
    const std::string& service_path,
    const std::string& key,
    const base::Value& value) {
  SCOPED_NET_LOG_IF_SLOW();
  bool changed = false;
  NetworkState* network = GetModifiableNetworkState(service_path);
  if (!network)
    return;
  std::string prev_connection_state = network->connection_state();
  bool prev_is_captive_portal = network->is_captive_portal();
  std::string prev_profile_path = network->profile_path();
  changed |= network->PropertyChanged(key, value);
  if (!changed)
    return;

  if (key == shill::kStateProperty || key == shill::kVisibleProperty) {
    network_list_sorted_ = false;
    if (ConnectionStateChanged(network, prev_connection_state,
                               prev_is_captive_portal)) {
      OnNetworkConnectionStateChanged(network);
      // If the connection state changes, other properties such as IPConfig
      // may have changed, so request a full update.
      RequestUpdateForNetwork(service_path);
    }
  } else {
    std::string value_str;
    value.GetAsString(&value_str);
    // Some property changes are noisy and not interesting:
    // * Wifi SignalStrength
    // * WifiFrequencyList updates
    // * Device property changes to "/" (occurs before a service is removed)
    if (key != shill::kSignalStrengthProperty &&
        key != shill::kWifiFrequencyListProperty &&
        (key != shill::kDeviceProperty || value_str != "/")) {
      std::string log_event = "NetworkPropertyUpdated";
      // Trigger a default network update for interesting changes only.
      if (network->path() == default_network_path_) {
        NotifyDefaultNetworkChanged(network);
        log_event = "Default" + log_event;
      }
      // Log event.
      std::string detail = network->name() + "." + key;
      detail += " = " + ValueAsString(value);
      device_event_log::LogLevel log_level;
      if (key == shill::kErrorProperty || key == shill::kErrorDetailsProperty) {
        log_level = device_event_log::LOG_LEVEL_ERROR;
      } else {
        log_level = device_event_log::LOG_LEVEL_EVENT;
      }
      NET_LOG_LEVEL(log_level, log_event, detail);
    }
  }

  // All property updates signal 'NetworkPropertiesUpdated'.
  NotifyNetworkPropertiesUpdated(network);

  // If added to a Profile, request a full update so that a NetworkState
  // gets created.
  if (prev_profile_path.empty() && !network->profile_path().empty())
    RequestUpdateForNetwork(service_path);
}

void NetworkStateHandler::UpdateDeviceProperty(const std::string& device_path,
                                               const std::string& key,
                                               const base::Value& value) {
  SCOPED_NET_LOG_IF_SLOW();
  DeviceState* device = GetModifiableDeviceState(device_path);
  if (!device)
    return;
  if (!device->PropertyChanged(key, value))
    return;

  std::string detail = device->name() + "." + key;
  detail += " = " + ValueAsString(value);
  NET_LOG_EVENT("DevicePropertyUpdated", detail);

  NotifyDeviceListChanged();
  NotifyDevicePropertiesUpdated(device);

  if (key == shill::kScanningProperty && device->scanning() == false) {
    NotifyScanCompleted(device);
  }
  if (key == shill::kEapAuthenticationCompletedProperty) {
    // Notify a change for each Ethernet service using this device.
    NetworkStateList ethernet_services;
    GetNetworkListByType(NetworkTypePattern::Ethernet(),
                         false /* configured_only */, false /* visible_only */,
                         0 /* no limit */, &ethernet_services);
    for (NetworkStateList::const_iterator it = ethernet_services.begin();
         it != ethernet_services.end(); ++it) {
      const NetworkState* ethernet_service = *it;
      if (ethernet_service->update_received() ||
          ethernet_service->device_path() != device->path()) {
        continue;
      }
      RequestUpdateForNetwork(ethernet_service->path());
    }
  }
}

void NetworkStateHandler::UpdateIPConfigProperties(
    ManagedState::ManagedType type,
    const std::string& path,
    const std::string& ip_config_path,
    const base::DictionaryValue& properties) {
  if (type == ManagedState::MANAGED_TYPE_NETWORK) {
    NetworkState* network = GetModifiableNetworkState(path);
    if (!network)
      return;
    network->IPConfigPropertiesChanged(properties);
    NotifyNetworkPropertiesUpdated(network);
    if (network->path() == default_network_path_)
      NotifyDefaultNetworkChanged(network);
  } else if (type == ManagedState::MANAGED_TYPE_DEVICE) {
    DeviceState* device = GetModifiableDeviceState(path);
    if (!device)
      return;
    device->IPConfigPropertiesChanged(ip_config_path, properties);
    NotifyDevicePropertiesUpdated(device);
    if (!default_network_path_.empty()) {
      const NetworkState* default_network =
          GetNetworkState(default_network_path_);
      if (default_network && default_network->device_path() == path)
        NotifyDefaultNetworkChanged(default_network);
    }
  }
}

void NetworkStateHandler::CheckPortalListChanged(
    const std::string& check_portal_list) {
  check_portal_list_ = check_portal_list;
}

void NetworkStateHandler::TechnologyListChanged() {
  // Eventually we would like to replace Technology state with Device state.
  // For now, treat technology state changes as device list changes.
  NotifyDeviceListChanged();
}

void NetworkStateHandler::ManagedStateListChanged(
    ManagedState::ManagedType type) {
  SCOPED_NET_LOG_IF_SLOW();
  if (type == ManagedState::MANAGED_TYPE_NETWORK) {
    SortNetworkList();
    UpdateNetworkStats();
    // Notify observers that the list of networks has changed.
    NET_LOG_EVENT("NOTIFY:NetworkListChanged",
                  base::StringPrintf("Size:%" PRIuS, network_list_.size()));
    FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                      NetworkListChanged());
  } else if (type == ManagedState::MANAGED_TYPE_DEVICE) {
    std::string devices;
    for (ManagedStateList::const_iterator iter = device_list_.begin();
         iter != device_list_.end(); ++iter) {
      if (iter != device_list_.begin())
        devices += ", ";
      devices += (*iter)->name();
    }
    NET_LOG_EVENT("DeviceList", devices);
    NotifyDeviceListChanged();
  } else {
    NOTREACHED();
  }
}

void NetworkStateHandler::SortNetworkList() {
  // Note: usually active networks will precede inactive networks, however
  // this may briefly be untrue during state transitions (e.g. a network may
  // transition to idle before the list is updated).
  ManagedStateList active, non_wifi_visible, wifi_visible, hidden, new_networks;
  for (ManagedStateList::iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    NetworkState* network = (*iter)->AsNetworkState();
    if (!network->update_received()) {
      new_networks.push_back(network);
      continue;
    }
    if (network->IsConnectedState() || network->IsConnectingState()) {
      active.push_back(network);
      continue;
    }
    if (network->visible()) {
      if (NetworkTypePattern::WiFi().MatchesType(network->type()))
        wifi_visible.push_back(network);
      else
        non_wifi_visible.push_back(network);
    } else {
      hidden.push_back(network);
    }
  }
  network_list_.clear();
  network_list_.insert(network_list_.end(), active.begin(), active.end());
  network_list_.insert(network_list_.end(), non_wifi_visible.begin(),
                       non_wifi_visible.end());
  network_list_.insert(network_list_.end(), wifi_visible.begin(),
                       wifi_visible.end());
  network_list_.insert(network_list_.end(), hidden.begin(), hidden.end());
  network_list_.insert(network_list_.end(), new_networks.begin(),
                       new_networks.end());
  network_list_sorted_ = true;
}

void NetworkStateHandler::UpdateNetworkStats() {
  size_t shared = 0, unshared = 0, visible = 0;
  for (ManagedStateList::iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    NetworkState* network = (*iter)->AsNetworkState();
    if (network->visible())
      ++visible;
    if (network->IsInProfile()) {
      if (network->IsPrivate())
        ++unshared;
      else
        ++shared;
    }
  }
  UMA_HISTOGRAM_COUNTS_100("Networks.Visible", visible);
  UMA_HISTOGRAM_COUNTS_100("Networks.RememberedShared", shared);
  UMA_HISTOGRAM_COUNTS_100("Networks.RememberedUnshared", unshared);
}

void NetworkStateHandler::DefaultNetworkServiceChanged(
    const std::string& service_path) {
  // Shill uses '/' for empty service path values; check explicitly for that.
  const char* kEmptyServicePath = "/";
  std::string new_service_path =
      (service_path != kEmptyServicePath) ? service_path : "";
  if (new_service_path == default_network_path_)
    return;

  default_network_path_ = service_path;
  NET_LOG_EVENT("DefaultNetworkServiceChanged:", default_network_path_);
  const NetworkState* network = nullptr;
  if (!default_network_path_.empty()) {
    network = GetNetworkState(default_network_path_);
    if (!network) {
      // If NetworkState is not available yet, do not notify observers here,
      // they will be notified when the state is received.
      NET_LOG_DEBUG("Default NetworkState not available",
                    default_network_path_);
      return;
    }
  }
  if (network && !network->IsConnectedState()) {
    NET_LOG_ERROR(
        "DefaultNetwork is not connected: " + network->connection_state(),
        network->path());
  }
  NotifyDefaultNetworkChanged(network);
}

//------------------------------------------------------------------------------
// Private methods

void NetworkStateHandler::UpdateGuid(NetworkState* network) {
  std::string specifier = network->GetSpecifier();
  DCHECK(!specifier.empty());
  if (!network->guid().empty()) {
    // If the network is saved in a profile, remove the entry from the map.
    // Otherwise ensure that the entry matches the specified GUID. (e.g. in
    // case a visible network with a specified guid gets configured with a
    // new guid).
    if (network->IsInProfile())
      specifier_guid_map_.erase(specifier);
    else
      specifier_guid_map_[specifier] = network->guid();
    return;
  }
  // Ensure that the NetworkState has a valid GUID.
  std::string guid;
  SpecifierGuidMap::iterator iter = specifier_guid_map_.find(specifier);
  if (iter != specifier_guid_map_.end()) {
    guid = iter->second;
  } else {
    guid = base::GenerateGUID();
    specifier_guid_map_[specifier] = guid;
  }
  network->SetGuid(guid);
}

void NetworkStateHandler::NotifyDeviceListChanged() {
  SCOPED_NET_LOG_IF_SLOW();
  NET_LOG_DEBUG("NOTIFY:DeviceListChanged",
                base::StringPrintf("Size:%" PRIuS, device_list_.size()));
  FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                    DeviceListChanged());
}

DeviceState* NetworkStateHandler::GetModifiableDeviceState(
    const std::string& device_path) const {
  ManagedState* managed = GetModifiableManagedState(&device_list_, device_path);
  if (!managed)
    return nullptr;
  return managed->AsDeviceState();
}

NetworkState* NetworkStateHandler::GetModifiableNetworkState(
    const std::string& service_path) const {
  ManagedState* managed =
      GetModifiableManagedState(&network_list_, service_path);
  if (!managed)
    return nullptr;
  return managed->AsNetworkState();
}

ManagedState* NetworkStateHandler::GetModifiableManagedState(
    const ManagedStateList* managed_list,
    const std::string& path) const {
  for (ManagedStateList::const_iterator iter = managed_list->begin();
       iter != managed_list->end(); ++iter) {
    ManagedState* managed = *iter;
    if (managed->path() == path)
      return managed;
  }
  return nullptr;
}

NetworkStateHandler::ManagedStateList* NetworkStateHandler::GetManagedList(
    ManagedState::ManagedType type) {
  switch (type) {
    case ManagedState::MANAGED_TYPE_NETWORK:
      return &network_list_;
    case ManagedState::MANAGED_TYPE_DEVICE:
      return &device_list_;
  }
  NOTREACHED();
  return nullptr;
}

void NetworkStateHandler::OnNetworkConnectionStateChanged(
    NetworkState* network) {
  SCOPED_NET_LOG_IF_SLOW();
  DCHECK(network);
  std::string event = "NetworkConnectionStateChanged";
  if (network->path() == default_network_path_) {
    event = "Default" + event;
    if (!network->IsConnectedState()) {
      NET_LOG_EVENT(
          "DefaultNetwork is not connected: " + network->connection_state(),
          network->path());
      default_network_path_.clear();
      SortNetworkList();
      NotifyDefaultNetworkChanged(nullptr);
    }
  }
  NET_LOG_EVENT("NOTIFY:" + event + ": " + network->connection_state(),
                GetLogName(network));
  FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                    NetworkConnectionStateChanged(network));
  if (network->path() == default_network_path_)
    NotifyDefaultNetworkChanged(network);
}

void NetworkStateHandler::NotifyDefaultNetworkChanged(
    const NetworkState* default_network) {
  SCOPED_NET_LOG_IF_SLOW();
  NET_LOG_EVENT("NOTIFY:DefaultNetworkChanged", GetLogName(default_network));
  FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                    DefaultNetworkChanged(default_network));
}

void NetworkStateHandler::NotifyNetworkPropertiesUpdated(
    const NetworkState* network) {
  SCOPED_NET_LOG_IF_SLOW();
  NET_LOG_DEBUG("NOTIFY:NetworkPropertiesUpdated", GetLogName(network));
  FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                    NetworkPropertiesUpdated(network));
}

void NetworkStateHandler::NotifyDevicePropertiesUpdated(
    const DeviceState* device) {
  SCOPED_NET_LOG_IF_SLOW();
  NET_LOG_DEBUG("NOTIFY:DevicePropertiesUpdated", GetLogName(device));
  FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                    DevicePropertiesUpdated(device));
}

void NetworkStateHandler::NotifyScanCompleted(const DeviceState* device) {
  SCOPED_NET_LOG_IF_SLOW();
  NET_LOG_DEBUG("NOTIFY:ScanCompleted", GetLogName(device));
  FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                    ScanCompleted(device));
}

std::string NetworkStateHandler::GetTechnologyForType(
    const NetworkTypePattern& type) const {
  if (type.MatchesType(shill::kTypeEthernet))
    return shill::kTypeEthernet;

  if (type.MatchesType(shill::kTypeWifi))
    return shill::kTypeWifi;

  if (type.Equals(NetworkTypePattern::Wimax()))
    return shill::kTypeWimax;

  // Prefer Wimax over Cellular only if it's available.
  if (type.MatchesType(shill::kTypeWimax) &&
      shill_property_handler_->IsTechnologyAvailable(shill::kTypeWimax)) {
    return shill::kTypeWimax;
  }

  if (type.MatchesType(shill::kTypeCellular))
    return shill::kTypeCellular;

  NOTREACHED();
  return std::string();
}

ScopedVector<std::string> NetworkStateHandler::GetTechnologiesForType(
    const NetworkTypePattern& type) const {
  ScopedVector<std::string> technologies;
  if (type.MatchesType(shill::kTypeEthernet))
    technologies.push_back(new std::string(shill::kTypeEthernet));
  if (type.MatchesType(shill::kTypeWifi))
    technologies.push_back(new std::string(shill::kTypeWifi));
  if (type.MatchesType(shill::kTypeWimax))
    technologies.push_back(new std::string(shill::kTypeWimax));
  if (type.MatchesType(shill::kTypeCellular))
    technologies.push_back(new std::string(shill::kTypeCellular));
  if (type.MatchesType(shill::kTypeBluetooth))
    technologies.push_back(new std::string(shill::kTypeBluetooth));
  if (type.MatchesType(shill::kTypeVPN))
    technologies.push_back(new std::string(shill::kTypeVPN));

  CHECK_GT(technologies.size(), 0ul);
  return technologies.Pass();
}

}  // namespace chromeos
