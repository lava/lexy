// Copyright (C) 2020-2021 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#ifndef LEXY_DSL_IDENTIFIER_HPP_INCLUDED
#define LEXY_DSL_IDENTIFIER_HPP_INCLUDED

#include <lexy/_detail/nttp_string.hpp>
#include <lexy/dsl/alternative.hpp>
#include <lexy/dsl/any.hpp>
#include <lexy/dsl/base.hpp>
#include <lexy/dsl/literal.hpp>
#include <lexy/dsl/token.hpp>
#include <lexy/engine/find.hpp>
#include <lexy/engine/literal.hpp>
#include <lexy/engine/while.hpp>
#include <lexy/lexeme.hpp>
#include <lexy/token.hpp>

//=== identifier ===//
namespace lexy
{
/// Error when we matched a reserved.
struct reserved_identifier
{
    static LEXY_CONSTEVAL auto name()
    {
        return "reserved identifier";
    }
};
} // namespace lexy

namespace lexyd
{
template <typename Leading, typename Trailing>
struct _idp : token_base<_idp<Leading, Trailing>>
{
    static LEXY_CONSTEVAL auto token_kind()
    {
        return lexy::identifier_token_kind;
    }

    struct token_engine : lexy::engine_matcher_base
    {
        using error_code = typename Leading::token_engine::error_code;

        template <typename Reader>
        static constexpr error_code match(Reader& reader)
        {
            if (auto ec = Leading::token_engine::match(reader); ec != error_code())
                return ec;

            lexy::engine_while<typename Trailing::token_engine>::match(reader);
            return error_code();
        }
    };

    template <typename Context, typename Reader>
    static constexpr void token_error(Context& context, Reader reader,
                                      typename token_engine::error_code ec,
                                      typename Reader::iterator         pos)
    {
        Leading::token_error(context, reader, ec, pos);
    }
};

// Not a full token, we only need ::token_engine to make it work.
template <typename R>
struct _contains : token_base<_contains<R>>
{
    struct token_engine : lexy::engine_matcher_base
    {
        enum class error_code
        {
            not_found = 1,
        };

        template <typename Reader>
        static constexpr error_code match(Reader& reader)
        {
            using condition = typename decltype(lexyd::token(R{}))::token_engine;
            if (!lexy::engine_try_match<lexy::engine_find<condition>>(reader))
                return error_code::not_found;

            lexy::engine_any::match(reader);
            return error_code();
        }
    };
};

template <typename String, typename Id>
struct _kw;

template <typename Leading, typename Trailing, typename... Reserved>
struct _id : rule_base
{
    template <typename NextParser>
    struct parser
    {
        template <typename... PrevArgs>
        struct _continuation
        {
            template <typename Context, typename Reader>
            LEXY_DSL_FUNC bool parse(Context& context, Reader& reader, PrevArgs&&... prev_args,
                                     Reader old)
            {
                auto begin = old.cur();
                auto end   = reader.cur();

                // Check that we're not creating a reserved identifier.
                if constexpr (sizeof...(Reserved) > 0)
                {
                    using reserved = decltype((Reserved{} / ...));

                    auto id_reader = lexy::partial_reader(old, end);
                    if (lexy::engine_try_match<typename reserved::token_engine>(id_reader)
                        && id_reader.cur() == end)
                    {
                        // We found a reserved identifier.
                        auto err = lexy::make_error<Reader, lexy::reserved_identifier>(begin, end);
                        context.error(err);
                        // But we can trivially recover, as we've still matched a well-formed
                        // identifier.
                    }
                }

                // We're done, create the value and continue.
                return NextParser::parse(context, reader, LEXY_FWD(prev_args)...,
                                         lexy::lexeme<Reader>(begin, end));
            }
        };

        template <typename Context, typename Reader, typename... Args>
        LEXY_DSL_FUNC bool parse(Context& context, Reader& reader, Args&&... args)
        {
            // Parse the pattern with a special continuation.
            using pattern = _idp<Leading, Trailing>;
            using cont    = _continuation<Args...>;
            return lexy::rule_parser<pattern, cont>::parse(context, reader, LEXY_FWD(args)...,
                                                           Reader(reader));
        }
    };

    template <typename R>
    LEXY_CONSTEVAL auto _make_reserve(R r) const
    {
        return lexyd::token(r);
    }
    template <typename String, typename Id>
    LEXY_CONSTEVAL auto _make_reserve(_kw<String, Id>) const
    {
        static_assert(std::is_same_v<decltype(Id{}.pattern()), decltype(pattern())>,
                      "must not reserve keywords from another identifier");
        // We turn the keyword into a literal to be able to use a trie for matching.
        return _lit<String>{};
    }

