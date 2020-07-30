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

#include "Receiver.h"
#include "DeviceManager.h"
#include "util/log.h"
#include "backend/hidpp10/Error.h"
#include "backend/hidpp20/Error.h"
#include "backend/Error.h"

using namespace logid;
using namespace logid::backend;

Receiver::Receiver(const std::string& path, DeviceManager* manager) :
    dj::ReceiverMonitor(path), _path (path), _manager (manager),
    _device_id (_manager->newDeviceId(true)),
    _ipc_interface (this)
{
    ipc::registerAuto(&_ipc_interface);
}

Receiver::~Receiver()
{
    _manager->dropDeviceId(_device_id, true);
}

void Receiver::addDevice(hidpp::DeviceConnectionEvent event)
{
    std::unique_lock<std::mutex> lock(_devices_change);
    try {
        // Check if device is ignored before continuing
        if(global_config->isIgnored(event.pid)) {
            logPrintf(DEBUG, "%s:%d: Device 0x%04x ignored.",
                      _path.c_str(), event.index, event.pid);
            return;
        }

        auto dev = _devices.find(event.index);
        if(dev != _devices.end()) {
            if(event.linkEstablished)
                dev->second->wakeup();
            else
                dev->second->sleep();
            return;
        }

        if(!event.linkEstablished)
            return;

        hidpp::Device hidpp_device(receiver(), event);

        auto version = hidpp_device.version();

        if(std::get<0>(version) < 2) {
            logPrintf(INFO, "Unsupported HID++ 1.0 device on %s:%d connected.",
                    _path.c_str(), event.index);
            return;
        }

        std::shared_ptr<Device> device = std::make_shared<Device>(this,
                event.index, _manager);

        _devices.emplace(event.index, device);
        _ipc_interface.devicePaired(std::to_string(device->deviceId()));
    } catch(hidpp10::Error &e) {
        logPrintf(ERROR,
                       "Caught HID++ 1.0 error while trying to initialize "
                       "%s:%d: %s", _path.c_str(), event.index, e.what());
    } catch(hidpp20::Error &e) {
        logPrintf(ERROR, "Caught HID++ 2.0 error while trying to initialize "
                          "%s:%d: %s", _path.c_str(), event.index, e.what());
    } catch(TimeoutError &e) {
        if(!event.fromTimeoutCheck)
            logPrintf(DEBUG, "%s:%d timed out, waiting for input from device to"
                             " initialize.", _path.c_str(), event.index);
        waitForDevice(event.index);
    }
}

void Receiver::removeDevice(hidpp::DeviceIndex index)
{
    std::unique_lock<std::mutex> lock(_devices_change);
    auto it = _devices.find(index);
    if(it != _devices.end()) {
        _ipc_interface.deviceUnpaired(std::to_string(it->second->deviceId()));
        _devices.erase(it);
    }
}

void Receiver::lockingChange(backend::dj::Receiver::PairingLockEvent event)
{
    if(!event.lockingOpen) {
        if(event.isError) {
            std::string error;
            switch(event.error) {
            case dj::Receiver::PairingError::Timeout:
                error = "Timeout";
                break;
            case dj::Receiver::PairingError::UnsupportedDevice:
                error = "Unsupported device";
                break;
            case dj::Receiver::PairingError::TooManyDevices:
                error = "Too many devices";
                break;
            case dj::Receiver::PairingError::ConnectionTimeout:
                error = "Connection sequence timeout";
                break;
            case dj::Receiver::PairingError::Reserved:
                error = "Reserved";
                break;
            }
            _ipc_interface.pairingLockStatus(event.lockingOpen,
                    event.isError, error);
            logPrintf(WARN, "Pairing failed on receiver %s: %s",
                      _path.c_str(), error.c_str());
        } else {
            _ipc_interface.pairingLockStatus(event.lockingOpen,
                    event.isError, "");
            logPrintf(INFO, "Pairing lock closed on %s", _path.c_str());
        }
    } else {
        _ipc_interface.pairingLockStatus(event.lockingOpen,
                event.isError, "");
        logPrintf(INFO, "Pairing lock opened on %s", _path.c_str());
    }
}

