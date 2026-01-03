//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_quark.h"

#include <cstdio>
#include <cassert>

#include <unordered_map>
#include <unordered_set>

#include <immer/vector_transient.hpp>


//---------------------------------------------------------------------
//- Globals
//---------------------------------------------------------------------
static std::unordered_set<std::string> voidc_interned_strings;

static std::unordered_map<const char *, const v_quark_t> voidc_quark_from_string;

static immer::vector_transient<const std::string *> voidc_quark_to_std_string;


//---------------------------------------------------------------------
//- Basics
//---------------------------------------------------------------------
const v_quark_t *
v_quark_ptr_from_string(const char *str)
{
    if (str == nullptr)
    {
        static const v_quark_t zero = 0;

        return &zero;
    }

    if (v_intern_try_string(str))   return &voidc_quark_from_string[str];

    return &voidc_quark_from_string[v_intern_string(str)];
}

//---------------------------------------------------------------------
v_quark_t
v_quark_from_string(const char *str)
{
    return *v_quark_ptr_from_string(str);
}


//---------------------------------------------------------------------
const char *
v_quark_to_string(v_quark_t vq)
{
    if (vq == 0)  return nullptr;

    return  voidc_quark_to_std_string[vq-1]->c_str();       //- Sic!
}


//---------------------------------------------------------------------
size_t
v_quark_to_string_size(v_quark_t vq)
{
    if (vq == 0)  return 0;

    return  voidc_quark_to_std_string[vq-1]->size();        //- Sic!
}


//---------------------------------------------------------------------
//- Utility
//---------------------------------------------------------------------
v_quark_t
v_quark_try_string(const char *str)
{
    if (str == nullptr) return 0;

    if (v_intern_try_string(str))   return voidc_quark_from_string[str];

    auto it = voidc_interned_strings.find(str);

    if (it == voidc_interned_strings.end()) return 0;

    auto it_str = it->c_str();

    return voidc_quark_from_string[it_str];
}

//---------------------------------------------------------------------
const char *
v_intern_string(const char *str)
{
    if (str == nullptr) return nullptr;

    auto [it, ok] = voidc_interned_strings.insert(str);

    auto it_str = it->c_str();

    if (ok)
    {
        auto q = v_quark_t(voidc_quark_from_string.size() + 1);     //- Sic!

        voidc_quark_from_string.insert({it_str, q});

        voidc_quark_to_std_string.push_back(&*it);
    }

    return it_str;
}

//---------------------------------------------------------------------
const char *
v_intern_try_string(const char *str)
{
    if (str == nullptr) return nullptr;

    auto it = voidc_quark_from_string.find(str);

    if (it == voidc_quark_from_string.end())  return nullptr;

    return str;     //- Sic!
}


