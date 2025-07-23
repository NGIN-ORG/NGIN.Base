/// @file TypeTraits.hpp
/// @brief Compile-time reflection utilities for extracting type names.
/// @note Requires C++20 for `constexpr` and `std::string_view`.
#pragma once

#include <array>
#include <string_view>
#include <type_traits>

namespace NGIN::Meta
{
    /// <summary>Maximum buffer size for type name extraction.</summary>
    static constexpr std::size_t MAX_NAME_BUFFER = 512;

    /// <summary>Compute the minimum of two values at compile time.</summary>
    /// <param name="a">First value.</param>
    /// <param name="b">Second value.</param>
    /// <returns>The smaller of the two values.</returns>
    constexpr std::size_t ConstexprMin(std::size_t a, std::size_t b) noexcept
    {
        return a < b ? a : b;
    }

    /// <summary>Compute the length of a C-string up to a maximum.</summary>
    /// <param name="str">Pointer to the null-terminated string.</param>
    /// <param name="maxLen">Maximum characters to inspect.</param>
    /// <returns>Number of characters before the null terminator or maxLen.</returns>
    constexpr std::size_t ConstexprStrnlen(const char* str, std::size_t maxLen) noexcept
    {
        std::size_t i = 0;
        while (i < maxLen && str[i] != '\0')
            ++i;
        return i;
    }

    /// <summary>Strip common C++ type tokens from a signature fragment.</summary>
    /// <param name="input">Input view containing the raw type string.</param>
    /// <param name="outBuffer">Output buffer to write the stripped string.</param>
    /// <param name="outBufferSize">Size of the output buffer.</param>
    /// <returns>Number of characters written, excluding the null terminator.</returns>
    constexpr std::size_t RemoveTokens(
            std::string_view input,
            char* outBuffer,
            std::size_t outBufferSize) noexcept
    {
        constexpr std::array<std::string_view, 4> tokens = {"class ", "struct ", "enum ", "union "};
        constexpr std::array<std::size_t, 4> length      = {6, 7, 5, 6};
        std::size_t i = 0, out = 0;
        while (i < input.size() && out + 1 < outBufferSize)
        {
            bool matched = false;
            for (size_t t = 0; t < tokens.size(); ++t)
            {
                if (i + length[t] <= input.size() &&
                    input.compare(i, length[t], tokens[t]) == 0)
                {
                    i += length[t];
                    matched = true;
                    break;
                }
            }
            if (!matched)
                outBuffer[out++] = input[i++];
        }
        if (out < outBufferSize)
            outBuffer[out] = '\0';
        return out;
    }

    /// <summary>Remove trailing spaces or tabs from a buffer.</summary>
    /// <param name="buf">Character buffer to trim in-place.</param>
    /// <param name="len">Current length of the buffer; updated as trimming occurs.</param>
    constexpr void RTrim(char* buf, std::size_t& len) noexcept
    {
        while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t'))
        {
            buf[--len] = '\0';
        }
    }

    /// <summary>Find the last occurrence of top-level "::" outside of template arguments.</summary>
    /// <param name="s">Qualified name string to search.</param>
    /// <returns>Index of the first ':' in the last top-level "::", or npos if not found.</returns>
    constexpr std::size_t FindLastTopLevelDoubleColon(std::string_view s) noexcept
    {
        int depth = 0;
        for (std::size_t i = s.size(); i > 1;)
        {
            --i;
            if (s[i] == '>')
                ++depth;
            else if (s[i] == '<')
                --depth;
            else if (depth == 0 && s[i] == ':' && s[i - 1] == ':')
                return i - 1;
        }
        return std::string_view::npos;
    }

