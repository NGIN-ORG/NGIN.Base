/// @file TypeName.hpp
/// @brief Compile-time facilities for extracting readable type and template names.
/// @details Separated from `TypeTraits.hpp` to keep lightweight boolean trait
/// utilities isolated from heavier constexpr string parsing logic. Include this
/// header only when you need human-readable / reflection-style type names.
#pragma once

#include <array>
#include <string_view>
#include <type_traits>

namespace NGIN::Meta
{
    // GCC 14 libstdc++ still has some internal guards in std::string_view::find that
    // are not constexpr-friendly in deeply nested constant evaluation contexts (shows
    // up when building large template type names). To preserve full constexpr support
    // we provide lightweight manual find helpers used only inside constexpr code paths.
    constexpr std::size_t SVFind(std::string_view haystack, std::string_view needle, std::size_t pos = 0) noexcept
    {
        if (needle.empty())
            return pos <= haystack.size() ? pos : std::string_view::npos;
        if (needle.size() > haystack.size())
            return std::string_view::npos;
        for (std::size_t i = pos; i + needle.size() <= haystack.size(); ++i)
        {
            bool match = true;
            for (std::size_t j = 0; j < needle.size(); ++j)
            {
                if (haystack[i + j] != needle[j])
                {
                    match = false;
                    break;
                }
            }
            if (match)
                return i;
        }
        return std::string_view::npos;
    }

    constexpr std::size_t SVFindChar(std::string_view haystack, char c, std::size_t pos = 0) noexcept
    {
        for (std::size_t i = pos; i < haystack.size(); ++i)
            if (haystack[i] == c)
                return i;
        return std::string_view::npos;
    }
    /// <summary>Maximum buffer size for type name extraction.</summary>
    inline constexpr std::size_t MAX_NAME_BUFFER = 512;

    /// <summary>Compute the length of a C-string up to a maximum.</summary>
    constexpr std::size_t ConstexprStrnlen(const char* str, std::size_t maxLen) noexcept
    {
        std::size_t i = 0;
        while (i < maxLen && str[i] != '\0')
            ++i;
        return i;
    }

    /// <summary>Strip common C++ type tokens from a signature fragment.</summary>
    constexpr std::size_t RemoveTokens(std::string_view input, char* outBuffer, std::size_t outBufferSize) noexcept
    {
        constexpr std::array<std::string_view, 4> tokens = {"class ", "struct ", "enum ", "union "};
        constexpr std::array<std::size_t, 4>      length = {6, 7, 5, 6};
        std::size_t                               i = 0, out = 0;
        while (i < input.size() && out + 1 < outBufferSize)
        {
            bool matched = false;
            for (size_t t = 0; t < tokens.size(); ++t)
            {
                if (i + length[t] <= input.size() && input.compare(i, length[t], tokens[t]) == 0)
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

    /// <summary>Remove trailing spaces or tabs from a buffer (in place).</summary>
    constexpr void RTrim(char* buf, std::size_t& len) noexcept
    {
        while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t'))
            buf[--len] = '\0';
    }

    /// <summary>Find the last occurrence of top-level "::" outside template args.</summary>
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
    template<typename U>
    struct RawTypeNameBuilder
    {
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

            auto start = SVFind(signature, prefix);
            if (start == std::string_view::npos)
                return buf;
            auto contentPos = start + prefix.size();
            auto end        = SVFind(signature, suffix, contentPos);
            if (end == std::string_view::npos || end <= contentPos)
                return buf;

            std::string_view rawView = signature.substr(contentPos, end - contentPos);
            auto             len     = RemoveTokens(rawView, buf.data(), buf.size());
            if (len < buf.size())
                RTrim(buf.data(), len);

            auto StripSpaceBeforeStar = [](char* b, std::size_t l) constexpr noexcept {
                std::size_t w = 0;
                for (std::size_t r = 0; r < l; ++r)
                {
                    if (b[r] == '*' && w > 0 && b[w - 1] == ' ')
                        b[w - 1] = '*';
                    else
                        b[w++] = b[r];
                }
                if (w < l)
                    b[w] = '\0';
                return w;
            };

            StripSpaceBeforeStar(buf.data(), len);
            return buf;
        }

        inline static constexpr std::array<char, MAX_NAME_BUFFER> buffer = BuildRawBuffer();
        inline static constexpr std::size_t                       length = ConstexprStrnlen(buffer.data(), buffer.size());
        inline static constexpr std::string_view                  name   = std::string_view(buffer.data(), length);
    };

    //================ TypeName primary (non-template types) =================//
    template<typename T>
    struct TypeName
    {
    private:
        using Base = std::remove_cv_t<std::remove_reference_t<T>>;

    public:
        inline static constexpr std::string_view rawName         = RawTypeNameBuilder<Base>::name;
        inline static constexpr std::string_view qualifiedName   = rawName;
        inline static constexpr std::string_view unqualifiedName = []() {
            auto pos = FindLastTopLevelDoubleColon(qualifiedName);
            return (pos == std::string_view::npos) ? qualifiedName : qualifiedName.substr(pos + 2);
        }();
        inline static constexpr std::string_view namespaceName = []() {
            auto pos = FindLastTopLevelDoubleColon(qualifiedName);
            return (pos == std::string_view::npos) ? std::string_view {} : qualifiedName.substr(0, pos);
        }();
    };

    //================ TypeName partial specialization (class templates) =================//
    template<template<typename...> class Template, typename... Args>
    struct TypeName<Template<Args...>>
    {
    private:
        using ThisT = Template<Args...>;

        template<bool Qualified>
        static constexpr std::array<char, MAX_NAME_BUFFER> BuildNameBuffer() noexcept
        {
            std::array<char, MAX_NAME_BUFFER> buf {};
            std::size_t                       p       = 0;
            constexpr auto                    raw     = RawTypeNameBuilder<ThisT>::name;
            auto                              baseEnd = SVFindChar(raw, '<');
            std::string_view                  base    = (baseEnd == std::string_view::npos) ? raw : raw.substr(0, baseEnd);
            std::string_view                  qbase   = base;
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
                     auto& name = Qualified ? TypeName<Args>::qualifiedName : TypeName<Args>::unqualifiedName;
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

        inline static constexpr auto        qualBuf = BuildNameBuffer<true>();
        inline static constexpr std::size_t qualLen = ConstexprStrnlen(qualBuf.data(), qualBuf.size());
        inline static constexpr auto        unqBuf  = BuildNameBuffer<false>();
        inline static constexpr std::size_t unqLen  = ConstexprStrnlen(unqBuf.data(), unqBuf.size());

    public:
        inline static constexpr std::string_view rawName         = RawTypeNameBuilder<ThisT>::name;
        inline static constexpr std::string_view qualifiedName   = std::string_view(qualBuf.data(), qualLen);
        inline static constexpr std::string_view unqualifiedName = std::string_view(unqBuf.data(), unqLen);
        inline static constexpr std::string_view namespaceName   = []() {
            auto pos = FindLastTopLevelDoubleColon(qualifiedName);
            return (pos == std::string_view::npos) ? std::string_view {} : qualifiedName.substr(0, pos);
        }();
    };
}// namespace NGIN::Meta
