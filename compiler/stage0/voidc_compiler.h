//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_COMPILER_H
#define VOIDC_COMPILER_H

#include "voidc_visitor.h"


//---------------------------------------------------------------------
//- Compilers (level 0) ...
//---------------------------------------------------------------------
visitor_t make_level_0_voidc_compiler(void);
visitor_t make_level_0_target_compiler(void);


//---------------------------------------------------------------------
//- Intrinsics (level 0) ...
//---------------------------------------------------------------------
class base_global_ctx_t;

void make_level_0_intrinsics(base_global_ctx_t &gctx);


#endif  //- VOIDC_COMPILER_H
