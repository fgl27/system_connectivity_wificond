/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "wificond/ap_interface_impl.h"

#include <android-base/logging.h>

#include "wificond/net/netlink_utils.h"

#include "wificond/ap_interface_binder.h"
#include "wificond/logging_utils.h"

using android::net::wifi::IApInterface;
using android::wifi_system::InterfaceTool;
using com::android::server::wifi::wificond::NativeWifiClient;
using std::array;
using std::endl;
using std::string;
using std::unique_ptr;
using std::vector;

using namespace std::placeholders;

namespace android {
namespace wificond {

ApInterfaceImpl::ApInterfaceImpl(const string& interface_name,
                                 uint32_t interface_index,
                                 NetlinkUtils* netlink_utils,
                                 InterfaceTool* if_tool)
    : interface_name_(interface_name),
      interface_index_(interface_index),
      netlink_utils_(netlink_utils),
      if_tool_(if_tool),
      binder_(new ApInterfaceBinder(this)) {
  // This log keeps compiler happy.
  LOG(DEBUG) << "Created ap interface " << interface_name_
             << " with index " << interface_index_;

  netlink_utils_->SubscribeStationEvent(
      interface_index_,
      std::bind(&ApInterfaceImpl::OnStationEvent,
                this,
                _1, _2));
  netlink_utils_->SubscribeChannelSwitchEvent(
      interface_index_,
      std::bind(&ApInterfaceImpl::OnChannelSwitchEvent, this, _1, _2));

}

ApInterfaceImpl::~ApInterfaceImpl() {
  binder_->NotifyImplDead();
  if_tool_->SetUpState(interface_name_.c_str(), false);
  netlink_utils_->UnsubscribeStationEvent(interface_index_);
  netlink_utils_->UnsubscribeChannelSwitchEvent(interface_index_);
}

sp<IApInterface> ApInterfaceImpl::GetBinder() const {
  return binder_;
}

void ApInterfaceImpl::Dump(std::stringstream* ss) const {
  *ss << "------- Dump of AP interface with index: "
      << interface_index_ << " and name: " << interface_name_
      << "-------" << endl;
  *ss << "Number of connected access point clients: "
      <<  connected_clients_.size() << endl;
  *ss << "------- Dump End -------" << endl;
}

void ApInterfaceImpl::OnStationEvent(
    StationEvent event,
    const array<uint8_t, ETH_ALEN>& mac_address) {
  if (event == NEW_STATION) {
    LOG(INFO) << "New station "
              << LoggingUtils::GetMacString(mac_address)
              << " connected to hotspot"
              << " using interface "
              << interface_name_;

    vector<uint8_t> mac_return_address = vector<uint8_t>(mac_address.begin(), mac_address.end());
    const auto iterator = std::find_if(connected_clients_.begin(), connected_clients_.end(),
        [&] (NativeWifiClient const& p) {
            return p.macAddress == mac_return_address;
    });

    if (iterator == connected_clients_.end()) {
      NativeWifiClient station;
      station.macAddress = mac_return_address;
      connected_clients_.push_back(station);

      LOG(INFO) << "Sending notifications for station event";
      binder_->NotifyConnectedClientsChanged(connected_clients_);
    }
  } else if (event == DEL_STATION) {
    LOG(INFO) << "Station "
              << LoggingUtils::GetMacString(mac_address)
              << " disassociated from hotspot";
    if (connected_clients_.size() <= 0) {
      LOG(ERROR) << "Received DEL_STATION event when station counter is: "
                 << connected_clients_.size();
      return;
    } else {
      vector<uint8_t> mac_return_address = vector<uint8_t>(mac_address.begin(), mac_address.end());
      const auto iterator = std::find_if(connected_clients_.begin(), connected_clients_.end(),
              [&] (NativeWifiClient const& p) {
                  return p.macAddress == mac_return_address;
          });

      if (iterator != connected_clients_.end()) {
        connected_clients_.erase(iterator);

        LOG(DEBUG) << "Sending notifications for station event";
        binder_->NotifyConnectedClientsChanged(connected_clients_);
      }
    }
  }
}


void ApInterfaceImpl::OnChannelSwitchEvent(uint32_t frequency,
                                           ChannelBandwidth bandwidth) {
  LOG(INFO) << "New channel on frequency: " << frequency
            << " with bandwidth: " << LoggingUtils::GetBandwidthString(bandwidth);
  binder_->NotifySoftApChannelSwitched(frequency, bandwidth);
}

std::vector<NativeWifiClient> ApInterfaceImpl::GetConnectedClients() const {
  return connected_clients_;
}

}  // namespace wificond
}  // namespace android
