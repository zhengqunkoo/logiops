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
#include <cassert>
#include <utility>
#include <algorithm>
#include "IPCVariant.h"

using namespace logid::ipc;

const std::string IPCVariant::SINGLE_TYPES = "nqiuxtdysgob";
const std::string IPCVariant::SPECIAL_TYPES = "a({";
const std::string IPCVariant::ALL_TYPES = SINGLE_TYPES + SPECIAL_TYPES;
const std::string IPCVariant::VALID_CHARS = ALL_TYPES + ")}";

IPCVariant::IPCVariant() : _type()
{
}

IPCVariant::IPCVariant(const char* data, TypeInfo::Type type) :
        _string_data (data), _type (TypeInfo(type))
{
    if(type != TypeInfo::String && type != TypeInfo::Signature &&
       type != TypeInfo::ObjectPath)
        throw InvalidType();
}

IPCVariant::IPCVariant(std::string data, TypeInfo::Type type) :
    _string_data (std::move(data)), _type (TypeInfo(type))
{
    if(type != TypeInfo::String && type != TypeInfo::Signature &&
        type != TypeInfo::ObjectPath)
        throw InvalidType();
}

IPCVariant::IPCVariant(std::vector<IPCVariant> array,
        const TypeInfo& type) : _array (std::move(array)), _type (type)
{
    if(_type.primaryType() == TypeInfo::Array) {
        for(auto& i : _array) {
            if(i.type() != _type.arrayType())
                throw InvalidType();
        }
    } else if(type.primaryType() == TypeInfo::Struct) {
        if(_array.size() != _type.structFormat().size())
            throw InvalidType();
        for(std::size_t i = 0; i < _array.size(); i++) {
            if(_array[i].type() != _type.structFormat()[i])
                throw InvalidType();
        }
    } else {
        throw InvalidType();
    }
}

IPCVariant::IPCVariant(IPCVariantDict dict,
        const TypeInfo& type) : _dict (std::move(dict)), _type (type)
{
    if(type.primaryType() != TypeInfo::Dict)
        throw InvalidType();

    for(auto& it : _dict) {
        if(it.first.type() != _type.dictType().first)
            throw InvalidType();
        if(it.second.type() != _type.dictType().second)
            throw InvalidType();
    }
}

IPCVariant::IPCVariant(const IPCVariant& o) :
    _array (o._array), _dict (o._dict), _num_data(o._num_data),
    _string_data (o._string_data), _type (o._type)
{
}

IPCVariant::IPCVariant(IPCVariant&& o) noexcept : _array
        (std::move(o._array)), _dict (std::move(o._dict)), _num_data
        (std::exchange(o._num_data, 0)),
        _string_data (std::move(o._string_data)), _type (std::move(o._type))
{
}

const IPCVariant::TypeInfo& IPCVariant::type() const
{
    return _type;
}

bool IPCVariant::operator==(const IPCVariant& other) const
{
    if(_type != other.type())
        return false;

    switch(_type.primaryType())
    {
    case TypeInfo::Int16:
        return (int16_t)(*this) == (int16_t)other;
    case TypeInfo::UInt16:
        return (uint16_t)(*this) == (uint16_t)other;
    case TypeInfo::Int32:
        return (int32_t)(*this) == (int32_t)other;
    case TypeInfo::UInt32:
        return (uint32_t)(*this) == (uint32_t)other;
    case TypeInfo::Int64:
        return (int64_t)(*this) == (int64_t)other;
    case TypeInfo::UInt64:
        return (uint64_t)(*this) == (uint64_t)other;
    case TypeInfo::Double:
        return (double)(*this) == (double)other;
    case TypeInfo::Byte:
        return (uint8_t)(*this) == (uint8_t)other;
    case TypeInfo::String:
    case TypeInfo::Signature:
    case TypeInfo::ObjectPath:
        return (std::string)(*this) == (std::string)other;
    case TypeInfo::Boolean:
        return (bool)(*this) == (bool)other;
    case TypeInfo::Array:
    case TypeInfo::Struct:
        return (const std::vector<IPCVariant>&)(*this) ==
            (const std::vector<IPCVariant>&)other;
    case TypeInfo::Dict:
        return (const IPCVariantDict&)(*this) ==
               (const IPCVariantDict&)other;
    case TypeInfo::None:
        return true;
    }

    return (*this);
}

IPCVariant& IPCVariant::operator=(const IPCVariant& o)
{
    _type = o._type;
    _num_data = o._num_data;
    _string_data = o._string_data;
    _array = o._array;
    _dict = o._dict;

    return (*this);
}

IPCVariant::InvalidType::InvalidType() : _what ("Invalid type")
{
}