    /// <summary>Helper to build a raw buffer containing the decorated type name.</summary>
    /// <typeparam name="U">Type to reflect.</typeparam>
    template<typename U>
    struct RawTypeNameBuilder
    {
        /// <summary>Construct the raw buffer for the type name at compile time.</summary>
        /// <returns>Array filled with the raw type name and null terminator.</returns>
        static consteval std::array<char, MAX_NAME_BUFFER> BuildRawBuffer() noexcept
        {
            std::array<char, MAX_NAME_BUFFER> buf {};

#if defined(_MSC_VER)
            constexpr std::string_view signature = __FUNCSIG__;
            constexpr std::string_view prefix    = "RawTypeNameBuilder<";
            constexpr std::string_view suffix    = ">::BuildRawBuffer";
#elif defined(__clang__)
            constexpr std::string_view signature = __PRETTY_FUNCTION__;
            constexpr std::string_view prefix    = "[U = ";
            constexpr std::string_view suffix    = "]";
#elif defined(__GNUC__)
            constexpr std::string_view signature = __PRETTY_FUNCTION__;
            constexpr std::string_view prefix    = "[with U = ";
            constexpr std::string_view suffix    = "]";
#else
            constexpr std::string_view signature = "";
            constexpr std::string_view prefix    = "";
            constexpr std::string_view suffix    = "";
#endif

            auto start = signature.find(prefix);
            if (start == std::string_view::npos)
                return buf;

            auto contentPos = start + prefix.size();
            auto end        = signature.find(suffix, contentPos);
            if (end == std::string_view::npos || end <= contentPos)
                return buf;

            std::string_view rawView = signature.substr(contentPos, end - contentPos);
            auto len                 = RemoveTokens(rawView, buf.data(), buf.size());
            if (len < buf.size())
                RTrim(buf.data(), len);

            auto StripSpaceBeforeStar = [](char* b, std::size_t l) constexpr noexcept {
                std::size_t w = 0;
                for (std::size_t r = 0; r < l; ++r)
                {
                    if (b[r] == '*' && w > 0 && b[w - 1] == ' ')
                    {
                        b[w - 1] = '*';
                    }
                    else
                    {
                        b[w++] = b[r];
                    }
                }
                if (w < l)
                    b[w] = '\0';
                return w;
            };

            len = StripSpaceBeforeStar(buf.data(), len);
            return buf;
        }

        /// <summary>Internal storage for the raw type name buffer.</summary>
        inline static constexpr std::array<char, MAX_NAME_BUFFER> buffer = BuildRawBuffer();
        /// <summary>Length of the raw name (excluding null terminator).</summary>
        inline static constexpr std::size_t length = ConstexprStrnlen(buffer.data(), buffer.size());
        /// <summary>View into the raw buffer as a string_view.</summary>
        inline static constexpr std::string_view name =
                std::string_view(buffer.data(), length);
    };

    /// <summary>Base traits class exposing fundamental type properties.</summary>
    /// <typeparam name="T">Type to inspect.</typeparam>
    template<typename T>
    struct TypeTraitsBase
    {
    private:
        using NoRef     = std::remove_reference_t<T>;
        using NoRefNoCV = std::remove_cv_t<NoRef>;
        using Bare      = std::remove_pointer_t<NoRefNoCV>;

    public:
        inline static constexpr bool isConst             = std::is_const_v<NoRef>;
        inline static constexpr bool isVolatile          = std::is_volatile_v<NoRef>;
        inline static constexpr bool isPointer           = std::is_pointer_v<NoRefNoCV>;
        inline static constexpr bool isReference         = std::is_reference_v<T>;
        inline static constexpr bool isLvalueReference   = std::is_lvalue_reference_v<T>;
        inline static constexpr bool isRvalueReference   = std::is_rvalue_reference_v<T>;
        inline static constexpr bool isArray             = std::is_array_v<NoRefNoCV>;
        inline static constexpr bool isEnum              = std::is_enum_v<Bare>;
        inline static constexpr bool isClass             = std::is_class_v<Bare>;
        inline static constexpr bool isUnion             = std::is_union_v<Bare>;
        inline static constexpr bool isIntegral          = std::is_integral_v<Bare>;
        inline static constexpr bool isFloatingPoint     = std::is_floating_point_v<Bare>;
        inline static constexpr bool isArithmetic        = std::is_arithmetic_v<NoRefNoCV>;
        inline static constexpr bool isFundamental       = std::is_fundamental_v<Bare>;
        inline static constexpr bool isSigned            = std::is_signed_v<Bare>;
        inline static constexpr bool isUnsigned          = std::is_unsigned_v<Bare>;
        inline static constexpr bool isTriviallyCopyable = std::is_trivially_copyable_v<Bare>;
        inline static constexpr bool isVoid              = std::is_void_v<Bare>;
    };

    /// <summary>Traits for non-template types, providing name and namespace.</summary>
    /// <typeparam name="T">Type to reflect.</typeparam>
    template<typename T>
    struct TypeTraits : TypeTraitsBase<T>
    {
    private:
        using Base = std::remove_cv_t<std::remove_reference_t<T>>;

