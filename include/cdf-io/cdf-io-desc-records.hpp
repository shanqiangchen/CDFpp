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
#include "../cdf-endianness.hpp"
#include "../cdf-enums.hpp"
#include <cstdint>
#include <tuple>
#include <type_traits>

namespace cdf
{
using magic_numbers_t = std::pair<uint32_t, uint32_t>;

template <std::size_t offset_param, typename T>
struct field_t
{
    using type = T;
    inline static constexpr std::size_t len = sizeof(T);
    inline static constexpr std::size_t offset = offset_param;
    T value;
    operator T() { return value; }
    field_t& operator=(const T& v)
    {
        this->value = v;
        return *this;
    }
};

template <std::size_t offset_param, std::size_t len_param>
struct str_field_t
{
    using type = std::string;
    inline static constexpr std::size_t offset = offset_param;
    inline static constexpr std::size_t len = len_param;
    std::string value;
    operator std::string() { return value; }
    str_field_t& operator=(const std::string& v)
    {
        this->value = v;
        return *this;
    }
};

template <std::size_t offset_param, typename T>
struct unused_field_t
{
    using type = T;
    inline static constexpr std::size_t len = sizeof(T);
    inline static constexpr std::size_t offset = offset_param;
    inline static constexpr T value = 0;
    operator T() { return value; }
};

template <typename buffer_t, typename T>
void extract_field(buffer_t buffer, std::size_t offset, T& field)
{
    if constexpr (std::is_same_v<std::string, typename T::type>)
    {
        std::size_t size = 0;
        for (; size < T::len; size++)
        {
            if (buffer[T::offset - offset + size] == '\0')
                break;
        }
        field = std::string { buffer + T::offset - offset, size };
    }
    else
        field = endianness::decode<typename T::type>(buffer + T::offset - offset);
}

template <typename buffer_t, typename... Ts>
void extract_fields(buffer_t buffer, std::size_t offset, Ts&&... fields)
{
    (extract_field(buffer, offset, std::forward<Ts>(fields)), ...);
}

template <typename field_type>
inline constexpr std::size_t after = field_type::offset + sizeof(typename field_type::type);


#define AFTER(f) after<decltype(f)>

struct v3x_tag
{
};

struct v2x_tag
{
};

template <typename version_t>
inline constexpr bool is_v3_v = std::is_same_v<version_t, v3x_tag>;

template <typename version_t, std::size_t offset_param>
using cdf_offset_field_t = std::conditional_t<is_v3_v<version_t>, field_t<offset_param, uint64_t>,
    field_t<offset_param, uint32_t>>;

template <typename version_t, std::size_t offset_param, std::size_t v3size, std::size_t v2size>
using cdf_string_field_t = std::conditional_t<is_v3_v<version_t>, str_field_t<offset_param, v3size>,
    str_field_t<offset_param, v2size>>;

template <typename version_t, cdf_record_type... record_t>
struct cdf_DR_header
{
    inline static constexpr bool v3 = is_v3_v<version_t>;
    inline static constexpr std::size_t offset = 0;
    using type = uint64_t;
    std::conditional_t<v3, field_t<0, uint64_t>, field_t<0, uint32_t>> record_size;
    field_t<AFTER(record_size), cdf_record_type> record_type;
    template <typename buffert_t>
    bool load(buffert_t&& buffer)
    {
        extract_fields(std::forward<buffert_t>(buffer), 0, record_size, record_type);
        return ((record_type == record_t) || ...);
    }
};

template <template <typename, typename> typename comp_t, typename... Ts>
struct most_member_t;

template <template <typename, typename> typename comp_t, typename T1>
struct most_member_t<comp_t, T1> : std::remove_reference_t<T1>
{
};

template <template <typename, typename> typename comp_t, typename T1, typename T2>
struct most_member_t<comp_t, T1, T2>
        : std::conditional_t<
              comp_t<std::remove_reference_t<T1>, std::remove_reference_t<T2>>::value,
              std::remove_reference_t<T1>, std::remove_reference_t<T2>>
{
};

template <template <typename, typename> typename comp_t, typename T1, typename T2, typename... Ts>
struct most_member_t<comp_t, T1, T2, Ts...>
        : std::conditional_t<
              comp_t<std::remove_reference_t<T1>, std::remove_reference_t<T2>>::value,
              most_member_t<comp_t, T1, Ts...>, most_member_t<comp_t, T2, Ts...>>
{
};


template <typename T1, typename T2>
using field_is_before_t
    = std::conditional_t<T1::offset <= T2::offset, std::true_type, std::false_type>;

template <typename T1, typename T2>
using field_is_after_t
    = std::conditional_t<T1::offset >= T2::offset, std::true_type, std::false_type>;

template <typename... Args>
using first_field_t = most_member_t<field_is_before_t, Args...>;

template <typename... Args>
using last_field_t = most_member_t<field_is_after_t, Args...>;

template <typename T, typename streamT>
T read_buffer(streamT&& stream, std::size_t pos, std::size_t size)
{
    T buffer(size);
    stream.seekg(pos);
    stream.read(buffer.data(), size);
    return buffer;
}

template <typename streamT, typename cdf_DR_t, typename... Ts>
constexpr bool load_desc_record(
    streamT&& stream, std::size_t offset, cdf_DR_t&& cdf_desc_record, Ts&&... fields)
{
    using last_member_t = last_field_t<Ts...>;
    constexpr std::size_t buffer_len = last_member_t::offset + last_member_t::len;
    char buffer[buffer_len];
    stream.seekg(offset);
    stream.read(buffer, buffer_len);
    if (cdf_desc_record.header.load(buffer))
    {
        extract_fields(buffer, 0, fields...);
        return true;
    }
    return false;
}

template <typename streamT, typename... Ts>
constexpr bool load_fields(streamT&& stream, std::size_t offset, Ts&&... fields)
{
    using last_member_t = last_field_t<Ts...>;
    using first_member_t = first_field_t<Ts...>;
    constexpr std::size_t buffer_len
        = last_member_t::offset + last_member_t::len - first_member_t::offset;
    char buffer[buffer_len];
    std::size_t pos = offset + first_member_t::offset;
    stream.seekg(pos);
    stream.read(buffer, buffer_len);
    extract_fields(buffer, first_member_t::offset, fields...);
    return true;
}

template <typename version_t>
struct cdf_CDR_t
{
    inline static constexpr bool v3 = is_v3_v<version_t>;
    cdf_DR_header<version_t, cdf_record_type::CDR> header;
    cdf_offset_field_t<version_t, AFTER(header)> GDRoffset;
    field_t<AFTER(GDRoffset), uint32_t> Version;
    field_t<AFTER(Version), uint32_t> Release;
    field_t<AFTER(Release), cdf_encoding> Encoding;
    field_t<AFTER(Encoding), uint32_t> Flags;
    unused_field_t<AFTER(Flags), uint32_t> rfuA;
    unused_field_t<AFTER(rfuA), uint32_t> rfuB;
    field_t<AFTER(rfuB), uint32_t> Increment;
    field_t<AFTER(Increment), uint32_t> Identifier;
    unused_field_t<AFTER(Identifier), uint32_t> rfuE;
    str_field_t<AFTER(rfuE), 256> copyright; // ignore format < 2.6

