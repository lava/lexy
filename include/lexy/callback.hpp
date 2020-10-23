// Copyright (C) 2020 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#ifndef LEXY_CALLBACK_HPP_INCLUDED
#define LEXY_CALLBACK_HPP_INCLUDED

#include <lexy/_detail/config.hpp>
#include <lexy/lexeme.hpp>

namespace lexy
{
template <typename Fn>
struct _fn_holder
{
    Fn fn;

    constexpr explicit _fn_holder(Fn fn) : fn(fn) {}

    template <typename... Args>
    constexpr auto operator()(Args&&... args) const -> decltype(fn(LEXY_FWD(args)...))
    {
        return fn(LEXY_FWD(args)...);
    }
};

template <typename Fn>
using _fn_as_base = std::conditional_t<std::is_class_v<Fn>, Fn, _fn_holder<Fn>>;
} // namespace lexy

namespace lexy
{
template <typename ReturnType, typename... Fns>
struct _callback : _fn_as_base<Fns>...
{
    using return_type = ReturnType;

    LEXY_CONSTEVAL explicit _callback(Fns... fns) : _fn_as_base<Fns>(fns)... {}

    using _fn_as_base<Fns>::operator()...;

    // This is a fallback overload to create a nice error message if the callback isn't handling a
    // case. The const volatile qualification ensures that it is worse than any other option, unless
    // another callback is const volatile qualified (but who does that).
    template <typename... Args>
    constexpr return_type operator()(const Args&...) const volatile
    {
        static_assert(_detail::error<Args...>, "missing callback overload for Args...");
        return LEXY_DECLVAL(return_type);
    }
};

/// Creates a callback.
template <typename ReturnType = void, typename... Fns>
LEXY_CONSTEVAL auto callback(Fns&&... fns)
{
    static_assert(((std::is_pointer_v<
                        std::decay_t<Fns>> || std::is_empty_v<std::decay_t<Fns>>)&&...),
                  "only capture-less lambdas are allowed in a callback");
    return _callback<ReturnType, std::decay_t<Fns>...>(LEXY_FWD(fns)...);
}

/// Invokes a callback into a result.
template <typename Result, typename ErrorOrValue, typename Callback, typename... Args>
constexpr Result invoke_as_result(ErrorOrValue tag, Callback&& callback, Args&&... args)
{
    using callback_t  = std::decay_t<Callback>;
    using return_type = typename callback_t::return_type;

    if constexpr (std::is_same_v<return_type, void>)
    {
        LEXY_FWD(callback)(LEXY_FWD(args)...);
        return Result(tag);
    }
    else
    {
        return Result(tag, LEXY_FWD(callback)(LEXY_FWD(args)...));
    }
}
} // namespace lexy

namespace lexy
{
template <typename T, typename Callback>
class _sink
{
public:
    using return_type = T;

    constexpr explicit _sink(Callback cb) : _value(), _cb(cb) {}

    template <typename... Args>
    constexpr void operator()(Args&&... args)
    {
        // We pass the value and other arguments to the internal callback.
        _cb(_value, LEXY_FWD(args)...);
    }

    constexpr T&& finish() &&
    {
        return LEXY_MOV(_value);
    }

private:
    T                          _value;
    LEXY_EMPTY_MEMBER Callback _cb;
};

template <typename T, typename... Fns>
class _sink_callback
{
public:
    LEXY_CONSTEVAL explicit _sink_callback(Fns... fns) : _cb(fns...) {}

    constexpr auto sink() const
    {
        return _sink<T, _callback<void, Fns...>>(_cb);
    }

private:
    LEXY_EMPTY_MEMBER _callback<void, Fns...> _cb;
};

/// Creates a sink callback.
template <typename T, typename... Fns>
LEXY_CONSTEVAL auto sink(Fns&&... fns)
{
    static_assert(((std::is_pointer_v<
                        std::decay_t<Fns>> || std::is_empty_v<std::decay_t<Fns>>)&&...),
                  "only capture-less lambdas are allowed in a callback");
    return _sink_callback<T, std::decay_t<Fns>...>(LEXY_FWD(fns)...);
}
} // namespace lexy

namespace lexy
{
struct _noop
{
    using return_type = void;

    constexpr auto sink() const
    {
        // We don't need a separate type, noop itself can have the required functions.
        return *this;
    }

    template <typename... Args>
    constexpr void operator()(const Args&...) const
    {}

    constexpr void finish() && {}
};

/// A callback with sink that does nothing.
inline constexpr auto noop = _noop{};
} // namespace lexy

namespace lexy
{
template <typename T>
struct _fwd
{
    using return_type = T;

    constexpr T operator()(T&& t) const
    {
        return LEXY_MOV(t);
    }
    constexpr T operator()(const T& t) const
    {
        return t;
    }
};

/// A callback that just forwards an existing object.
template <typename T>
constexpr auto forward = _fwd<T>{};

template <typename T>
struct _construct
{
    using return_type = T;

    constexpr T operator()(T&& t) const
    {
        return LEXY_MOV(t);
    }
    constexpr T operator()(const T& t) const
    {
        return t;
    }

    template <typename... Args>
    constexpr T operator()(Args&&... args) const
    {
        if constexpr (std::is_constructible_v<T, Args&&...>)
            return T(LEXY_FWD(args)...);
        else
            return T{LEXY_FWD(args)...};
    }
};

/// A callback that constructs an object of type T by forwarding the arguments.
template <typename T>
constexpr auto construct = _construct<T>{};

template <typename T, typename PtrT>
struct _new
{
    using return_type = PtrT;

