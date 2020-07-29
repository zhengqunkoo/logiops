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
#include <cassert>
#include "IPCServer.h"
#include "../util/log.h"
#include "util/VariantTranslator.h"

using namespace logid::ipc;

/* DBus Implementation:
 * The IPCServer class is supposed to handle all OS-specific code.
 * On operating systems that do not support DBus, this class can be replaced
 * with a different implementation.
 */

IPCServer::IPCServer()
{
    GError* err;
    _bus_type = G_BUS_TYPE_SYSTEM;
    _connection = g_bus_get_sync(_bus_type, nullptr, &err);

    if(!_connection) {
        logPrintf(WARN, "Could not open system bus, trying session bus");
        _bus_type = G_BUS_TYPE_SESSION;
        _connection = g_bus_get_sync(_bus_type, nullptr, &err);
        if(!_connection)
            throw std::runtime_error("Failed to open DBus connection");
    }

    auto* server_ptr = new IPCServer*;
    (*server_ptr) = this;

    _gdbus_name = g_bus_own_name_on_connection(_connection, LOGID_DBUS_NAME,
            G_BUS_NAME_OWNER_FLAGS_NONE, IPCServer::name_acquired_handler,
            IPCServer::name_lost_handler, (gpointer)server_ptr,
            IPCServer::free_gpointer_ptr);

    if(!_gdbus_name)
        throw std::runtime_error("Failed to open DBus connection");

    _object_manager = g_dbus_object_manager_server_new(
            LOGID_DBUS_OBJECTMANAGER_NODE);
    g_dbus_object_manager_server_set_connection(_object_manager, _connection);
}

IPCServer::~IPCServer()
{
    g_bus_unown_name(_gdbus_name);

    if(!g_dbus_connection_close_sync(_connection, nullptr, nullptr))
        logPrintf(ERROR, "Failed to close DBus connection");

    for(auto& node : _nodes) {
        for(auto& iface : node.second)
            iface.second.interface->unregisterInterface();
    }
}

void IPCServer::listen()
{
    auto* loop = g_main_loop_new(nullptr, false);
    g_main_loop_run(loop);
}

const GDBusInterfaceVTable IPCServer::interface_vtable = {
        .method_call = IPCServer::gdbus_method_call,
        .get_property = IPCServer::gdbus_get_property,
        .set_property = IPCServer::gdbus_set_property,
        .padding = {}
};

void IPCServer::registerInterface(IPCInterface *ipc_interface)
{
    auto* server_ptr = new IPCServer*;
    (*server_ptr) = this;

    auto* info = _makeInterfaceInfo(ipc_interface);

    auto registration_id = g_dbus_connection_register_object(_connection,
            ipc_interface->node().c_str(),
            info, /// TODO: Warning: mixing shared_ptr with raw ptr
            &interface_vtable,
            (gpointer)server_ptr,
            free_gpointer_ptr,
            nullptr);

    _nodes[ipc_interface->node()][ipc_interface->name()] = {
            ipc_interface,
            registration_id,
            info
    };
}

void IPCServer::unregisterInterface(const std::string& node, const std::string&
    interface)
{
    auto node_it = _nodes.find(node);
    if(node_it == _nodes.end())
        return;

    auto interface_it = node_it->second.find(interface);
    if(interface_it == node_it->second.end())
        return;

   g_dbus_connection_unregister_object(_connection,
           interface_it->second.registrationId);

   g_dbus_interface_info_unref(interface_it->second.info);

   node_it->second.erase(interface_it);
   if(node_it->second.empty())
       _nodes.erase(node_it);
}

void IPCServer::emitSignal(IPCInterface *interface, const std::string &signal,
        const IPCFunctionArgs &params)
{
    GVariant* gparams = nullptr;

    if(!params.empty()) {
        auto *gstruct = g_new(GVariant*, params.size());
        for (std::size_t i = 0; i < params.size(); i++)
            gstruct[i] = toGVariant(params[i]);
        gparams = g_variant_new_tuple(gstruct, params.size());
    }

    if(!g_dbus_connection_emit_signal(_connection, nullptr,
            interface->node().c_str(), interface->name().c_str(),
            signal.c_str(), gparams, nullptr))
        throw std::runtime_error("signal broadcast failed");
}

