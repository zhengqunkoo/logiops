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

#ifndef LOGID_DEVICE_H
#define LOGID_DEVICE_H

#include "backend/hidpp/defs.h"
#include "backend/hidpp20/Device.h"
#include "features/DeviceFeature.h"
#include "Configuration.h"
#include "util/log.h"

namespace logid
{
    class Device;
    class Receiver;
    class DeviceManager;

    /* TODO: Implement HID++ 1.0 support
     * Currently, the logid::Device class has a hardcoded requirement
     * for an HID++ 2.0 device.
     */
    class Device
    {
    private:
        class Config;
        class IPC;
    public:
        Device(std::string path, backend::hidpp::DeviceIndex index,
                DeviceManager* manager);
        Device(const std::shared_ptr<backend::raw::RawDevice>& raw_device,
                backend::hidpp::DeviceIndex index, DeviceManager* manager);
        Device(Receiver* receiver, backend::hidpp::DeviceIndex index,
            DeviceManager* manager);
        ~Device();

        const std::string& name() const;
        uint16_t pid() const;
        int deviceId() const;

        Config& config();
        backend::hidpp20::Device& hidpp20();

        const IPC& ipc() const;

        void wakeup();
        void sleep();

        void reset();

        template<typename T>
        std::shared_ptr<T> getFeature(std::string name) {
            auto it = _features.find(name);
            if(it == _features.end())
                return nullptr;
            try {
                return std::dynamic_pointer_cast<T>(it->second);
            } catch(std::bad_cast& e) {
                logPrintf(ERROR, "bad_cast while getting device feature %s: %s",
                                 name.c_str(), e.what());
                return nullptr;
            }
        }

    private:
        void _init();

        /* Adds a feature without calling an error if unsupported */
        template<typename T>
        void _addFeature(std::string name)
        {
            try {
                _features.emplace(name, std::make_shared<T>(this));
            } catch (features::UnsupportedFeature& e) {
            }
        }
        class IPC;
        class Config
        {
        public:
            friend IPC;
            Config(const std::shared_ptr<Configuration>& config, Device*
                device);
            libconfig::Setting& getSetting(const std::string& path);
            void setSetting(const std::string& path, libconfig::Setting&
                setting);
            const std::map<std::string, std::string>& getProfiles() const;
            void setProfile(const std::string& name);

            void saveProfile();
        private:
            Device* _device;
            std::string _root_setting;
            std::string _profile_root;
            std::string _profile_name;
            std::string _default_profile;
            std::map<std::string, std::string> _profiles;
            std::shared_ptr<Configuration> _config;
        };

        backend::hidpp20::Device _hidpp20;
        std::string _path;
        backend::hidpp::DeviceIndex _index;
        std::map<std::string, std::shared_ptr<features::DeviceFeature>>
            _features;
        Config _config;

        Receiver* _receiver;
        DeviceManager* _manager;
        int _device_id;

        class IPC : public ipc::IPCInterface
        {
        public:
            explicit IPC(Device* device);
            void sleep();
            void wakeup();
            void initFeatures();
            void updateProfiles();
        private:
            Device* _device;
        };

        IPC _ipc_interface;

        void _makeResetMechanism();
        std::unique_ptr<std::function<void()>> _reset_mechanism;
    };
}

#endif //LOGID_DEVICE_H
