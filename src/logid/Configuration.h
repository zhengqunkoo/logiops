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

#ifndef LOGID_CONFIGURATION_H
#define LOGID_CONFIGURATION_H

#include <map>
#include <libconfig.h++>
#include <memory>
#include <chrono>
#include <set>
#include "ipc/IPCInterface.h"

#define LOGID_DEFAULT_IO_TIMEOUT std::chrono::seconds(2)
#define LOGID_DEFAULT_WORKER_COUNT 4

namespace logid
{
    class Configuration
    {
    public:
        explicit Configuration(const std::string& config_file);
        Configuration();
        libconfig::Setting& getSetting(const std::string& path);
        std::string getDevice(const std::string& name);
        bool isIgnored(uint16_t pid) const;

        void reload();

        class DeviceNotFound : public std::exception
        {
        public:
            explicit DeviceNotFound(std::string name);
            const char* what() const noexcept override;
        private:
            std::string _name;
        };

        std::chrono::milliseconds ioTimeout() const;
        int workerCount() const;
    private:
        void _readConfig();

        class IPC : public ipc::IPCInterface
        {
        public:
            IPC(Configuration* config);
        private:
            void reload();
            Configuration* _config;
        };

        std::map<std::string, std::string> _device_paths;
        std::set<uint16_t> _ignore_list;
        std::chrono::milliseconds _io_timeout = LOGID_DEFAULT_IO_TIMEOUT;
        int _worker_threads = LOGID_DEFAULT_WORKER_COUNT;
        std::string _config_file;
        libconfig::Config _config;

        IPC _ipc_interface;
    };

    extern std::shared_ptr<Configuration> global_config;
}

#endif //LOGID_CONFIGURATION_H