void IPCServer::gdbus_method_call(GDBusConnection* connection,
        const gchar *sender, const gchar *object_path,
        const gchar *interface_name, const gchar *method_name,
        GVariant *parameters, GDBusMethodInvocation *invocation,
        gpointer user_data)
{
    // Suppress unused warnings
    ///TODO: Use sender and connection?
    (void)sender; (void)connection;

    auto* server_ptr = *(IPCServer**)user_data;
    if(!server_ptr) {
        logPrintf(DEBUG, "Ignoring gdbus_method_call on null IPCServer.");
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                G_DBUS_ERROR_FAILED, "null IPCServer");
        return;
    }

    auto node = server_ptr->_nodes.find(object_path);
    if(node == server_ptr->_nodes.end()) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                G_DBUS_ERROR_UNKNOWN_OBJECT, "Unknown object");
        return;
    }

    auto interface = node->second.find(interface_name);
    if(interface == node->second.end()) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                G_DBUS_ERROR_UNKNOWN_INTERFACE, "Unknown interface");
        return;
    }

    auto function_it = interface->second.interface->getFunctions().find
            (method_name);
    if(function_it == interface->second.interface->getFunctions().end()) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                G_DBUS_ERROR_UNKNOWN_METHOD,"Unknown method");
        return;
    }

    std::shared_ptr<IPCFunction> function = function_it->second;
    if(g_variant_n_children(parameters) != function->args.size()) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                G_DBUS_ERROR_INVALID_ARGS,"Invalid argument");
        return;
    }

    IPCFunctionArgs args;

    try {
        for(std::size_t i = 0; i < function->args.size(); i++) {
            auto* arg = g_variant_get_child_value(parameters, i);
            if(g_variant_check_format_string(parameters,
                    function->args[i].second.typeSignature().c_str(), false)) {
                g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                        G_DBUS_ERROR_INVALID_ARGS,"Invalid argument");
                return;
            }

            args.push_back(translateGVariant(arg));
        }
    } catch(IPCVariant::InvalidType& e) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                G_DBUS_ERROR_INVALID_ARGS,"Invalid argument");
        return;
    }

    IPCFunctionArgs response;
    try {
        response = (function->function)(args);
    } catch(std::exception& e) {
        logPrintf(ERROR, "Error calling IPC function %s:%s:%s - %s",
                object_path, interface_name, method_name, e.what());
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                G_DBUS_ERROR_FAILED, "Internal error: %s", e.what());
        return;
    }

    if(response.empty()) {
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }

    auto* gstruct = g_new(GVariant*, response.size());
    for(std::size_t i = 0; i < response.size(); i++)
        gstruct[i] = toGVariant(response[i]);

    g_dbus_method_invocation_return_value(invocation,
            g_variant_new_tuple(gstruct, response.size()));
}

GVariant* IPCServer::gdbus_get_property(GDBusConnection *connection,
        const gchar *sender, const gchar *object_path,
        const gchar *interface_name, const gchar *property_name,
        GError **error, gpointer user_data)
{
    // Suppress unused warnings
    ///TODO: Use sender and connection?
    (void)sender; (void)connection;

    auto* server_ptr = *(IPCServer**)user_data;
    if(!server_ptr) {
        logPrintf(DEBUG, "Ignoring gdbus_get_property on null IPCServer.");
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "null IPCServer");
        return nullptr;
    }

    auto node = server_ptr->_nodes.find(object_path);
    if(node == server_ptr->_nodes.end()) {
        g_set_error(error, G_DBUS_ERROR,G_DBUS_ERROR_UNKNOWN_OBJECT,
                "Unknown object");
        return nullptr;
    }

    auto interface = node->second.find(interface_name);
    if(interface == node->second.end()) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE,
                "Unknown interface");
        return nullptr;
    }

    auto property = interface->second.interface->getProperties().find(
            property_name);
    if(property == interface->second.interface->getProperties().end()) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                    "Unknown property");
        return nullptr;
    }

    if(!property->second.readable) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                "Property not readable");
    }

    return toGVariant(interface->second.interface->getProperty(property_name));
}

