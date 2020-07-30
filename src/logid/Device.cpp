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

#include "util/log.h"
#include "Device.h"
#include "Receiver.h"
#include "DeviceManager.h"
#include "features/DPI.h"
#include "features/SmartShift.h"
#include "features/RemapButton.h"
#include "backend/hidpp20/features/Reset.h"
#include "features/HiresScroll.h"
#include "features/DeviceStatus.h"
#include "util/task.h"

using namespace logid;
using namespace logid::backend;

Device::Device(std::string path, backend::hidpp::DeviceIndex index,
    DeviceManager* manager) : _hidpp20 (path, index), _path (std::move(path)),
    _index (index), _config (global_config, this), _receiver (nullptr),
    _manager (manager), _device_id (_manager->newDeviceId()),
    _ipc_interface (this)
{
    _init();
}

Device::Device(const std::shared_ptr<backend::raw::RawDevice>& raw_device,
        hidpp::DeviceIndex index, DeviceManager* manager) :
        _hidpp20(raw_device, index), _path (raw_device->hidrawPath()),
        _index (index), _config (global_config, this), _receiver (nullptr),
        _manager (manager), _device_id (_manager->newDeviceId()),
        _ipc_interface (this)
{
    _init();
}

Device::Device(Receiver* receiver, hidpp::DeviceIndex index,
    DeviceManager* manager) : _hidpp20 (receiver->rawReceiver(), index),
    _path (receiver->path()), _index (index), _config (global_config, this),
    _receiver (receiver), _manager (manager),
    _device_id (_manager->newDeviceId()), _ipc_interface (this)
{
    _init();
}

Device::~Device()
{
    _manager->dropDeviceId(_device_id);
}

void Device::_init()
{
    ipc::registerAuto(&_ipc_interface);
    logPrintf(INFO, "Device found: %s on %s:%d", name().c_str(),
            hidpp20().devicePath().c_str(), _index);

    _addFeature<features::DPI>("dpi");
    _addFeature<features::SmartShift>("smartshift");
    _addFeature<features::HiresScroll>("hiresscroll");
    _addFeature<features::RemapButton>("remapbutton");
    _addFeature<features::DeviceStatus>("devicestatus");
    _ipc_interface.initFeatures();

    _makeResetMechanism();
    reset();

    for(auto& feature: _features) {
        feature.second->configure();
        feature.second->listen();
    }

    _hidpp20.listen();
}

const std::string& Device::name() const
{
    return _hidpp20.name();
}

uint16_t Device::pid() const
{
    return _hidpp20.pid();
}

int Device::deviceId() const
{
    return _device_id;
}

void Device::sleep()
{
    logPrintf(INFO, "%s:%d fell asleep.", _path.c_str(), _index);
    _ipc_interface.sleep();
}

void Device::wakeup()
{
    logPrintf(INFO, "%s:%d woke up.", _path.c_str(), _index);
    _ipc_interface.wakeup();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    reset();

    for(auto& feature: _features)
        feature.second->configure();
}

void Device::reset()
{
    if(_reset_mechanism)
        (*_reset_mechanism)();
    else
        logPrintf(DEBUG, "%s:%d tried to reset, but no reset mechanism was "
                         "available.", _path.c_str(), _index);
}

Device::Config& Device::config()
{
    return _config;
}

hidpp20::Device& Device::hidpp20()
{
    return _hidpp20;
}

void Device::_makeResetMechanism()
{
    try {
        hidpp20::Reset reset(&_hidpp20);
        _reset_mechanism = std::make_unique<std::function<void()>>(
                [dev=&this->_hidpp20]{
                    hidpp20::Reset reset(dev);
                        reset.reset(reset.getProfile());
                });
    } catch(hidpp20::UnsupportedFeature& e) {
        // Reset unsupported, ignore.
    }
}

Device::Config::Config(const std::shared_ptr<Configuration>& config, Device*
    device) : _device (device), _config (config)
{
    try {
        _root_setting = config->getDevice(device->name());
    } catch(Configuration::DeviceNotFound& e) {
        logPrintf(INFO, "Device %s not configured, using default config.",
                device->name().c_str());
    }
}

libconfig::Setting& Device::Config::getSetting(const std::string& path)
{
    return _config->getSetting(_root_setting + '/' + path);
}

Device::IPC::IPC(Device *device) : ipc::IPCInterface("device/" +
std::to_string(device->_device_id), "Device"), _device (device)
{
    ipc::IPCArgsInfo status_signal_args;
    _signals.emplace("wakeup", status_signal_args);
    _signals.emplace("sleep", status_signal_args);

    ipc::IPCProperty name_property = {
            ipc::IPCVariant(_device->name()),
            ipc::IPCVariant::TypeInfo(ipc::IPCVariant::TypeInfo::String),
            true,
            false
    };

    ipc::IPCProperty pid_property = {
            ipc::IPCVariant(_device->pid()),
            ipc::IPCVariant::TypeInfo(ipc::IPCVariant::TypeInfo::UInt16),
            true,
            false
    };

    ipc::IPCProperty asleep_property = {
            ipc::IPCVariant(false),
            ipc::IPCVariant::TypeInfo(ipc::IPCVariant::TypeInfo::Boolean),
            true,
            false
    };

    std::vector<ipc::IPCVariant> null_array;
    ipc::IPCProperty features_property = {
            ipc::IPCVariant(null_array, ipc::IPCVariant::TypeInfo("as")),
            ipc::IPCVariant::TypeInfo("as"),
            true,
            false
    };

    std::string recv_name;
    if(_device->_receiver) {
        recv_name = std::to_string(_device->_receiver->deviceId());
    }
    ipc::IPCProperty recv_property = {
            ipc::IPCVariant(recv_name),
            ipc::IPCVariant::TypeInfo(ipc::IPCVariant::TypeInfo::String),
            true,
            false
    };

    ipc::IPCProperty rawpath_property = {
            ipc::IPCVariant(_device->_path),
            ipc::IPCVariant::TypeInfo(ipc::IPCVariant::TypeInfo::String),
            true,
            false
    };

    ipc::IPCProperty devindex_property = {
            ipc::IPCVariant(_device->_index),
            ipc::IPCVariant::TypeInfo(ipc::IPCVariant::TypeInfo::Byte),
            true,
            false
    };

    _properties.emplace("name", name_property);
    _properties.emplace("pid", pid_property);
    _properties.emplace("supportedFeatures", features_property);
    _properties.emplace("asleep", asleep_property);
    _properties.emplace("receiver", recv_property);
    _properties.emplace("rawPath", rawpath_property);
    _properties.emplace("deviceIndex", devindex_property);

    auto reconf_function = std::make_shared<ipc::IPCFunction>();
    reconf_function->function = [dev=this->_device](const ipc::IPCFunctionArgs&
            args)->ipc::IPCFunctionArgs {
        (void)args;
        task::spawn([dev]{
            dev->reset();
            for (auto &feature: dev->_features)
                feature.second->configure();
        });
        return {};
    };
    _functions.emplace("reconfigure", reconf_function);
}

void Device::IPC::sleep()
{
    _properties["asleep"].property = true;
    emitSignal("sleep", {});
}

void Device::IPC::wakeup()
{
    _properties["asleep"].property = false;
    emitSignal("wakeup", {});
}

void Device::IPC::initFeatures()
{
    auto features_prop = _properties["supportedFeatures"];
    auto features = (std::vector<ipc::IPCVariant>&)features_prop.property;
    features.clear();
    for(auto& it : _device->_features)
        features.emplace_back(it.first);
    features_prop.property = features;
    _properties["supportedFeatures"] = features_prop;
}