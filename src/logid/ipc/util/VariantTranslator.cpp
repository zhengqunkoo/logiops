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
#include <map>
#include <stdexcept>
#include <cassert>
#include "VariantTranslator.h"

using namespace logid::ipc;

IPCVariant logid::ipc::translateGVariant(GVariant* variant)
{
    IPCVariant::TypeInfo type(g_variant_get_type_string(variant));

    if(type.primaryType() == IPCVariant::TypeInfo::Array) {
        const auto& array_type = type.arrayType();
        if(array_type.primaryType() == IPCVariant::TypeInfo::Struct) {
            if(array_type.structFormat().size() == 2) {
                // This is a dict
                auto ts = type.typeSignature();
                ts.replace(ts.size()-1, 1, "}");
                ts.replace(0, 2, "{");
                type = IPCVariant::TypeInfo(ts);
            }
        }
    }

    gsize length;
    std::vector<IPCVariant> array;

    switch(type.primaryType()) {
    case IPCVariant::TypeInfo::None:
        throw std::runtime_error("invalid none typeinfo");
    case IPCVariant::TypeInfo::Int16:
        return IPCVariant(g_variant_get_int16(variant));
    case IPCVariant::TypeInfo::UInt16:
        return IPCVariant(g_variant_get_uint16(variant));
    case IPCVariant::TypeInfo::Int32:
        return IPCVariant(g_variant_get_int32(variant));
    case IPCVariant::TypeInfo::UInt32:
        return IPCVariant(g_variant_get_uint32(variant));
    case IPCVariant::TypeInfo::Int64:
        return IPCVariant(g_variant_get_int64(variant));
    case IPCVariant::TypeInfo::UInt64:
        return IPCVariant(g_variant_get_uint64(variant));
    case IPCVariant::TypeInfo::Double:
        return IPCVariant(g_variant_get_double(variant));
    case IPCVariant::TypeInfo::Byte:
        return IPCVariant(g_variant_get_byte(variant));
    case IPCVariant::TypeInfo::String:
    case IPCVariant::TypeInfo::Signature:
    case IPCVariant::TypeInfo::ObjectPath:
        return IPCVariant(std::string(g_variant_get_string(variant, &length),
                length), type.primaryType());
    case IPCVariant::TypeInfo::Boolean:
        return IPCVariant((bool)g_variant_get_boolean(variant));
    case IPCVariant::TypeInfo::Array:
    case IPCVariant::TypeInfo::Struct:
        length = g_variant_n_children(variant);
        array.resize(length);
        for(gsize i = 0; i < length; i++) {
            array[i] = translateGVariant(g_variant_get_child_value(variant, i));
        }

        return IPCVariant(array, type);
    case IPCVariant::TypeInfo::Dict:
        IPCVariantDict dict;
        length = g_variant_n_children(variant);
        for(gsize i = 0; i < length; i++) {
            auto* element = g_variant_get_child_value(variant, i);
            // Element count should be 2
            auto* key = g_variant_get_child_value(element, 0);
            auto* val = g_variant_get_child_value(element, 1);
            dict[translateGVariant(key)] = translateGVariant(val);
        }
        return IPCVariant(dict, type);
    }

    throw IPCVariant::InvalidType();
}

GVariant* logid::ipc::toGVariant(const IPCVariant &ipc_variant)
{
    IPCVariant::TypeInfo::Type primary_type = ipc_variant.type().primaryType();
    GVariantType* child_type = nullptr;

    switch(primary_type) {
    case IPCVariant::TypeInfo::None:
        throw std::runtime_error("invalid none typeinfo");
    case IPCVariant::TypeInfo::Int16:
        return g_variant_new_int16(ipc_variant);
    case IPCVariant::TypeInfo::UInt16:
        return g_variant_new_uint16(ipc_variant);
    case IPCVariant::TypeInfo::Int32:
        return g_variant_new_int32(ipc_variant);
    case IPCVariant::TypeInfo::UInt32:
        return g_variant_new_uint32(ipc_variant);
    case IPCVariant::TypeInfo::Int64:
        return g_variant_new_int64(ipc_variant);
    case IPCVariant::TypeInfo::UInt64:
        return g_variant_new_uint64(ipc_variant);
    case IPCVariant::TypeInfo::Double:
        return g_variant_new_double(ipc_variant);
    case IPCVariant::TypeInfo::Byte:
        return g_variant_new_byte(ipc_variant);
    case IPCVariant::TypeInfo::String:
        return g_variant_new_string((const gchar*)ipc_variant);
    case IPCVariant::TypeInfo::Signature:
        return g_variant_new_signature((const gchar*)ipc_variant);
    case IPCVariant::TypeInfo::ObjectPath:
        return g_variant_new_object_path((const gchar*)ipc_variant);
    case IPCVariant::TypeInfo::Boolean:
        return g_variant_new_boolean((bool)ipc_variant);
    case IPCVariant::TypeInfo::Array: {
        auto array = (const std::vector<IPCVariant>&)ipc_variant;
        auto* garray = g_new(GVariant*, array.size());
        child_type = g_variant_type_new(ipc_variant.type().arrayType()
                .typeSignature().c_str());
        for(std::size_t i = 0; i < array.size(); i++)
            garray[i] = toGVariant(array[i]);

        return g_variant_new_array(child_type, garray, array.size());
    }
    case IPCVariant::TypeInfo::Struct: {
        auto vstruct = (const std::vector<IPCVariant>&)ipc_variant;
        auto* gstruct = g_new(GVariant*, vstruct.size());
        for(std::size_t i = 0; i < vstruct.size(); i++)
            gstruct[i] = toGVariant(vstruct[i]);
        return g_variant_new_tuple(gstruct, vstruct.size());
    }
    case IPCVariant::TypeInfo::Dict:
        /// TODO: Make dict?
        auto dict = (const IPCVariantDict&)ipc_variant;
        auto* garray = g_new(GVariant*, dict.size());

        auto dict_type = ipc_variant.type().dictType();
        std::string dstruct_type = dict_type.first.typeSignature() +
                dict_type.second.typeSignature();
        child_type = g_variant_type_new(dstruct_type.c_str());

        std::size_t i = 0;
        for(auto& it : dict) {
            if(!child_type) {
                std::string type = it.first.type().typeSignature() +
                        it.second.type().typeSignature();
                child_type = g_variant_type_new(type.c_str());
            }
            auto* gstruct = g_new(GVariant*, 2);
            gstruct[0] = toGVariant(it.first);
            gstruct[1] = toGVariant(it.second);
            garray[i] = g_variant_new_tuple(gstruct, 2);
        }
        return g_variant_new_array(child_type, garray, dict.size());
    }

    assert(false); // This shouldn't happen
    return nullptr;
}