gboolean IPCServer::gdbus_set_property(GDBusConnection *connection,
        const gchar *sender, const gchar *object_path,
        const gchar *interface_name, const gchar *property_name,
        GVariant *value, GError **error, gpointer user_data)
{
    // Suppress unused warnings
    ///TODO: Use sender and connection?
    (void)sender; (void)connection;

    auto* server_ptr = *(IPCServer**)user_data;
    if(!server_ptr) {
        logPrintf(DEBUG, "Ignoring gdbus_get_property on null IPCServer.");
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "null IPCServer");
        return false;
    }

    auto node = server_ptr->_nodes.find(object_path);
    if(node == server_ptr->_nodes.end()) {
        g_set_error(error, G_DBUS_ERROR,G_DBUS_ERROR_UNKNOWN_OBJECT,
                    "Unknown object");
        return false;
    }

    auto interface = node->second.find(interface_name);
    if(interface == node->second.end()) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE,
                    "Unknown interface");
        return false;
    }

    auto property = interface->second.interface->getProperties().find(
            property_name);
    if(property == interface->second.interface->getProperties().end()) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                    "Unknown property");
        return false;
    }

    const auto& ipc_variant = translateGVariant(value);

    if(ipc_variant.type() != property->second.type) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "Invalid argument");
        return false;
    }

    if(!property->second.writable) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                    "Property not writable");
    }

    interface->second.interface->setProperty(property_name, ipc_variant);
    return true;
}

void IPCServer::name_acquired_handler(GDBusConnection *connection,
        const gchar *name, gpointer user_data)
{
    (void)connection; // Suppress unused warning

    auto* server_ptr = *(IPCServer**)user_data;
    if(!server_ptr)
        return;

    logPrintf(DEBUG, "Successfully acquired DBus name %s", name);
}

void IPCServer::name_lost_handler(GDBusConnection* connection,
        const gchar* name, gpointer user_data)
{
    (void)connection; // Suppress unused warning

    auto* server_ptr = *(IPCServer**)user_data;
    if(!server_ptr)
        return;

    if(server_ptr->_bus_type == G_BUS_TYPE_SYSTEM) {
        logPrintf(WARN, "Failed to own %s on system bus, trying session bus.",
                name);
        g_dbus_connection_close_sync(server_ptr->_connection, nullptr, nullptr);
        server_ptr->_bus_type = G_BUS_TYPE_SESSION;
        server_ptr->_connection = g_bus_get_sync(server_ptr->_bus_type,
                nullptr, nullptr);
        g_bus_own_name_on_connection(server_ptr->_connection, LOGID_DBUS_NAME,
                G_BUS_NAME_OWNER_FLAGS_NONE, IPCServer::name_acquired_handler,
                IPCServer::name_lost_handler, user_data,
                IPCServer::free_gpointer_ptr);
        for(auto& node : server_ptr->_nodes) {
            for(auto& interface : node.second) {
                auto* interface_data = new IPCServer*;
                (*interface_data) = server_ptr;
                interface.second.registrationId =
                        g_dbus_connection_register_object(
                                server_ptr->_connection,
                                node.first.c_str(),
                                interface.second.info,
                                &interface_vtable,
                                (gpointer)interface_data,
                                free_gpointer_ptr,
                                nullptr);
            }
        }
    } else {
        logPrintf(ERROR, "Failed to own %s on system bus and session bus",
                name);
        std::terminate();
    }
}

void IPCServer::free_gpointer_ptr(gpointer user_data)
{
    auto* ptr = (void**)user_data;
    (*ptr) = nullptr;
    delete ptr;
}