IPCVariant::InvalidType::InvalidType(std::string type_signature) :
    _what ("Invalid type " + type_signature)
{
}

const char * IPCVariant::InvalidType::what() const noexcept
{
    return _what.c_str();
}

IPCVariant::TypeInfo::TypeInfo() : _type (None),
    _type_signature (1, None)
{
}

IPCVariant::TypeInfo::TypeInfo(std::string type_signature) :
    _type_signature (type_signature)
{
    if(type_signature.empty())
        throw InvalidType();

    // Check to ensure all chars are valid
    for(char c : type_signature)
        if(VALID_CHARS.find(c) == std::string::npos)
            throw InvalidType(type_signature);

    if(SINGLE_TYPES.find(type_signature[0]) != std::string::npos) {
        if(type_signature.size() != 1)
            throw InvalidType(type_signature);
        _type = static_cast<Type>(type_signature[0]);
        return;
    }

    if(SPECIAL_TYPES.find(type_signature[0]) == std::string::npos)
        throw InvalidType(type_signature);

    if(type_signature.size() == 1)
        throw InvalidType(type_signature);

    _type = static_cast<Type>(type_signature[0]);

    // Special types

    if(_type == Array) {
        _array_type = std::make_shared<TypeInfo>(type_signature.substr(1));
    } else if(_type == Struct) {
        if(type_signature.back() != ')')
            throw InvalidType(type_signature);

        for(std::size_t i = 1; i < type_signature.size()-1; i++) {
            if(SINGLE_TYPES.find(type_signature[i]) != std::string::npos) {
                _struct_types.emplace_back(type_signature[i]);
            } else if(SPECIAL_TYPES.find(type_signature[i]) !=
                std::string::npos) {
                auto end = getSpecialEnd(type_signature, i);
                if(end >= type_signature.size()-1)
                    throw InvalidType(type_signature);
                _struct_types.emplace_back(type_signature.substr(i, end-i+1));
                i = end; // Fast-forward to last character
            } else
                // Stray character
                throw InvalidType(type_signature);
        }
    } else if(_type == Dict) {
        if(type_signature.back() != '}')
            throw InvalidType(type_signature);

        std::size_t value_start = 1;
        if(SINGLE_TYPES.find(type_signature[1]) != std::string::npos) {
            _dict_key = std::make_shared<TypeInfo>(type_signature.substr(1, 1));
            value_start = 2;
        } else if(SPECIAL_TYPES.find(type_signature[1]) != std::string::npos) {
            auto key_end = getSpecialEnd(type_signature, 1);
            if(key_end >= type_signature.size()-1)
                throw InvalidType(type_signature);
            _dict_key = std::make_shared<TypeInfo>(type_signature.substr(1,
                    key_end)); // key_end - 1 + 1 = key_end
            value_start = key_end + 1;
        } else {
            throw InvalidType(type_signature);
        }
        if(value_start >= type_signature.size())
            throw InvalidType(type_signature);

        if(SINGLE_TYPES.find(type_signature[value_start]) !=
            std::string::npos) {
            _dict_value = std::make_shared<TypeInfo>(type_signature.substr
                    (value_start, 1));
        } else if(SPECIAL_TYPES.find(type_signature[value_start]) !=
            std::string::npos) {
            auto value_end = getSpecialEnd(type_signature, value_start);
            if(value_end != type_signature.size()-2)
                throw InvalidType(type_signature);
            _dict_value = std::make_shared<TypeInfo>(type_signature.substr
                    (value_start, value_end-value_start+1));
        } else {
            throw InvalidType(type_signature);
        }
    }
}

IPCVariant::TypeInfo::TypeInfo(char type_signature) :
    _type_signature (1, type_signature)
{
    if(SINGLE_TYPES.find(type_signature) != std::string::npos)
        _type = static_cast<TypeInfo::Type>(type_signature);
    else
        throw InvalidType(_type_signature);
}

IPCVariant::TypeInfo::TypeInfo(const TypeInfo& o) :
    _type (o._type), _type_signature (o._type_signature),
    _struct_types (o._struct_types), _array_type (o._array_type),
    _dict_key (o._dict_key), _dict_value (o._dict_value)
{
}

IPCVariant::TypeInfo::TypeInfo(TypeInfo&& o) :
    _type (std::exchange(o._type,None)),
    _type_signature (std::move(o._type_signature)),
    _struct_types (std::move(o._struct_types)),
    _array_type (std::move(o._array_type)),
    _dict_key (std::move(o._dict_key)),
    _dict_value (std::move(o._dict_value))
{
}

IPCVariant::TypeInfo::TypeInfo(Type t) : _type (t), _type_signature (1, t)
{
    if(t == None || t == Array || t == Struct || t == Dict)
        throw InvalidType();
}

