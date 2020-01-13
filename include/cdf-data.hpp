#pragma once
/*------------------------------------------------------------------------------
-- This file is a part of the CDFpp library
-- Copyright (C) 2019, Plasma Physics Laboratory - CNRS
--
-- This program is free software; you can redistribute it and/or modify
-- it under the terms of the GNU General Public License as published by
-- the Free Software Foundation; either version 2 of the License, or
-- (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
-- GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with this program; if not, write to the Free Software
-- Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
-------------------------------------------------------------------------------*/
/*-- Author : Alexis Jeandet
-- Mail : alexis.jeandet@member.fsf.org
----------------------------------------------------------------------------*/
#include "cdf-endianness.hpp"
#include "cdf-enums.hpp"
#include <algorithm>
#include <cstdint>
#include <variant>
#include <vector>

namespace cdf
{

namespace
{
    template <typename... Ts>
    struct Visitor : Ts...
    {
        Visitor(const Ts&... args) : Ts(args)... {}

        using Ts::operator()...;
    };

    template <typename... Ts>
    auto make_visitor(Ts... lambdas)
    {
        return Visitor<Ts...>(lambdas...);
    }
}

struct cdf_none
{
    bool operator==([[maybe_unused]] const cdf_none& other) const { return true; }
};

using cdf_values_t = std::variant<cdf_none, std::vector<char>, std::vector<uint8_t>,
    std::vector<uint16_t>, std::vector<uint32_t>, std::vector<int8_t>, std::vector<int16_t>,
    std::vector<int32_t>, std::vector<int64_t>, std::vector<float>, std::vector<double>,
    std::vector<tt2000_t>, std::vector<epoch>, std::vector<epoch16>, std::string>;


template <CDF_Types type, typename endianness_t>
auto load_values(const char* buffer, std::size_t buffer_size)
{
    if constexpr (type == CDF_Types::CDF_CHAR) // special case for strings
    {
        std::string result(buffer_size, '\0');
        std::copy_n(buffer, buffer_size, result.data());
        return result;
    }
    else if constexpr (type == CDF_Types::CDF_EPOCH16) // special case for epoch
    {
        std::size_t size = buffer_size / sizeof(from_cdf_type_t<type>);
        std::vector<from_cdf_type_t<type>> result(size);
        endianness::decode_v<endianness_t>(
            buffer, buffer_size * 2, reinterpret_cast<double*>(result.data()));
        return cdf_values_t { std::move(result) };
    }
    else
    {
        std::size_t size = buffer_size / sizeof(from_cdf_type_t<type>);
        std::vector<from_cdf_type_t<type>> result(size);
        endianness::decode_v<endianness_t>(buffer, buffer_size, result.data());
        return cdf_values_t { std::move(result) };
    }
}

struct data_t
{

    template <CDF_Types type>
    decltype(auto) get()
    {
        return std::get<std::vector<from_cdf_type_t<type>>>(p_values);
    }

    template <CDF_Types type>
    decltype(auto) get() const
    {
        return std::get<std::vector<from_cdf_type_t<type>>>(p_values);
    }

    template <typename type>
    decltype(auto) get()
    {
        if constexpr (std::is_same_v<type, std::string>)
        {
            return std::get<type>(p_values);
        }
        else
        {
            return std::get<std::vector<type>>(p_values);
        }
    }

    template <typename type>
    decltype(auto) get() const
    {
        if constexpr (std::is_same_v<type, std::string>)
        {
            return std::get<type>(p_values);
        }
        else
        {
            // might use optional later
            // if (std::holds_alternative<std::vector<type>>(p_values))
            //{
            return std::get<std::vector<type>>(p_values);
            //}
        }
    }


    data_t() : p_values { cdf_none {} }, p_type { CDF_Types::CDF_NONE } {}
    data_t(const data_t& other) = default;
    data_t(data_t&& other) = default;
    data_t& operator=(data_t&& other)
    {
        std::swap(p_values, other.p_values);
        std::swap(p_type, other.p_type);
        return *this;
    }
    data_t& operator=(const data_t& other)
    {
        p_values = other.p_values;
        p_type = other.p_type;
        return *this;
    }
    template <typename T, typename Dummy = void,
        typename = std::enable_if_t<!std::is_same_v<std::remove_reference_t<T>, data_t>, Dummy>>
    data_t(T&& values)
            : p_values { std::forward<T>(values) }
            , p_type { to_cdf_type<typename std::remove_reference_t<T>::value_type>() }
    {
    }

    data_t(const cdf_values_t& values, CDF_Types type) : p_values { values }, p_type { type } {}
    data_t(cdf_values_t&& values, CDF_Types type) : p_values { values }, p_type { type } {}

    CDF_Types type() { return p_type; }

    template <typename... Ts>
    friend auto visit(data_t& data, Ts... lambdas);

private:
    cdf_values_t p_values;
    CDF_Types p_type;
};

template <typename... Ts>
auto visit(data_t& data, Ts... lambdas)
{
    return std::visit(make_visitor(lambdas...), data.p_values);
}

data_t load_values(
    const char* buffer, std::size_t buffer_size, CDF_Types type, cdf_encoding encoding)
{
#define DATA_FROM_T(type)                                                                          \
    case CDF_Types::type:                                                                          \
        if (endianness::is_big_endian_encoding(encoding))                                          \
            return data_t { load_values<CDF_Types::type, endianness::big_endian_t>(                \
                                buffer, buffer_size),                                              \
                CDF_Types::type };                                                                 \
        return data_t { load_values<CDF_Types::type, endianness::little_endian_t>(                 \
                            buffer, buffer_size),                                                  \
            CDF_Types::type };

    switch (type)
    {
        DATA_FROM_T(CDF_FLOAT)
        DATA_FROM_T(CDF_DOUBLE)
        DATA_FROM_T(CDF_REAL4)
        DATA_FROM_T(CDF_REAL8)
        DATA_FROM_T(CDF_EPOCH)
        DATA_FROM_T(CDF_EPOCH16)
        DATA_FROM_T(CDF_TIME_TT2000)
        DATA_FROM_T(CDF_CHAR)
        DATA_FROM_T(CDF_UCHAR)
        DATA_FROM_T(CDF_INT1)
        DATA_FROM_T(CDF_INT2)
        DATA_FROM_T(CDF_INT4)
        DATA_FROM_T(CDF_INT8)
        DATA_FROM_T(CDF_UINT1)
        DATA_FROM_T(CDF_UINT2)
        DATA_FROM_T(CDF_UINT4)
        DATA_FROM_T(CDF_BYTE)
        case CDF_Types::CDF_NONE:
            return {};
    }
    return {};
}

}