GDBusInterfaceInfo* IPCServer::_makeInterfaceInfo(IPCInterface* interface)
{
    auto* info = g_new(GDBusInterfaceInfo, 1);
    info->ref_count = 1;
    info->annotations = nullptr;
    info->name = g_strdup(interface->name().c_str());

    const auto& methods = interface->getFunctions();
    if(methods.empty()) {
        info->methods = nullptr;
    } else {
        info->methods = g_new(GDBusMethodInfo*, methods.size() + 1);
        info->methods[methods.size()] = nullptr;
    }

    std::size_t i = 0;
    for(const auto& it : methods) {
        info->methods[i] = g_new(GDBusMethodInfo, 1);
        info->methods[i]->ref_count = 1;
        info->methods[i]->name = g_strdup(it.first.c_str());

        const auto& in_args = it.second->args;

        if(in_args.empty()) {
            info->methods[i]->in_args = nullptr;
        } else {
            info->methods[i]->in_args = g_new(GDBusArgInfo*,
                    in_args.size()+1);
            // Null-terminate
            info->methods[i]->in_args[in_args.size()] = nullptr;

            for(std::size_t j = 0; j < in_args.size(); j++) {
                info->methods[i]->in_args[j] = g_new(GDBusArgInfo, 1);
                info->methods[i]->in_args[j]->annotations = nullptr;
                info->methods[i]->in_args[j]->name = g_strdup(
                        in_args[j].first.c_str());
                info->methods[i]->in_args[j]->signature = g_strdup(
                        in_args[j].second.typeSignature().c_str());
                info->methods[i]->in_args[j]->ref_count = 1;
            }
        }

        const auto& out_args = it.second->responses;

        if(out_args.empty()) {
            info->methods[i]->out_args = nullptr;
        } else {
            info->methods[i]->out_args = g_new(GDBusArgInfo*,
                                              out_args.size()+1);
            // Null-terminate
            info->methods[i]->out_args[out_args.size()] = nullptr;

            for(std::size_t j = 0; j < out_args.size(); j++) {
                info->methods[i]->out_args[j] = g_new(GDBusArgInfo, 1);
                info->methods[i]->out_args[j]->annotations = nullptr;
                info->methods[i]->out_args[j]->name = g_strdup(
                        out_args[j].first.c_str());
                info->methods[i]->out_args[j]->signature = g_strdup(
                        out_args[j].second.typeSignature().c_str());
                info->methods[i]->out_args[j]->ref_count = 1;
            }
        }

        info->methods[i]->annotations = nullptr; ///TODO: Annotations?
        i++;
    }

    const auto& properties = interface->getProperties();
    if(properties.empty()) {
        info->properties = nullptr;
    } else {
        info->properties = g_new(GDBusPropertyInfo*, properties.size()+1);
        info->properties[properties.size()] = nullptr;
    }

    i = 0;
    for(const auto& it: properties) {
        info->properties[i] = g_new(GDBusPropertyInfo, 1);
        info->properties[i]->ref_count = 1;
        info->properties[i]->name = g_strdup(it.first.c_str());
        info->properties[i]->signature = g_strdup(
                it.second.type.typeSignature().c_str());
        info->properties[i]->flags = G_DBUS_PROPERTY_INFO_FLAGS_NONE;
        if(it.second.readable)
            info->properties[i]->flags =(GDBusPropertyInfoFlags)(info->
                    properties[i]->flags | G_DBUS_PROPERTY_INFO_FLAGS_READABLE);
        if(it.second.writable)
            info->properties[i]->flags =(GDBusPropertyInfoFlags)(info->
                    properties[i]->flags | G_DBUS_PROPERTY_INFO_FLAGS_WRITABLE);

        info->properties[i]->annotations = nullptr;
        i++;
    }

    const auto& signals = interface->getSignals();
    if(signals.empty()) {
        info->signals = nullptr;
    } else {
        info->signals = g_new(GDBusSignalInfo*, signals.size()+1);
        info->signals[signals.size()] = nullptr;
    }

    i = 0;
    for(const auto& it: signals) {
        info->signals[i] = g_new(GDBusSignalInfo, 1);
        info->signals[i]->ref_count = 1;
        info->signals[i]->name = g_strdup(it.first.c_str());
        info->signals[i]->annotations = nullptr;

        const auto& args = it.second;
        if(args.empty()) {
            info->signals[i]->args = nullptr;
        } else {
            info->signals[i]->args = g_new(GDBusArgInfo*, args.size()+1);
            info->signals[i]->ref_count = 1;
            info->signals[i]->args[args.size()] = nullptr;

            for(std::size_t j = 0; j < args.size(); j++) {
                info->signals[i]->args[j] = g_new(GDBusArgInfo, 1);
                info->signals[i]->args[j]->ref_count = 1;
                info->signals[i]->args[j]->name = g_strdup(
                        args[j].first.c_str());
                info->signals[i]->args[j]->signature = g_strdup(
                        args[j].second.typeSignature().c_str());

                info->signals[i]->args[j]->annotations = nullptr;
            }
        }

        i++;
    }

    return info;
}
