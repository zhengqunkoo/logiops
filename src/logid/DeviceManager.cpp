/*
 * Copyright 2019-2020 PixlOne
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <thread>
#include <sstream>
#include <algorithm>

#include "DeviceManager.h"
#include "Receiver.h"
#include "util/log.h"
#include "backend/hidpp10/Error.h"
#include "backend/Error.h"

using namespace logid;
using namespace logid::backend;

DeviceManager::DeviceManager() : _ipc_interface ()
{
    ipc::registerAuto(&_ipc_interface);
}

int DeviceManager::newDeviceId(bool receiver)
{
    int id = 0;
    /* Lock device id mutex */ {
        std::unique_lock<std::mutex> lock(_device_id_lock);
        if(!_device_ids.empty()) {
            if(*_device_ids.begin() > 0) {
                id = 0;
            } else {
                auto it = std::adjacent_find(_device_ids.begin(),
                        _device_ids.end(),[](int a, int b) {
                                                 return a + 1 != b;
                                             });
                if (it == _device_ids.end())
                    id = *_device_ids.rbegin() + 1;
                else
                    id = *it + 1;
            }
        }
        _device_ids.insert(id);
    }

    if(receiver)
        _ipc_interface.addReceiver(std::to_string(id));
    else
        _ipc_interface.addDevice(std::to_string(id));

    return id;
}

void DeviceManager::dropDeviceId(int id, bool receiver)
{
    /* Lock device id mutex */ {
        std::unique_lock<std::mutex> lock(_device_id_lock);
        _device_ids.erase(id);
    }

    if(receiver)
        _ipc_interface.removeReceiver(std::to_string(id));
    else
        _ipc_interface.removeDevice(std::to_string(id));
}

void DeviceManager::addDevice(std::string path)
{
    // Check if device already exists
    if(_devices.find(path) != _devices.end() ||
        _receivers.find(path) != _receivers.end())
        return;

    bool defaultExists = true;
    bool isReceiver = false;

    // Check if device is ignored before continuing
    {
        raw::RawDevice raw_dev(path);
        if(global_config->isIgnored(raw_dev.productId())) {
            logPrintf(DEBUG, "%s: Device 0x%04x ignored.",
                  path.c_str(), raw_dev.productId());
            return;
        }
    }

    try {
        hidpp::Device device(path, hidpp::DefaultDevice);
        isReceiver = device.version() == std::make_tuple(1, 0);
    } catch(hidpp10::Error &e) {
        if(e.code() != hidpp10::Error::UnknownDevice)
            throw;
    } catch(hidpp::Device::InvalidDevice &e) { // Ignore
        defaultExists = false;
    } catch(std::system_error &e) {
        logPrintf(WARN, "I/O error on %s: %s, skipping device.",
                path.c_str(), e.what());
        return;
    } catch (TimeoutError &e) {
        logPrintf(WARN, "Device %s timed out.", path.c_str());
        defaultExists = false;
    }

    if(isReceiver) {
        logPrintf(INFO, "Detected receiver at %s", path.c_str());
        auto receiver = std::make_shared<Receiver>(path, this);
        receiver->run();
        _receivers.emplace(path, receiver);
    } else {
         /* TODO: Can non-receivers only contain 1 device?
         * If the device exists, it is guaranteed to be an HID++ 2.0 device */
        if(defaultExists) {
            auto device = std::make_shared<Device>(path, hidpp::DefaultDevice,
                    this);
            _devices.emplace(path,  device);
        } else {
            try {
                auto device = std::make_shared<Device>(path,
                        hidpp::CordedDevice, this);
                _devices.emplace(path, device);
            } catch(hidpp10::Error &e) {
                if(e.code() != hidpp10::Error::UnknownDevice)
                    throw;
                else
                    logPrintf(WARN,
                            "HID++ 1.0 error while trying to initialize %s:"
                            "%s", path.c_str(), e.what());
            } catch(hidpp::Device::InvalidDevice &e) { // Ignore
            } catch(std::system_error &e) {
                // This error should have been thrown previously
                logPrintf(WARN, "I/O error on %s: %s", path.c_str(),
                        e.what());
            }
        }
    }
}

void DeviceManager::removeDevice(std::string path)
{
    auto receiver = _receivers.find(path);

    if(receiver != _receivers.end()) {
        _receivers.erase(receiver);
        logPrintf(INFO, "Receiver on %s disconnected", path.c_str());
    } else {
        auto device = _devices.find(path);
        if(device != _devices.end()) {
            _devices.erase(device);
            logPrintf(INFO, "Device on %s disconnected", path.c_str());
        }
    }
}

DeviceManager::IPC::IPC() : ipc::IPCInterface("", "DeviceManager")
{
    std::vector<ipc::IPCVariant> dev_type(0);
    ipc::IPCProperty devices = {
            ipc::IPCVariant(dev_type, ipc::IPCVariant::TypeInfo("as")),
            ipc::IPCVariant::TypeInfo("as"),
            true,
            false
    };

    ipc::IPCArgsInfo device_signal_args = {{"device",
                                            ipc::IPCVariant::TypeInfo('s')}};
    ipc::IPCArgsInfo receiver_signal_args = {{"receiver",
                                              ipc::IPCVariant::TypeInfo('s')}};

    _signals.emplace("deviceAdded", device_signal_args);
    _signals.emplace("deviceRemoved", device_signal_args);
    _signals.emplace("receiverAdded", device_signal_args);
    _signals.emplace("receiverRemoved", device_signal_args);

    _properties.emplace("devices", devices);
    _properties.emplace("receivers", devices);
}

void DeviceManager::IPC::addDevice(const std::string& name)
{
    auto dev_property = _properties["devices"];
    auto devices = (std::vector<ipc::IPCVariant>&)(dev_property.property);
    devices.emplace_back(name);
    dev_property.property = devices;
    _properties["devices"] = dev_property;

    emitSignal("deviceAdded", {ipc::IPCVariant(name)});
}

void DeviceManager::IPC::removeDevice(const std::string& name)
{
    auto dev_property = _properties["devices"];
    auto devices = (std::vector<ipc::IPCVariant>&)(dev_property.property);
    for(auto it = devices.begin(); it != devices.end(); it++) {
        if(*it == name) {
            devices.erase(it);
            break;
        }
    }
    dev_property.property = devices;
    _properties["devices"] = dev_property;

    emitSignal("deviceRemoved", {ipc::IPCVariant(name)});
}

void DeviceManager::IPC::addReceiver(const std::string &name)
{
    auto recv_property = _properties["receivers"];
    auto receivers = (std::vector<ipc::IPCVariant>&)(recv_property.property);
    receivers.emplace_back(name);
    recv_property.property = receivers;
    _properties["receivers"] = recv_property;

    emitSignal("receiverAdded", {ipc::IPCVariant(name)});
}

void DeviceManager::IPC::removeReceiver(const std::string &name)
{
    auto recv_property = _properties["receivers"];
    auto receivers = (std::vector<ipc::IPCVariant>&)(recv_property.property);
    for(auto it = receivers.begin(); it != receivers.end(); it++) {
        if(*it == name) {
            receivers.erase(it);
            break;
        }
    }
    recv_property.property = receivers;
    _properties["receivers"] = recv_property;

    emitSignal("receiverRemoved", {ipc::IPCVariant(name)});
}

DeviceManager::IPC& DeviceManager::ipc()
{
    return _ipc_interface;
}
