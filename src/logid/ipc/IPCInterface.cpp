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
#include <stdexcept>
#include <utility>
#include "IPCInterface.h"
#include "IPCServer.h"

using namespace logid::ipc;

IPCInterface::IPCInterface(const std::string& node, const std::string& name)
{
    if(node.empty())
        _node = LOGID_DBUS_OBJECTMANAGER_NODE;
    else
        _node = LOGID_DBUS_OBJECTMANAGER_NODE "/" + node;
    if(name.empty())
        _name = LOGID_DBUS_NAME;
    else
        _name = LOGID_DBUS_NAME "." + name;
}

IPCInterface::~IPCInterface()
{
    unregisterInterface();
}

void IPCInterface::registerInterface(IPCServer *server)
{
    _server = server;
    _server->registerInterface(this);
}

void IPCInterface::unregisterInterface()
{
    if(_server) {
        _server->unregisterInterface(_node, _name);
        _server = nullptr;
    }
}

void logid::ipc::registerAuto(IPCInterface *interface)
{
    interface->registerInterface(server.get());
}

void logid::ipc::unregisterAuto(IPCInterface* interface)
{
    interface->unregisterInterface();
}

void IPCInterface::setProperty(const std::string& property, const IPCVariant& value)
{
    auto it = _properties.find(property);
    if(it == _properties.end())
        throw std::invalid_argument(property);

    if(it->second.type != value.type())
        throw std::invalid_argument(value);

    it->second.property = value;
}

const std::map<std::string, std::shared_ptr<IPCFunction>>&
        IPCInterface::getFunctions() const
{
   return _functions;
}

const std::map<std::string, IPCProperty>& IPCInterface::getProperties() const
{
    return _properties;
}

const std::map<std::string, IPCArgsInfo>& IPCInterface::getSignals() const
{
    return _signals;
}

const IPCVariant& IPCInterface::getProperty(const std::string &property)
{
    auto it = _properties.find(property);
    if(it == _properties.end())
        throw std::invalid_argument(property);

    return it->second.property;
}

void IPCInterface::emitSignal(const std::string& signal,
        const IPCFunctionArgs& args)
{
    if(!_server) ///TODO: Warn
        return;

    auto signal_it = _signals.find(signal);
    if(signal_it == _signals.end())
        throw std::invalid_argument(signal);

    if(signal_it->second.size() != args.size())
        throw std::invalid_argument("args");

    for(std::size_t i = 0; i < args.size(); i++) {
        if(args[i].type() != signal_it->second[i].second)
            throw std::invalid_argument("type");
    }

    _server->emitSignal(this, signal, args);
}

const std::string& IPCInterface::name() const
{
    return _name;
}

const std::string& IPCInterface::node() const
{
    return _node;
}
