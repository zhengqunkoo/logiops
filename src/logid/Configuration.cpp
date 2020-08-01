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

#include <utility>
#include <vector>
#include <map>

#include "Configuration.h"
#include "util/log.h"

using namespace logid;
using namespace libconfig;
using namespace std::chrono;

Configuration::Configuration(const std::string& config_file) :
    _config_file (config_file), _ipc_interface(this)
{
    try {
        _config.readFile(config_file.c_str());
    } catch(const FileIOException &e) {
        logPrintf(ERROR, "I/O Error while reading %s: %s", config_file.c_str(),
                e.what());
        throw e;
    } catch(const ParseException &e) {
        logPrintf(ERROR, "Parse error in %s, line %d: %s", e.getFile(),
                e.getLine(), e.getError());
        throw e;
    }

    _readConfig();
    ipc::registerAuto(&_ipc_interface);
}

Configuration::Configuration() : _ipc_interface(this)
{
}

void Configuration::_readConfig()
{
    const Setting &root = _config.getRoot();

    try {
        auto& worker_count = root["workers"];
        if(worker_count.getType() == Setting::TypeInt) {
            _worker_threads = worker_count;
            if(_worker_threads < 0)
                logPrintf(WARN, "Line %d: workers cannot be negative.",
                          worker_count.getSourceLine());
        } else {
            logPrintf(WARN, "Line %d: workers must be an integer.",
                      worker_count.getSourceLine());
        }
    } catch(const SettingNotFoundException& e) {
        // Ignore
    }

    try {
        auto& timeout = root["io_timeout"];
        if(timeout.isNumber()) {
            if(timeout.getType() == Setting::TypeFloat)
                _io_timeout = duration_cast<milliseconds>(
                        duration<double, std::milli>(timeout));
            else
                _io_timeout = milliseconds((int)timeout);
        } else
            logPrintf(WARN, "Line %d: io_timeout must be a number.",
                      timeout.getSourceLine());
    } catch(const SettingNotFoundException& e) {
        // Ignore
    }

    try {
        auto& devices = root["devices"];

        for(int i = 0; i < devices.getLength(); i++) {
            const Setting& device = devices[i];
            std::string name;
            try {
                if(!device.lookupValue("name", name)) {
                    logPrintf(WARN, "Line %d: 'name' must be a string, skipping"
                                    " device.", device["name"].getSourceLine());
                    continue;
                }
            } catch(SettingNotFoundException &e) {
                logPrintf(WARN, "Line %d: Missing name field, skipping device."
                        , device.getSourceLine());
                continue;
            }
            _device_paths.insert({name, device.getPath()});
        }
    }
    catch(const SettingNotFoundException &e) {
        logPrintf(WARN, "No devices listed in config file.");
    }

    try {
        auto& ignore = root.lookup("ignore");
        if(ignore.getType() == libconfig::Setting::TypeInt) {
            _ignore_list.insert((int)ignore);
        } else if(ignore.isList() || ignore.isArray()) {
            int ignore_count = ignore.getLength();
            for(int i = 0; i < ignore_count; i++) {
                if(ignore[i].getType() != libconfig::Setting::TypeInt) {
                    logPrintf(WARN, "Line %d: ignore must refer to device PIDs",
                              ignore[i].getSourceLine());
                    if(ignore.isArray())
                        break;
                } else
                    _ignore_list.insert((int)ignore[i]);
            }
        }
    } catch(const SettingNotFoundException& e) {
        // May be called blacklist
        try {
            auto& ignore = root.lookup("blacklist");
            if(ignore.getType() == libconfig::Setting::TypeInt) {
                _ignore_list.insert((int)ignore);
            } else if(ignore.isList() || ignore.isArray()) {
                int ignore_count = ignore.getLength();
                for(int i = 0; i < ignore_count; i++) {
                    if(ignore[i].getType() != libconfig::Setting::TypeInt) {
                        logPrintf(WARN, "Line %d: blacklist must refer to "
                                        "device PIDs",
                                  ignore[i].getSourceLine());
                        if(ignore.isArray())
                            break;
                    } else
                        _ignore_list.insert((int)ignore[i]);
                }
            }
        } catch(const SettingNotFoundException& e) {
            // Ignore
        }
    }
}

void Configuration::reload()
{
    if(_config_file.empty()) {
        logPrintf(WARN, "No config file loaded, cannot reload.");
        return;
    }

    try {
        _config.readFile(_config_file.c_str());
    } catch(const FileIOException &e) {
        logPrintf(ERROR, "I/O Error while reading %s: %s", _config_file.c_str(),
                  e.what());
        logPrintf(INFO, "Keeping old configuration.");
    } catch(const ParseException &e) {
        logPrintf(ERROR, "Parse error in %s, line %d: %s", e.getFile(),
                  e.getLine(), e.getError());
        logPrintf(INFO, "Keeping old configuration.");
    }

    _readConfig();
    logPrintf(INFO, "Reloaded configuration successfully.");
}

libconfig::Setting& Configuration::getSetting(const std::string& path)
{
    return _config.lookup(path);
}

std::string Configuration::getDevice(const std::string& name)
{
    auto it = _device_paths.find(name);
    if(it == _device_paths.end())
        throw DeviceNotFound(name);
    else
        return it->second;
}

bool Configuration::isIgnored(uint16_t pid) const
{
    return _ignore_list.find(pid) != _ignore_list.end();
}

Configuration::DeviceNotFound::DeviceNotFound(std::string name) :
    _name (std::move(name))
{
}

const char * Configuration::DeviceNotFound::what() const noexcept
{
    return _name.c_str();
}

int Configuration::workerCount() const
{
    return _worker_threads;
}

std::chrono::milliseconds Configuration::ioTimeout() const
{
    return _io_timeout;
}

Configuration::IPC::IPC(Configuration* config) : ipc::IPCInterface("",
        "Configuration"), _config (config)
{
    auto function = std::make_shared<ipc::IPCFunction>();
    function->function = [c=this->_config](const ipc::IPCFunctionArgs&)
            ->ipc::IPCFunctionArgs {
                c->reload();
                return {};
            };

    _functions.emplace("reload", function);
}