IPCVariant& IPCVariant::operator=(const std::vector<IPCVariant>& other)
{
    if(_type.primaryType() == TypeInfo::Array) {
        const auto& array_type = _type.arrayType();
        for(const auto& i : other) {
            if(i.type() != array_type)
                throw InvalidType();
        }
    } else if(_type.primaryType() == TypeInfo::Struct) {
        const auto& struct_format = _type.structFormat();
        if(other.size() != struct_format.size())
            throw InvalidType();
        for(std::size_t i = 0; i < other.size(); i++) {
            if(other[i].type() != struct_format[i])
                throw InvalidType();
        }
    } else {
        throw InvalidType();
    }

    _array = other;
    return *this;
}

IPCVariant& IPCVariant::operator=(const IPCVariantDict& other)
{
    if(_type.primaryType() != TypeInfo::Dict)
        throw InvalidType();

    auto dict_format = _type.dictType();
    for(const auto & i : other) {
        if(i.first.type() != dict_format.first ||
            i.second.type() != dict_format.second)
            throw InvalidType();
    }

    _dict = other;
    return *this;
}

IPCVariant::TypeInfo& IPCVariant::TypeInfo::operator=(const TypeInfo &other)
{
    _type = other._type;
    _type_signature = other._type_signature;
    _array_type = other._array_type;
    _dict_key = other._dict_key;
    _dict_value = other._dict_value;
    _struct_types = other._struct_types;

    return (*this);
}

std::size_t IPCVariant::TypeInfo::getSpecialEnd(
        const std::string &type_signature, std::size_t start)
{
    assert(start < type_signature.size()-1);
    assert(SPECIAL_TYPES.find(type_signature[start]) != std::string::npos);

    if(type_signature[start] == Array) {
        if(SINGLE_TYPES.find(type_signature[start+1]) != std::string::npos) {
            return start + 1;
        } else if(SPECIAL_TYPES.find(type_signature[start+1]) !=
            std::string::npos) {
            if(start >= type_signature.size()-2)
                throw InvalidType(type_signature);

            return getSpecialEnd(type_signature, start+1);
        } else {
            throw InvalidType(type_signature);
        }
    } else {
        char c = type_signature[start] == '{' ? '}' : ')';
        int end_count = 1;
        std::size_t end;
        for(end = start + 1; end < type_signature.size(); end++) {
            if(type_signature[end] == type_signature[start])
                end_count++;
            else if(type_signature[end] == c)
                end_count--;
            if(!end_count)
                break;
        }
        if(end_count)
            throw InvalidType(type_signature);
        else
            return end;
    }
}

IPCVariant::TypeInfo::Type IPCVariant::TypeInfo::primaryType() const
{
    return _type;
}

const std::string& IPCVariant::TypeInfo::typeSignature() const
{
    return _type_signature;
}

bool IPCVariant::TypeInfo::operator==(const TypeInfo& other) const
{
    return other.typeSignature() == _type_signature;
}

bool IPCVariant::TypeInfo::operator!=(const TypeInfo& other) const
{
    return other.typeSignature() != _type_signature;
}

const IPCVariant::TypeInfo& IPCVariant::TypeInfo::arrayType() const
{
    if(_type != Array)
        throw InvalidType();
    return *_array_type;
}

const std::vector<IPCVariant::TypeInfo>& IPCVariant::TypeInfo::structFormat()
    const
{
    if(_type != Struct)
        throw InvalidType();
    return _struct_types;
}

std::pair<const IPCVariant::TypeInfo&, const IPCVariant::TypeInfo&>
    IPCVariant::TypeInfo::dictType() const
{
    if(_type != Dict)
        throw InvalidType();
    return {*_dict_key, *_dict_value};
}

const IPCVariant& IPCVariant::operator[](const ipc::IPCVariant& key) const
{
    if(_type.primaryType() != TypeInfo::Dict)
        throw InvalidType();

    return _dict.find(key)->second;
}

const IPCVariant& IPCVariant::operator[](std::size_t index) const
{
    if(_type.primaryType() != TypeInfo::Array &&
       _type.primaryType() != TypeInfo::Struct)
        throw InvalidType();

    assert(index >= 0 && index < _array.size());
    return _array[index];
}

IPCVariant& IPCVariant::operator[](ipc::IPCVariant& key)
{
    if(_type.primaryType() != TypeInfo::Dict)
        throw InvalidType();

    return _dict[key];
}

IPCVariant& IPCVariant::operator[](std::size_t index)
{
    if(_type.primaryType() != TypeInfo::Array &&
        _type.primaryType() != TypeInfo::Struct)
        throw InvalidType();

    assert(index < _array.size());
    return _array[index];
}