const std::string& Receiver::path() const
{
    return _path;
}

int Receiver::deviceId() const
{
    return _device_id;
}

std::shared_ptr<dj::Receiver> Receiver::rawReceiver()
{
    return receiver();
}

Receiver::IPC::IPC(Receiver *receiver) : IPCInterface("receiver/" +
    std::to_string(receiver->deviceId()), "Receiver"),
    _receiver (receiver)
{
    ipc::IPCArgsInfo device_signal_args = {{"device",
                                            ipc::IPCVariant::TypeInfo('s')}};
    _signals.emplace("devicePaired", device_signal_args);
    _signals.emplace("deviceUnpaired", device_signal_args);

    ipc::IPCArgsInfo pair_signal_args = {{"pairing",
                                            ipc::IPCVariant::TypeInfo('b')},
                                         { "isError",
                                           ipc::IPCVariant::TypeInfo('b')},
                                         { "error",
                                           ipc::IPCVariant::TypeInfo('s')}};
    _signals.emplace("pairingStatus", pair_signal_args);

    std::vector<ipc::IPCVariant> null_array;
    ipc::IPCProperty devices = {
            ipc::IPCVariant(null_array, ipc::IPCVariant::TypeInfo("as")),
            ipc::IPCVariant::TypeInfo("as"),
            true,
            false
    };
    _properties.emplace("devices", devices);

    auto pair_function = std::make_shared<ipc::IPCFunction>();
    pair_function->function = [r=this->_receiver]
            (const ipc::IPCFunctionArgs& args)->ipc::IPCFunctionArgs {
        logPrintf(INFO, "Starting pair on %s, timing out in %d seconds.",
                r->path().c_str(), (uint8_t)args[0]);
        r->pair(args[0]);
        return {};
    };
    pair_function->args = {{"timeout",ipc::IPCVariant::TypeInfo(
            ipc::IPCVariant::TypeInfo::Byte)}};

    auto unpair_function = std::make_shared<ipc::IPCFunction>();
    unpair_function->function = [r=this->_receiver]
            (const ipc::IPCFunctionArgs& args)->ipc::IPCFunctionArgs {
        uint8_t raw_index = args[0];
        if(raw_index < hidpp::WirelessDevice1 ||
            raw_index > hidpp::WirelessDevice6)
            throw std::invalid_argument("index");
        hidpp::DeviceIndex index = static_cast<hidpp::DeviceIndex>(raw_index);
        r->unpair(index);
        return {};
    };
    unpair_function->args = {{"index",ipc::IPCVariant::TypeInfo(
            ipc::IPCVariant::TypeInfo::Byte)}};

    auto stoppair_function = std::make_shared<ipc::IPCFunction>();
    stoppair_function->function = [r=this->_receiver]
            (const ipc::IPCFunctionArgs& args)->ipc::IPCFunctionArgs {
        logPrintf(INFO, "Cancelling pair on %s.", r->path().c_str());
        r->stopPairing();
        return {};
    };

    _functions.emplace("pair", pair_function);
    _functions.emplace("stopPairing", stoppair_function);
    _functions.emplace("unpair", unpair_function);
}

void Receiver::IPC::devicePaired(const std::string& name)
{
    auto dev_property = _properties["devices"];
    auto devices = (std::vector<ipc::IPCVariant>&)(dev_property.property);
    devices.emplace_back(name);
    dev_property.property = devices;
    _properties["devices"] = dev_property;

    emitSignal("devicePaired", {ipc::IPCVariant(name)});
}

void Receiver::IPC::deviceUnpaired(const std::string& name)
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

    emitSignal("deviceUnpaired", {ipc::IPCVariant(name)});
}

void Receiver::IPC::pairingLockStatus(bool locked, bool isError,
        const std::string& error)
{
    emitSignal("pairingStatus", {ipc::IPCVariant(locked),
                                 ipc::IPCVariant(isError),
                                 ipc::IPCVariant(error)});
}