    template <typename streamT>
    bool load(streamT&& stream)
    {
        return load_desc_record(stream, 8, *this, GDRoffset, Version, Release, Encoding, Flags,
            Increment, Identifier, copyright);
    }
};

template <typename version_t>
struct cdf_GDR_t
{
    inline static constexpr bool v3 = is_v3_v<version_t>;
    cdf_DR_header<version_t, cdf_record_type::GDR> header;
    cdf_offset_field_t<version_t, AFTER(header)> rVDRhead;
    cdf_offset_field_t<version_t, AFTER(rVDRhead)> zVDRhead;
    cdf_offset_field_t<version_t, AFTER(zVDRhead)> ADRhead;
    cdf_offset_field_t<version_t, AFTER(ADRhead)> eof;
    field_t<AFTER(eof), uint32_t> NrVars;
    field_t<AFTER(NrVars), uint32_t> NumAttr;
    field_t<AFTER(NumAttr), uint32_t> rMaxRec;
    field_t<AFTER(rMaxRec), uint32_t> rNumDims;
    field_t<AFTER(rNumDims), uint32_t> NzVars;
    field_t<AFTER(NzVars), uint32_t> UIRhead;
    unused_field_t<AFTER(UIRhead), uint32_t> rfuC;
    field_t<AFTER(rfuC), uint32_t> LeapSecondLastUpdated;
    unused_field_t<AFTER(LeapSecondLastUpdated), uint32_t> rfuE;
    template <typename streamT>
    bool load(streamT&& stream, std::size_t GDRoffset)
    {
        return load_desc_record(stream, GDRoffset, *this, rVDRhead, zVDRhead, ADRhead, eof, NrVars,
            NumAttr, rMaxRec, rNumDims, NzVars, UIRhead, LeapSecondLastUpdated);
    }
};

template <typename version_t>
struct cdf_ADR_t
{
    inline static constexpr bool v3 = is_v3_v<version_t>;
    cdf_DR_header<version_t, cdf_record_type::ADR> header;
    cdf_offset_field_t<version_t, AFTER(header)> ADRnext;
    cdf_offset_field_t<version_t, AFTER(ADRnext)> AgrEDRhead;
    field_t<AFTER(AgrEDRhead), cdf_attr_scope> scope;
    field_t<AFTER(scope), uint32_t> num;
    field_t<AFTER(num), uint32_t> NgrEntries;
    field_t<AFTER(NgrEntries), uint32_t> MAXgrEntries;
    unused_field_t<AFTER(MAXgrEntries), uint32_t> rfuA;
    cdf_offset_field_t<version_t, AFTER(rfuA)> AzEDRhead;
    field_t<AFTER(AzEDRhead), uint32_t> NzEntries;
    field_t<AFTER(NzEntries), uint32_t> MAXzEntries;
    unused_field_t<AFTER(MAXzEntries), uint32_t> rfuE;
    cdf_string_field_t<version_t, AFTER(rfuE), 256, 64> Name;

