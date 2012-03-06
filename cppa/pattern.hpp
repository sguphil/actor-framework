/******************************************************************************\
 *           ___        __                                                    *
 *          /\_ \    __/\ \                                                   *
 *          \//\ \  /\_\ \ \____    ___   _____   _____      __               *
 *            \ \ \ \/\ \ \ '__`\  /'___\/\ '__`\/\ '__`\  /'__`\             *
 *             \_\ \_\ \ \ \ \L\ \/\ \__/\ \ \L\ \ \ \L\ \/\ \L\.\_           *
 *             /\____\\ \_\ \_,__/\ \____\\ \ ,__/\ \ ,__/\ \__/.\_\          *
 *             \/____/ \/_/\/___/  \/____/ \ \ \/  \ \ \/  \/__/\/_/          *
 *                                          \ \_\   \ \_\                     *
 *                                           \/_/    \/_/                     *
 *                                                                            *
 * Copyright (C) 2011, 2012                                                   *
 * Dominik Charousset <dominik.charousset@haw-hamburg.de>                     *
 *                                                                            *
 * This file is part of libcppa.                                              *
 * libcppa is free software: you can redistribute it and/or modify it under   *
 * the terms of the GNU Lesser General Public License as published by the     *
 * Free Software Foundation, either version 3 of the License                  *
 * or (at your option) any later version.                                     *
 *                                                                            *
 * libcppa is distributed in the hope that it will be useful,                 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                       *
 * See the GNU Lesser General Public License for more details.                *
 *                                                                            *
 * You should have received a copy of the GNU Lesser General Public License   *
 * along with libcppa. If not, see <http://www.gnu.org/licenses/>.            *
\******************************************************************************/


#ifndef LIBCPPA_PATTERN_HPP
#define LIBCPPA_PATTERN_HPP

#include <list>
#include <vector>
#include <cstddef>
#include <type_traits>

#include "cppa/option.hpp"
#include "cppa/anything.hpp"
#include "cppa/any_tuple.hpp"
#include "cppa/tuple_view.hpp"
#include "cppa/type_value_pair.hpp"
#include "cppa/uniform_type_info.hpp"

#include "cppa/util/type_list.hpp"
#include "cppa/util/arg_match_t.hpp"
#include "cppa/util/fixed_vector.hpp"
#include "cppa/util/is_primitive.hpp"
#include "cppa/util/static_foreach.hpp"

#include "cppa/detail/boxed.hpp"
#include "cppa/detail/tdata.hpp"
#include "cppa/detail/types_array.hpp"

namespace cppa {

enum class wildcard_position
{
    nil,
    trailing,
    leading,
    in_between,
    multiple
};

template<typename Types>
constexpr wildcard_position get_wildcard_position()
{
    return util::tl_exists<Types, is_anything>::value
           ? ((util::tl_count<Types, is_anything>::value == 1)
              ? (std::is_same<typename Types::head, anything>::value
                 ? wildcard_position::leading
                 : (std::is_same<typename Types::back, anything>::value
                    ? wildcard_position::trailing
                    : wildcard_position::in_between))
              : wildcard_position::multiple)
           : wildcard_position::nil;
}

template<class ExtendedType, class BasicType>
ExtendedType* extend_pattern(BasicType const* p);

template<typename... Types>
class pattern
{

    template<class ExtendedType, class BasicType>
    friend ExtendedType* extend_pattern(BasicType const* p);

    static_assert(sizeof...(Types) > 0, "empty pattern");

    pattern(pattern const&) = delete;
    pattern& operator=(pattern const&) = delete;

 public:

    static constexpr size_t size = sizeof...(Types);

    static constexpr wildcard_position wildcard_pos =
            get_wildcard_position<util::type_list<Types...> >();

    typedef util::type_list<Types...> types;

    typedef typename util::tl_filter_not<types, is_anything>::type
            filtered_types;

    static constexpr size_t filtered_size = filtered_types::size;

    typedef util::fixed_vector<size_t, filtered_types::size> mapping_vector;

    typedef type_value_pair_const_iterator const_iterator;