    public:
        /// <summary>Full raw name including template parameters.</summary>
        inline static const std::string_view rawName = RawTypeNameBuilder<Base>::name;
        /// <summary>Qualified name including namespace.</summary>
        inline static const std::string_view qualifiedName = rawName;
        /// <summary>Unqualified name without namespace.</summary>
        inline static const std::string_view unqualifiedName = []() {
            auto pos = FindLastTopLevelDoubleColon(qualifiedName);
            return (pos == std::string_view::npos)
                           ? qualifiedName
                           : qualifiedName.substr(pos + 2);
        }();
        /// <summary>Namespace part of the qualified name.</summary>
        inline static const std::string_view namespaceName = []() {
            auto pos = FindLastTopLevelDoubleColon(qualifiedName);
            return (pos == std::string_view::npos)
                           ? std::string_view {}
                           : qualifiedName.substr(0, pos);
        }();
    };

    /// <summary>Traits specialization for template types.</summary>
    /// <typeparam name="Template">Template class.</typeparam>
    /// <typeparam name="Args">Template parameter types.</typeparam>
    template<template<typename...> class Template, typename... Args>
    struct TypeTraits<Template<Args...>> : TypeTraitsBase<Template<Args...>>
    {
    private:
        using ThisT = Template<Args...>;

        /// <summary>Build the qualified or unqualified template name buffer.</summary>
        /// <tparam Qualified>Whether to include namespaces.</t>
        template<bool Qualified>
        static constexpr std::array<char, MAX_NAME_BUFFER> BuildNameBuffer() noexcept
        {
            std::array<char, MAX_NAME_BUFFER> buf {};
            std::size_t p          = 0;
            constexpr auto raw     = RawTypeNameBuilder<ThisT>::name;
            auto baseEnd           = raw.find('<');
            std::string_view base  = (baseEnd == std::string_view::npos)
                                             ? raw
                                             : raw.substr(0, baseEnd);
            std::string_view qbase = base;
            if constexpr (!Qualified)
            {
                auto pos = FindLastTopLevelDoubleColon(base);
                if (pos != std::string_view::npos)
                    qbase = base.substr(pos + 2);
            }
            for (char c: qbase)
                if (p + 1 < buf.size())
                    buf[p++] = c;
            if constexpr (sizeof...(Args) > 0)
            {
                if (p + 1 < buf.size())
                    buf[p++] = '<';
                bool first = true;
                (..., ([&]() {
                     if (!first && p + 2 < buf.size())
                     {
                         buf[p++] = ',';
                         buf[p++] = ' ';
                     }
                     first      = false;
                     auto& name = Qualified
                                          ? TypeTraits<Args>::qualifiedName
                                          : TypeTraits<Args>::unqualifiedName;
                     for (char c: name)
                         if (p + 1 < buf.size())
                             buf[p++] = c;
                 }()));
                if (p + 1 < buf.size())
                    buf[p++] = '>';
            }
            if (p < buf.size())
                buf[p] = '\0';
            return buf;
        }

        inline static const auto qualBuf        = BuildNameBuffer<true>();
        inline static const std::size_t qualLen = ConstexprStrnlen(qualBuf.data(), qualBuf.size());
        inline static const auto unqBuf         = BuildNameBuffer<false>();
        inline static const std::size_t unqLen  = ConstexprStrnlen(unqBuf.data(), unqBuf.size());

    public:
        /// <summary>Raw name including all tokens and parameters.</summary>
        inline static const std::string_view rawName = RawTypeNameBuilder<ThisT>::name;
        /// <summary>Qualified template name with namespace.</summary>
        inline static const std::string_view qualifiedName = std::string_view(qualBuf.data(), qualLen);
        /// <summary>Unqualified template name without namespace.</summary>
        inline static const std::string_view unqualifiedName = std::string_view(unqBuf.data(), unqLen);
        /// <summary>Namespace part of the qualified template name.</summary>
        inline static const std::string_view namespaceName = []() {
            auto pos = FindLastTopLevelDoubleColon(qualifiedName);
            return (pos == std::string_view::npos)
                           ? std::string_view {}
                           : qualifiedName.substr(0, pos);
        }();
    };

}// namespace NGIN::Meta
