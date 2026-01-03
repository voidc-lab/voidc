//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_QUARK_H
#define VOIDC_QUARK_H

#include "voidc_dllexport.h"

#include <cstdint>
#include <cstddef>


//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION

using v_quark_t = uint32_t;


const v_quark_t *v_quark_ptr_from_string(const char *str);

v_quark_t v_quark_from_string(const char *str);

const char *v_quark_to_string(v_quark_t vq);


size_t v_quark_to_string_size(v_quark_t vq);


v_quark_t v_quark_try_string(const char *str);

const char *v_intern_string(const char *str);

const char *v_intern_try_string(const char *str);


VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


#endif  //- VOIDC_QUARK_H