    typedef std::reverse_iterator<const_iterator> reverse_const_iterator;

    inline const_iterator begin() const { return m_ptrs; }

    inline const_iterator end() const { return m_ptrs + size; }

    inline reverse_const_iterator rbegin() const
    {
        return reverse_const_iterator{end()};
    }

    inline reverse_const_iterator rend() const
    {
        return reverse_const_iterator{begin()};
    }

    inline const_iterator vbegin() const { return m_vbegin; }

    inline const_iterator vend() const { return m_vend; }

    pattern() : m_has_values(false)
    {
        auto& arr = detail::static_types_array<Types...>::arr;
        for (size_t i = 0; i < size; ++i)
        {
            m_ptrs[i].first = arr[i];
            m_ptrs[i].second = nullptr;
        }
        m_vbegin = m_vend = begin();
    }

    template<typename Arg0, typename... Args>
    pattern(Arg0 const& arg0, Args const&... args) : m_data(arg0, args...)
    {
        using std::is_same;
        using namespace util;
        static constexpr bool all_boxed =
                util::tl_forall<type_list<Arg0, Args...>,
                                detail::is_boxed        >::value;
        m_has_values = !all_boxed;
        typedef typename type_list<Arg0, Args...>::back arg_n;
        static constexpr bool ignore_arg_n = is_same<arg_n, arg_match_t>::value;
        // ignore extra arg_match_t argument
        static constexpr size_t args_size = sizeof...(Args)
                                          + (ignore_arg_n ? 0 : 1);
        static_assert(args_size <= size, "too many arguments");
        auto& arr = detail::static_types_array<Types...>::arr;
        // "polymophic lambda"
        init_helper f(m_ptrs, arr);
        // init elements [0, N)
        util::static_foreach<0, args_size>::_(m_data, f);
        // init elements [N, size)
        for (size_t i = args_size; i < size; ++i)
        {
            m_ptrs[i].first = arr[i];
            m_ptrs[i].second = nullptr;
        }
        init_value_iterators();
    }

    inline bool has_values() const { return m_has_values; }

 private:

    struct init_helper
    {
        type_value_pair* iter_to;
        type_value_pair_const_iterator iter_from;
        init_helper(type_value_pair* pp, detail::types_array<Types...>& tarr)
            : iter_to(pp), iter_from(tarr.begin()) { }
        template<typename T>
        void operator()(option<T> const& what)
        {
            iter_to->first = iter_from.type();
            iter_to->second = (what) ? &(*what) : nullptr;
            ++iter_to;
            ++iter_from;
        }
    };

    void init_value_iterators()
    {
        auto pred = [](type_value_pair const& tvp) { return tvp.second != 0; };
        auto last = end();
        m_vbegin = std::find_if(begin(), last, pred);
        if (m_vbegin == last)
        {
            m_vbegin = m_vend = begin();
        }
        else
        {
            m_vend = std::find_if(rbegin(), rend(), pred).base();
        }
    }

    detail::tdata<option<Types>...> m_data;
    bool m_has_values;
    type_value_pair m_ptrs[size];

    const_iterator m_vbegin;
    const_iterator m_vend;

};

template<class ExtendedType, class BasicType>
ExtendedType* extend_pattern(BasicType const* p)
{
    ExtendedType* et = new ExtendedType;
    if (p->has_values())
    {
        detail::tdata_set(et->m_data, p->m_data);
        et->m_has_values = true;
        typedef typename ExtendedType::types extended_types;
        typedef typename
                detail::static_types_array_from_type_list<extended_types>::type
                tarr;
        auto& arr = tarr::arr;
        typename ExtendedType::init_helper f(et->m_ptrs, arr);
        util::static_foreach<0, BasicType::size>::_(et->m_data, f);
        et->init_value_iterators();
    }
    return et;
}

template<typename TypeList>
struct pattern_from_type_list;

template<typename... Types>
struct pattern_from_type_list<util::type_list<Types...>>
{
    typedef pattern<Types...> type;
};

} // namespace cppa

#endif // PATTERN_HPP