    //=== dsl ===//
    /// Adds a set of reserved identifiers.
    template <typename... R>
    LEXY_CONSTEVAL auto reserve(R... r) const
    {
        static_assert(sizeof...(R) > 0);
        return _id<Leading, Trailing, Reserved..., decltype(_make_reserve(r))...>{};
    }

    /// Reserves everything starting with the given rule.
    template <typename... R>
    LEXY_CONSTEVAL auto reserve_prefix(R... prefix) const
    {
        return reserve((prefix + lexyd::any)...);
    }

    /// Reservers everything containing the given rule.
    template <typename... R>
    LEXY_CONSTEVAL auto reserve_containing(R...) const
    {
        return reserve(_contains<R>{}...);
    }

    /// Matches every identifier, ignoring reserved ones.
    LEXY_CONSTEVAL auto pattern() const
    {
        return _idp<Leading, Trailing>{};
    }

    /// Matches the initial char set of an identifier.
    LEXY_CONSTEVAL auto leading_pattern() const
    {
        return Leading{};
    }

    /// Matches the trailing char set of an identifier.
    LEXY_CONSTEVAL auto trailing_pattern() const
    {
        return Trailing{};
    }
};

/// Creates an identifier that consists of one or more of the given tokens.
template <typename Token>
LEXY_CONSTEVAL auto identifier(Token)
{
    return _id<Token, Token>{};
}

/// Creates an identifier that consists of one leading token followed by zero or more trailing
/// tokens.
template <typename LeadingToken, typename TrailingToken>
LEXY_CONSTEVAL auto identifier(LeadingToken, TrailingToken)
{
    return _id<LeadingToken, TrailingToken>{};
}
} // namespace lexyd

//=== keyword ===//
namespace lexyd
{
template <typename String, typename Id>
struct _kw : token_base<_kw<String, Id>>
{
    struct token_engine : lexy::engine_matcher_base
    {
        enum class error_code
        {
            error = 1,
        };

        template <typename Reader>
        static constexpr error_code match(Reader& reader)
        {
            // Try to match the keyword.
            using literal_engine = typename _lit<String>::token_engine;
            if (auto ec = literal_engine::match(reader);
                ec != typename literal_engine::error_code())
                return error_code::error;

            // To qualify as a keyword, and not just the prefix of an identifier,
            // we must not have a trailing identifier character.
            using trailing_engine = typename decltype(Id{}.trailing_pattern())::token_engine;
            if (lexy::engine_peek<trailing_engine>(reader))
                return error_code::error;

            return error_code();
        }
    };

    template <typename Context, typename Reader>
    static constexpr void token_error(Context& context, Reader reader,
                                      typename token_engine::error_code,
                                      typename Reader::iterator pos)
    {
        using reader_char_type = typename Reader::encoding::char_type;
        constexpr auto string  = String::template get<reader_char_type>();

        // Find the range of the identifier.
        auto begin = pos;
        if (begin == reader.cur())
        {
            // We failed at the first character, need to match the identifier as normal.
            using id_engine = typename decltype(Id{}.pattern())::token_engine;
            lexy::engine_try_match<id_engine>(reader);
        }
        else
        {
            // We have already moved past the initial character, consume trailing only.
            using trailing_engine = typename decltype(Id{}.trailing_pattern())::token_engine;
            lexy::engine_while<trailing_engine>::match(reader);
        }
        auto end = reader.cur();

        auto err = lexy::make_error<Reader, lexy::expected_keyword>(begin, end, string.c_str());
        context.error(err);
    }
};

template <typename String, typename L, typename T, typename... R>
LEXY_CONSTEVAL auto _keyword(_id<L, T, R...>)
{
    // We don't need the reserved words, remove them to keep type name short.
    static_assert(String::size > 0, "keyword must not be empty");
    return _kw<String, _id<L, T>>{};
}

#if LEXY_HAS_NTTP
/// Matches the keyword.
template <lexy::_detail::string_literal Str, typename L, typename T, typename... R>
LEXY_CONSTEVAL auto keyword(_id<L, T, R...> id)
{
    return _keyword<lexy::_detail::type_string<Str>>(id);
}
#endif

#define LEXY_KEYWORD(Str, Id) ::lexyd::_keyword<LEXY_NTTP_STRING(Str)>(Id)
} // namespace lexyd

#endif // LEXY_DSL_IDENTIFIER_HPP_INCLUDED