    template <typename streamT>
    bool load(streamT&& stream, std::size_t ADRoffset)
    {
        return load_desc_record(stream, ADRoffset, *this, ADRnext, AgrEDRhead, scope, num,
            NgrEntries, MAXgrEntries, AzEDRhead, NzEntries, MAXzEntries, Name);
    }
};

template <typename version_t>
struct cdf_AEDR_t
{
    inline static constexpr bool v3 = is_v3_v<version_t>;
    cdf_DR_header<version_t, cdf_record_type::AzEDR, cdf_record_type::AgrEDR> header;
    cdf_offset_field_t<version_t, AFTER(header)> AEDRnext;
    field_t<AFTER(AEDRnext), uint32_t> AttrNum;
    field_t<AFTER(AttrNum), CDF_Types> DataType;
    field_t<AFTER(DataType), uint32_t> Num;
    field_t<AFTER(Num), uint32_t> NumElements;
    field_t<AFTER(NumElements), uint32_t> NumStrings;
    unused_field_t<AFTER(NumStrings), uint32_t> rfB;
    unused_field_t<AFTER(rfB), uint32_t> rfC;
    unused_field_t<AFTER(rfC), uint32_t> rfD;
    unused_field_t<AFTER(rfD), uint32_t> rfE;
    field_t<AFTER(rfE), uint32_t> Values;

    template <typename streamT>
    bool load(streamT&& stream, std::size_t AEDRoffset)
    {
        return load_desc_record(
            stream, AEDRoffset, *this, AEDRnext, AttrNum, DataType, Num, NumElements, NumStrings);
    }
};

template <typename version_t>
struct cdf_VDR_t
{
    inline static constexpr bool v3 = is_v3_v<version_t>;
    cdf_DR_header<version_t, cdf_record_type::rVDR, cdf_record_type::zVDR> header;
    cdf_offset_field_t<version_t, AFTER(header)> VDRnext;
    field_t<AFTER(VDRnext), CDF_Types> DataType;
    field_t<AFTER(DataType), uint32_t> MaxRec;
    cdf_offset_field_t<version_t, AFTER(MaxRec)> VXRhead;
    cdf_offset_field_t<version_t, AFTER(VXRhead)> VXRtail;
    field_t<AFTER(VXRtail), uint32_t> Flags;
    field_t<AFTER(Flags), uint32_t> SRecords;
    field_t<AFTER(SRecords), uint32_t> rfuB;
    field_t<AFTER(rfuB), uint32_t> rfuC;
    field_t<AFTER(rfuC), uint32_t> rfuF;
    field_t<AFTER(rfuF), uint32_t> NumElems;
    field_t<AFTER(NumElems), uint32_t> Num;
    cdf_offset_field_t<version_t, AFTER(Num)> CPRorSPRoffset;
    field_t<AFTER(CPRorSPRoffset), uint32_t> BlockingFactor;
    cdf_string_field_t<version_t, AFTER(BlockingFactor), 256, 64> Name;
    field_t<AFTER(Name), uint32_t> zNumDims;
    field_t<AFTER(zNumDims), uint32_t> zDimSizes;
    field_t<AFTER(zDimSizes), uint32_t> DimVarys;
    field_t<AFTER(DimVarys), uint32_t> PadValues;

    template <typename streamT>
    bool load(streamT&& stream, std::size_t VDRoffset)
    {
        return load_desc_record(stream, VDRoffset, *this, VDRnext, DataType, MaxRec, VXRhead,
            VXRtail, Flags, SRecords, NumElems, Num, CPRorSPRoffset, BlockingFactor, Name, zNumDims,
            zDimSizes, DimVarys);
    }
};

template <typename version_t>
struct cdf_VXR_t
{
    inline static constexpr bool v3 = is_v3_v<version_t>;
    cdf_DR_header<version_t, cdf_record_type::VXR> header;
    cdf_offset_field_t<version_t, AFTER(header)> VXRnext;
    field_t<AFTER(VXRnext), CDF_Types> Nentries;
    field_t<AFTER(Nentries), uint32_t> NusedEntries;
    template <typename streamT>
    bool load(streamT&& stream, std::size_t VXRoffset)
    {
        return load_desc_record(stream, VXRoffset, *this, VXRnext, Nentries, NusedEntries);
    }
};

template <typename version_t>
struct cdf_VVR_t
{
    inline static constexpr bool v3 = is_v3_v<version_t>;
    cdf_DR_header<version_t, cdf_record_type::VVR> header;
    template <typename streamT>
    bool load(streamT&& stream, std::size_t VXRoffset)
    {
        return load_desc_record(stream, VXRoffset, *this);
    }
};

}