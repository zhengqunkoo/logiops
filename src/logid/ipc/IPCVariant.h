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
#ifndef LOGID_IPC_VARIANT_H
#define LOGID_IPC_VARIANT_H

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace logid {
namespace ipc
{
    class IPCVariant;
    typedef std::map<IPCVariant, IPCVariant, std::equal_to<>> IPCVariantDict;

    class IPCVariant
    {
    public:
        class InvalidType : public std::exception
        {
        public:
            InvalidType();
            explicit InvalidType(std::string type_signature);

            virtual const char* what() const noexcept;
        private:
            std::string _what;
        };
        class TypeInfo
        {
        public:
            enum Type : char
            {
                Int16 = 'n',
                UInt16 = 'q',
                Int32 = 'i',
                UInt32 = 'u',
                Int64 = 'x',
                UInt64 = 't',
                Double = 'd',
                Byte = 'y',
                String = 's',
                Signature = 'g',
                ObjectPath = 'o',
                Boolean = 'b',
                Array = 'a',
                Struct = '(',
                Dict = '{',
                None = '0'
            };

            TypeInfo();
            explicit TypeInfo(std::string type_signature);
            explicit TypeInfo(char type_signature);

            TypeInfo(const TypeInfo& type_info);
            TypeInfo(TypeInfo&& o);
            explicit TypeInfo(Type t);

            TypeInfo& operator=(const TypeInfo& other);

            static std::size_t getSpecialEnd(const std::string& type_signature,
                    std::size_t start);

            Type primaryType() const;

            const TypeInfo& arrayType() const;
            const std::vector<TypeInfo>& structFormat() const;
            std::pair<const TypeInfo&, const TypeInfo&> dictType() const;

            const std::string& typeSignature() const;

            bool operator==(const TypeInfo& other) const;
            bool operator!=(const TypeInfo& other) const;
        private:
            Type _type;
            std::string _type_signature;
            std::vector<TypeInfo> _struct_types;
            std::shared_ptr<TypeInfo> _array_type;
            std::shared_ptr<TypeInfo> _dict_key;
            std::shared_ptr<TypeInfo> _dict_value;
        };

    private:
        static const std::string SINGLE_TYPES;
        static const std::string SPECIAL_TYPES;
        static const std::string ALL_TYPES;
        static const std::string VALID_CHARS;

        std::vector<IPCVariant> _array; // May also be used for structs
        IPCVariantDict _dict;
        uint64_t _num_data;
        std::string _string_data;

        TypeInfo _type;

    public:
        #define NUM_DATA(x, t) \
        explicit IPCVariant(x data) : _num_data (data), _type (TypeInfo::t) \
        { \
        }
            NUM_DATA(int16_t, Int16)
            NUM_DATA(uint16_t, UInt16)
            NUM_DATA(int32_t, Int16)
            NUM_DATA(uint32_t, UInt32)
            NUM_DATA(int64_t, Int64)
            NUM_DATA(uint64_t, UInt64)
            NUM_DATA(double, Double)
            NUM_DATA(uint8_t, Byte)
            NUM_DATA(bool, Boolean)
        #undef NUM_DATA

        IPCVariant();

        explicit IPCVariant(const char* data,
                TypeInfo::Type type=TypeInfo::String);
        explicit IPCVariant(std::string data,
                TypeInfo::Type type=TypeInfo::String);

        IPCVariant(std::vector<IPCVariant> array,
                const TypeInfo& type);
        IPCVariant(IPCVariantDict dict,
                const TypeInfo& type);
        IPCVariant(const IPCVariant& ipc_variant);
        IPCVariant(IPCVariant&& ipc_variant) noexcept;

        const TypeInfo& type() const;

        #define TYPE_OVERLOAD(x, c, v) \
        IPCVariant& operator=(const x& other) \
        { \
            if(c) \
                throw InvalidType(); \
            v = other; \
            return *this; \
        } \
        \
        bool operator==(const x& other) const \
        { \
            if(c) \
                return false; \
            return (x)v == other; \
        } \
        bool operator!=(const x& other) const \
        { \
            if(c) \
                return true; \
            return (x)v != other; \
        } \
        operator x() const \
        { \
            if(c) \
                throw InvalidType(); \
            return (x)v; \
        }
        #define NUM_TYPE_OVERLOAD(x, t) TYPE_OVERLOAD(x, \
            _type.primaryType() != TypeInfo::t, _num_data)
            NUM_TYPE_OVERLOAD(int16_t, Int16)
            NUM_TYPE_OVERLOAD(uint16_t, UInt16)
        IPCVariant &operator=(const int32_t &other)
        {
            if (_type.primaryType() != TypeInfo::Int32)throw InvalidType();
            _num_data = other;
            return *this;
        }
        bool operator==(const int32_t &other) const
        {
            if (_type.primaryType() != TypeInfo::Int32)return false;
            return (int32_t) _num_data == other;
        }
        bool operator!=(const int32_t &other) const
        {
            if (_type.primaryType() != TypeInfo::Int32)return true;
            return (int32_t) _num_data != other;
        }
        operator int32_t() const
        {
            if (_type.primaryType() != TypeInfo::Int32)throw InvalidType();
            return (int32_t) _num_data;
        }
            NUM_TYPE_OVERLOAD(uint32_t, UInt32)
            NUM_TYPE_OVERLOAD(int64_t, Int64)
            NUM_TYPE_OVERLOAD(uint64_t, UInt64)
            NUM_TYPE_OVERLOAD(double, Double)
            NUM_TYPE_OVERLOAD(uint8_t, Byte)
            //NUM_TYPE_OVERLOAD(bool, Boolean)
            IPCVariant &operator=(const bool &other)
            {
                if (_type.primaryType() != TypeInfo::Boolean)throw InvalidType();
                _num_data = other;
                return *this;
            }
            bool operator==(const bool &other) const
            {
                if (_type.primaryType() != TypeInfo::Boolean)return false;
                return (bool) _num_data == other;
            }
            bool operator!=(const bool &other) const
            {
                if (_type.primaryType() != TypeInfo::Boolean)
                    return true;
                return (bool) _num_data != other;
            }
            operator bool() const
            {
                if (_type.primaryType() != TypeInfo::Boolean)
                    return false;
                return (bool) _num_data;
            }

            TYPE_OVERLOAD(std::string, _type.primaryType() !=
            TypeInfo::String && _type.primaryType() != TypeInfo::Signature &&
                _type.primaryType() != TypeInfo::ObjectPath, _string_data)

        #undef NUM_TYPE_OVERLOAD
        #undef TYPE_OVERLOAD

        explicit operator const char*() const
        {
            if (_type.primaryType() != TypeInfo::String &&
                _type.primaryType() != TypeInfo::Signature &&
                _type.primaryType() != TypeInfo::ObjectPath)
                throw InvalidType();
            return _string_data.c_str();
        }

        IPCVariant &operator=(const std::vector<IPCVariant>& other);
        bool operator==(const std::vector<IPCVariant>& other) const
        {
            if(_type.primaryType() != TypeInfo::Array &&
               _type.primaryType() != TypeInfo::Struct)
                return false;
            return (const std::vector<IPCVariant>&)_array == other;
        }
        bool operator!=(const std::vector<IPCVariant>& other) const
        {
            if(_type.primaryType() != TypeInfo::Array &&
               _type.primaryType() != TypeInfo::Struct)
                return true;
            return (const std::vector<IPCVariant>&)_array != other;
        }
        explicit operator const std::vector<IPCVariant>&() const
        {
            if(_type.primaryType() != TypeInfo::Dict)
                throw InvalidType();
            return _array;
        }

        IPCVariant& operator=(const IPCVariantDict& other);

        bool operator==(const IPCVariantDict& other) const
        {
            if(_type.primaryType() != TypeInfo::Dict)
                return false;
            return (IPCVariantDict)_dict == other;
        }
        bool operator!=(const IPCVariantDict& other) const
        {
            if(_type.primaryType() != TypeInfo::Dict)
                return true;
            return (IPCVariantDict)_dict != other;
        }
        explicit operator IPCVariantDict() const
        {
            if(_type.primaryType() != TypeInfo::Dict)
                throw InvalidType();
            return _dict;
        }

        bool operator==(const IPCVariant& other) const;
        bool operator!=(const IPCVariant& other) const
        {
            return !(*this == other);
        }
        IPCVariant& operator=(const IPCVariant& other);
    };
}}

#endif //LOGID_IPC_VARIANT_H
