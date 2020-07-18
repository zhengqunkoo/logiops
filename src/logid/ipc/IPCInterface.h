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
#ifndef LOGID_IPC_INTERFACE_H
#define LOGID_IPC_INTERFACE_H

#include <functional>
#include "IPCVariant.h"

namespace logid {
namespace ipc
{
    class IPCServer;

    typedef std::vector<IPCVariant> IPCFunctionArgs;
    typedef std::vector<std::pair<std::string, IPCVariant::TypeInfo>>
        IPCArgsInfo;

    struct IPCFunction
    {
        IPCArgsInfo args;
        IPCArgsInfo responses;
        std::function<IPCFunctionArgs(const IPCFunctionArgs&)> function;
    };

    struct IPCProperty
    {
        IPCVariant property;
        IPCVariant::TypeInfo type;
        bool readable;
        bool writable;
    };

    class IPCInterface
    {
    public:
        IPCInterface(std::string node, std::string name);
        ~IPCInterface();

        /* TODO: In client
         * IPCFunctionArgs callFunction(std::string function, IPCFunctionArgs
         *    args);
         */

        virtual void registerInterface(IPCServer* server);
        virtual void unregisterInterface();

        const std::map<std::string, std::shared_ptr<IPCFunction>>&
            getFunctions() const;
        const std::map<std::string, IPCProperty>& getProperties() const;
        const std::map<std::string, IPCArgsInfo>& getSignals() const;

        void setProperty(const std::string& property, const IPCVariant& value);
        const IPCVariant& getProperty(const std::string& property);

        void emitSignal(const std::string& signal, const IPCFunctionArgs& args);

        const std::string& name() const;
        const std::string& node() const;
    protected:
        std::map<std::string, std::shared_ptr<IPCFunction>> _functions;
        std::map<std::string, IPCProperty> _properties;
        std::map<std::string, IPCArgsInfo> _signals;

        std::string _node;
        std::string _name;

        IPCServer* _server = nullptr;
    };
}}

#endif //LOGID_IPC_INTERFACE_H