    constexpr PtrT operator()(T&& t) const
    {
        auto ptr = new T(LEXY_MOV(t));
        return PtrT(ptr);
    }
    constexpr PtrT operator()(const T& t) const
    {
        auto ptr = new T(t);
        return PtrT(ptr);
    }

    template <typename... Args>
    constexpr PtrT operator()(Args&&... args) const
    {
        if constexpr (std::is_constructible_v<T, Args&&...>)
        {
            auto ptr = new T(LEXY_FWD(args)...);
            return PtrT(ptr);
        }
        else
        {
            auto ptr = new T{LEXY_FWD(args)...};
            return PtrT(ptr);
        }
    }
};

/// A callback that constructs an object of type T on the heap by forwarding the arguments.
template <typename T, typename PtrT = T*>
constexpr auto new_ = _new<T, PtrT>{};
} // namespace lexy

namespace lexy
{
template <typename T>
struct _list
{
    using return_type = T;

    template <typename... Args>
    constexpr T operator()(Args&&... args) const
    {
        // Use the initializer_list constructor.
        return T{LEXY_FWD(args)...};
    }

    struct _sink
    {
        T _result;

        using return_type = T;

        void operator()(const typename T::value_type& obj)
        {
            _result.push_back(obj);
        }
        void operator()(typename T::value_type&& obj)
        {
            _result.push_back(LEXY_MOV(obj));
        }
        template <typename... Args>
        auto operator()(Args&&... args) -> std::enable_if_t<(sizeof...(Args) > 1)>
        {
            _result.emplace_back(LEXY_FWD(args)...);
        }

        T&& finish() &&
        {
            return LEXY_MOV(_result);
        }
    };
    constexpr auto sink() const
    {
        return _sink{};
    }
};

/// A callback with sink that creates a list of things (e.g. a `std::vector`, `std::list`, etc.).
/// As a callback, it forwards the arguments to the initializer list constructor.
/// As a sink, it repeatedly calls `push_back()` and `emplace_back()`.
template <typename T>
constexpr auto as_list = _list<T>{};

template <typename T>
struct _collection
{
    using return_type = T;

    template <typename... Args>
    constexpr T operator()(Args&&... args) const
    {
        // Use the initializer_list constructor.
        return T{LEXY_FWD(args)...};
    }

    struct _sink
    {
        T _result;

        using return_type = T;

        void operator()(const typename T::value_type& obj)
        {
            _result.insert(obj);
        }
        void operator()(typename T::value_type&& obj)
        {
            _result.insert(LEXY_MOV(obj));
        }
        template <typename... Args>
        auto operator()(Args&&... args) -> std::enable_if_t<(sizeof...(Args) > 1)>
        {
            _result.emplace(LEXY_FWD(args)...);
        }

        T&& finish() &&
        {
            return LEXY_MOV(_result);
        }
    };
    constexpr auto sink() const
    {
        return _sink{};
    }
};

/// A callback with sink that creates an unordered collection of things (e.g. a `std::set`,
/// `std::unordered_map`, etc.). As a callback, it forwards the arguments to the initializer list
/// constructor. As a sink, it repeatedly calls `insert()` and `emplace()`.
template <typename T>
constexpr auto as_collection = _collection<T>{};
} // namespace lexy

namespace lexy
{
template <typename String>
struct _as_string
{
    using return_type = String;

    constexpr String operator()(String&& str) const
    {
        return LEXY_MOV(str);
    }
    constexpr String operator()(const String& str) const
    {
        return str;
    }

    template <typename CharT>
    constexpr auto operator()(const CharT* str, std::size_t length) const
        -> decltype(String(str, length))
    {
        return String(str, length);
    }

    template <typename Reader>
    constexpr String operator()(lexeme<Reader> lex) const
    {
        using iterator = typename lexeme<Reader>::iterator;
        if constexpr (std::is_pointer_v<iterator>)
            return String(lex.data(), lex.size());
        else
            return String(lex.begin(), lex.end());
    }

    struct _sink
    {
        String _result;

        using return_type = String;

        template <typename CharT>
        auto operator()(CharT c) -> decltype(_result.push_back(c))
        {
            return _result.push_back(c);
        }

        void operator()(const String& str)
        {
            _result.append(str);
        }
        void operator()(String&& str)
        {
            _result.append(LEXY_MOV(str));
        }

        template <typename CharT>
        auto operator()(const CharT* str, std::size_t length)
            -> decltype(_result.append(str, length))
        {
            return _result.append(str, length);
        }

        template <typename Reader>
        void operator()(lexeme<Reader> lex)
        {
            using iterator = typename lexeme<Reader>::iterator;
            if constexpr (std::is_pointer_v<iterator>)
                _result.append(lex.data(), lex.size());
            else
                _result.append(lex.begin(), lex.end());
        }

        String&& finish() &&
        {
            return LEXY_MOV(_result);
        }
    };
    constexpr auto sink() const
    {
        return _sink{};
    }
};

/// A callback with sink that creates a string (e.g. `std::string`).
/// As a callback, it converts a lexeme into the string.
/// As a sink, it repeatedly calls `.push_back()` for individual characters,
/// or `.append()` for lexemes or other strings.
template <typename String>
constexpr auto as_string = _as_string<String>{};
} // namespace lexy

namespace lexy
{
template <typename T>
struct _int
{
    using return_type = T;

    template <typename Integer>
    constexpr T operator()(const Integer& value) const
    {
        return T(value);
    }
    template <typename Integer>
    constexpr T operator()(int sign, const Integer& value) const
    {
        return T(sign * value);
    }
};

// A callback that takes an optional sign and an integer and produces the signed integer.
template <typename T>
constexpr auto as_integer = _int<T>{};
} // namespace lexy

#endif // LEXY_CALLBACK_HPP_INCLUDED
