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
#ifndef LOGID_IPC_SERVER_H
#define LOGID_IPC_SERVER_H

#include <string>
#include <map>
#include "IPCVariant.h"
#include "IPCInterface.h"

extern "C"
{
#include <gio/gio.h>
}

#define LOGID_DBUS_NAME "pizza.pixl.logiops"
#define LOGID_DBUS_OBJECTMANAGER_NODE "/pizza/pixl/logiops"

namespace logid {
namespace ipc
{
    class IPCServer
    {
    public:
        IPCServer();
        ~IPCServer();

        void listen();
        void stop();

        void registerInterface(IPCInterface* interface);
        void unregisterInterface(const std::string& node, const std::string&
            interface);

        void emitSignal(IPCInterface* interface, const std::string& signal,
                const IPCFunctionArgs& params);
    private:
        /* Begin IPC backend dependent code */
        struct Interface
        {
            IPCInterface* interface;
            guint registrationId;

            GDBusInterfaceInfo* info;
        };

        GIOStream _gio_stream{};
        GDBusConnection* _connection;
        GDBusObjectManagerServer* _object_manager;
        guint _gdbus_name = 0;

        // C-style GDBus callbacks
        static void gdbus_method_call(GDBusConnection *connection,
                const gchar *sender, const gchar *object_path,
                const gchar *interface_name, const gchar *method_name,
                GVariant *parameters, GDBusMethodInvocation *invocation,
                gpointer user_data);

        static GVariant* gdbus_get_property(GDBusConnection *connection,
                const gchar *sender, const gchar *object_path,
                const gchar *interface_name, const gchar *property_name,
                GError **error, gpointer user_data);
        static gboolean gdbus_set_property(GDBusConnection *connection,
                const gchar *sender, const gchar *object_path,
                const gchar *interface_name, const gchar *property_name,
                GVariant *value, GError **error, gpointer user_data);

        static void name_lost_handler(GDBusConnection* connection,
                const gchar* name, gpointer user_data);

        static void free_gpointer_ptr(gpointer user_data);
        /* End IPC backend dependent code */

        std::map<std::string, std::map<std::string, Interface>> _nodes;

        GDBusInterfaceInfo* _makeInterfaceInfo(IPCInterface* interface);

        static const GDBusInterfaceVTable interface_vtable;
    };

    extern std::shared_ptr<IPCServer> server;
}}

#endif //LOGID_IPC_SERVER_H
