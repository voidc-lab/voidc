//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_types.h"

#include "voidc_target.h"

#include <cstdio>
#include <cassert>
#include <algorithm>

#include <llvm-c/Core.h>


//---------------------------------------------------------------------
v_type_t::~v_type_t() {}

v_type_generic_t::arg_t::~arg_t() {}


//---------------------------------------------------------------------
static v_quark_t obtain_llvm_type_quarks[v_type_t::k_count];
static v_quark_t initialize_quarks[v_type_t::k_count];


//---------------------------------------------------------------------
extern "C"
{


//---------------------------------------------------------------------
static
LLVMTypeRef
obtain_llvm_type_void(void *, const v_type_t *typ)
{
    typ->cached_llvm_type = LLVMVoidTypeInContext(typ->context.llvm_ctx);

    return  typ->cached_llvm_type;
}

static
LLVMTypeRef
obtain_llvm_type_f16(void *, const v_type_t *typ)
{
    typ->cached_llvm_type = LLVMHalfTypeInContext(typ->context.llvm_ctx);

    return  typ->cached_llvm_type;
}

static
LLVMTypeRef
obtain_llvm_type_f32(void *, const v_type_t *typ)
{
    typ->cached_llvm_type = LLVMFloatTypeInContext(typ->context.llvm_ctx);

    return  typ->cached_llvm_type;
}

static
LLVMTypeRef
obtain_llvm_type_f64(void *, const v_type_t *typ)
{
    typ->cached_llvm_type = LLVMDoubleTypeInContext(typ->context.llvm_ctx);

    return  typ->cached_llvm_type;
}

static
LLVMTypeRef
obtain_llvm_type_f128(void *, const v_type_t *typ)
{
    typ->cached_llvm_type = LLVMFP128TypeInContext(typ->context.llvm_ctx);

    return  typ->cached_llvm_type;
}


//---------------------------------------------------------------------
static
LLVMTypeRef
obtain_llvm_type_integer(void *, const v_type_t *typ)
{
    auto t = static_cast<const v_type_integer_t *>(typ);

    typ->cached_llvm_type = LLVMIntTypeInContext(typ->context.llvm_ctx, t->width());

    return  typ->cached_llvm_type;
}


//---------------------------------------------------------------------
static
LLVMTypeRef
obtain_llvm_type_function(void *, const v_type_t *typ)
{
    auto t = static_cast<const v_type_function_t *>(typ);

    auto pcount = t->param_count();
    auto params = t->param_types();

    LLVMTypeRef ft[pcount];

    for (unsigned i=0; i<pcount; ++i)
    {
        ft[i] = params[i]->llvm_type();
    }

    typ->cached_llvm_type = LLVMFunctionType(t->return_type()->llvm_type(), ft, pcount, t->is_var_arg());

    return  typ->cached_llvm_type;
}


//---------------------------------------------------------------------
static
LLVMTypeRef
obtain_llvm_type_refptr(void *, const v_type_t *typ)
{
    auto t = static_cast<const v_type_refptr_t *>(typ);

    typ->cached_llvm_type = LLVMPointerTypeInContext(typ->context.llvm_ctx, t->address_space());

    return  typ->cached_llvm_type;
}


//---------------------------------------------------------------------
static
LLVMTypeRef
obtain_llvm_type_struct(void *, const v_type_t *typ)
{
    auto t = static_cast<const v_type_struct_t *>(typ);

    if (t->is_opaque())  return  typ->context.opaque_struct_type;

    auto count = t->element_count();
    auto types = t->element_types();

    LLVMTypeRef elts[count];

    for (unsigned i=0; i<count; ++i)
    {
        elts[i] = types[i]->llvm_type();
    }

    typ->cached_llvm_type = LLVMStructTypeInContext(typ->context.llvm_ctx, elts, count, t->is_packed());

    return  typ->cached_llvm_type;
}


//---------------------------------------------------------------------
static
LLVMTypeRef
obtain_llvm_type_array(void *, const v_type_t *typ)
{
    auto t = static_cast<const v_type_array_t *>(typ);

    auto et = t->element_type()->llvm_type();

    typ->cached_llvm_type = LLVMArrayType2(et, t->length());

    return  typ->cached_llvm_type;
}


//---------------------------------------------------------------------
static
LLVMTypeRef
obtain_llvm_type_vector(void *, const v_type_t *typ)
{
    auto t = static_cast<const v_type_vector_t *>(typ);

    auto et = t->element_type()->llvm_type();

    typ->cached_llvm_type = LLVMVectorType(et, t->size());

    return  typ->cached_llvm_type;
}

static
LLVMTypeRef
obtain_llvm_type_svector(void *, const v_type_t *typ)
{
    auto t = static_cast<const v_type_svector_t *>(typ);

    auto et = t->element_type()->llvm_type();

    typ->cached_llvm_type = LLVMScalableVectorType(et, t->size());

    return  typ->cached_llvm_type;
}


//---------------------------------------------------------------------
static
LLVMTypeRef
obtain_llvm_type_generic(void *, const v_type_t *typ)
{
    typ->cached_llvm_type = nullptr;        //- Sic !!!

    return  typ->cached_llvm_type;
}


//---------------------------------------------------------------------
}   //- extern "C"


void
v_type_struct_t::set_body(v_type_t * const *elts, unsigned count, bool packed)
{
    if (!is_opaque())       //- Check!
    {
        auto &[e, p] = *body_key;

        bool ok = (p == packed  &&  e.size() == count);

        if (ok)
        {
            for (unsigned i=0; i<count; ++i)
            {
                if (e[i] != elts[i])
                {
                    ok = false;

                    break;
                }
            }
        }

        if (ok) return;

        throw std::runtime_error(std::string("Struct body does not match: ") + v_quark_to_string(*name_key));
    }

    auto *st = context.make_struct_type(elts, count, packed);

    body_key = st->body_key;

    assert(cached_llvm_type == LLVMTypeRef(-2));        //- Sic !!!

    void *aux;
    auto *fun = context.get_initialize_hook(kind(), &aux);

    fun(aux, this);                     //- Second time!
}


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
voidc_types_ctx_t::voidc_types_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size)
  : llvm_ctx(ctx),
    opaque_struct_type(LLVMStructTypeInContext(ctx, nullptr, 0, false)),        //- Sic!!!

    void_type     (make_void_type()),

    bool_type     (make_uint_type(1)),
    char_type     (make_int_type(8)),
    short_type    (make_int_type(16)),
    int_type      (make_int_type(8*int_size)),
    unsigned_type (make_uint_type(8*int_size)),
    long_type     (make_int_type(8*long_size)),
    long_long_type(make_int_type(64)),
    intptr_t_type (make_int_type(8*ptr_size)),
    size_t_type   (make_uint_type(8*ptr_size)),
    char32_t_type (make_uint_type(32)),
    uint64_t_type (make_uint_type(64)),

    static_type_type(make_generic_type(v_quark_from_string("v_static_type_t"), nullptr, 0))
{
    //- ...
}

voidc_types_ctx_t::~voidc_types_ctx_t()
{
    //- ...
}


//---------------------------------------------------------------------
template <typename T> inline
T *
voidc_types_ctx_t::check_cached_llvm_type(T *t)
{
    if (t->cached_llvm_type == LLVMTypeRef(-1))
    {
        void *aux;

        auto fun = get_initialize_hook(t->kind(), &aux);

        if (fun)  fun(aux, t);

        t->cached_llvm_type = LLVMTypeRef(-2);          //- Sic !!!
    }

    return  t;
}


//---------------------------------------------------------------------
template <typename T> inline
T *
voidc_types_ctx_t::make_type_helper(std::unique_ptr<T> &tptr)
{
    if (!tptr)  tptr.reset(new T(*this));

    return check_cached_llvm_type(tptr.get());
}

//---------------------------------------------------------------------
v_type_void_t * voidc_types_ctx_t::make_void_type(void) { return make_type_helper(_void_type); }
v_type_f16_t  * voidc_types_ctx_t::make_f16_type(void)  { return make_type_helper(_f16_type);  }
v_type_f32_t  * voidc_types_ctx_t::make_f32_type(void)  { return make_type_helper(_f32_type);  }
v_type_f64_t  * voidc_types_ctx_t::make_f64_type(void)  { return make_type_helper(_f64_type);  }
v_type_f128_t * voidc_types_ctx_t::make_f128_type(void) { return make_type_helper(_f128_type); }


//---------------------------------------------------------------------
template <typename T, typename K> inline
T *
voidc_types_ctx_t::make_type_helper(types_map_t<T, K> &tmap, const K &key)
{
    auto [it, nx] = tmap.try_emplace(key, nullptr);

    if (nx)
    {
        auto t = new T(*this, it->first);

        it->second.reset(t);
    }

    return check_cached_llvm_type(it->second.get());
}


//---------------------------------------------------------------------
v_type_int_t *
voidc_types_ctx_t::make_int_type(unsigned bits)
{
    return make_type_helper(int_types, bits);
}

v_type_uint_t *
voidc_types_ctx_t::make_uint_type(unsigned bits)
{
    return make_type_helper(uint_types, bits);
}


//---------------------------------------------------------------------
v_type_function_t *
voidc_types_ctx_t::make_function_type(v_type_t *ret, v_type_t * const *args, unsigned count, bool var_arg)
{
    const unsigned N = count + 1;

    v_type_t *ft_data[N];

    ft_data[0] = ret;

    std::copy_n(args, count, ft_data+1);

    assert((std::all_of(ft_data, ft_data+N, [this](auto t){ return &t->context == this; })));

    v_type_function_t::key_t key = { {ft_data, ft_data+N}, var_arg };

    return make_type_helper(function_types, key);
}


//---------------------------------------------------------------------
v_type_pointer_t *
voidc_types_ctx_t::make_pointer_type(v_type_t *et, unsigned addr_space)
{
    assert(&et->context == this);

    v_type_pointer_t::key_t key = { et, addr_space };

    return make_type_helper(pointer_types, key);
}

//---------------------------------------------------------------------
v_type_reference_t *
voidc_types_ctx_t::make_reference_type(v_type_t *et, unsigned addr_space)
{
    assert(&et->context == this);

    v_type_reference_t::key_t key = { et, addr_space };

    return make_type_helper(reference_types, key);
}


//---------------------------------------------------------------------
v_type_struct_t *
voidc_types_ctx_t::make_struct_type(v_quark_t name)
{
    return make_type_helper(named_struct_types, name);
}

v_type_struct_t *
voidc_types_ctx_t::make_struct_type(v_type_t * const *elts, unsigned count, bool packed)
{
    assert((std::all_of(elts, elts+count, [this](auto t){ return &t->context == this; })));

    v_type_struct_t::body_key_t key = { {elts, elts+count}, packed };

    return make_type_helper(anon_struct_types, key);
}


//---------------------------------------------------------------------
v_type_array_t *
voidc_types_ctx_t::make_array_type(v_type_t *et, uint64_t count)
{
    assert(&et->context == this);

    v_type_array_t::key_t key = { et, count };

    return make_type_helper(array_types, key);
}


//---------------------------------------------------------------------
v_type_vector_t *
voidc_types_ctx_t::make_vector_type(v_type_t *et, unsigned count)
{
    assert(&et->context == this);

    v_type_vector_t::key_t key = { et, count };

    return make_type_helper(vector_types, key);
}

v_type_svector_t *
voidc_types_ctx_t::make_svector_type(v_type_t *et, unsigned count)
{
    assert(&et->context == this);

    v_type_svector_t::key_t key = { et, count };

    return make_type_helper(svector_types, key);
}


//---------------------------------------------------------------------
v_type_generic_t *
voidc_types_ctx_t::make_generic_type(v_quark_t cons, v_type_generic_t::arg_t * const *args, unsigned count)
{
    assert((std::all_of(args, args+count, [this](auto a){ return &a->context == this; })));

    v_type_generic_t::key_t key = { cons, {args, args+count} };

    return make_type_helper(generic_types, key);
}

//---------------------------------------------------------------------
template <typename T, typename K> inline
T *
voidc_types_ctx_t::make_arg_helper(types_map_t<T, K> &tmap, const K &key)
{
    auto [it, nx] = tmap.try_emplace(key, nullptr);

    if (nx)
    {
        auto t = new T(*this, it->first);

        it->second.reset(t);
    }

    return  it->second.get();
}

//---------------------------------------------------------------------
v_type_generic_t::arg_number_t *
voidc_types_ctx_t::make_number_arg(uint64_t num)
{
    return make_arg_helper(number_args, num);
}

v_type_generic_t::arg_string_t *
voidc_types_ctx_t::make_string_arg(const std::string &str)
{
    return make_arg_helper(string_args, str);
}

v_type_generic_t::arg_quark_t *
voidc_types_ctx_t::make_quark_arg(v_quark_t q)
{
    return make_arg_helper(quark_args, q);
}

v_type_generic_t::arg_type_t *
voidc_types_ctx_t::make_type_arg(v_type_t *t)
{
    assert(&t->context == this);

    return make_arg_helper(type_args, t);
}

v_type_generic_t::arg_cons_t *
voidc_types_ctx_t::make_cons_arg(v_quark_t cons, v_type_generic_t::arg_t * const *args, unsigned count)
{
    assert((std::all_of(args, args+count, [this](auto a){ return &a->context == this; })));

    v_type_generic_t::arg_cons_t::key_t key = { cons, {args, args+count} };

    return make_arg_helper(cons_args, key);
}


//---------------------------------------------------------------------
static void *
get_hook(voidc_types_ctx_t *tctx, v_quark_t quark, void **paux)
{
    auto *gctx = static_cast<base_global_ctx_t *>(tctx);
    auto *lctx = gctx->local_ctx;

    if (!gctx->is_initialized)  return nullptr;

    auto *intrinsics = &gctx->decls.intrinsics;

    if (lctx) intrinsics = &lctx->decls.intrinsics;

    if (auto *p = intrinsics->find(quark))
    {
        if (paux) *paux = p->second;

        return  p->first;
    }

    return nullptr;
}

static void
set_hook(voidc_types_ctx_t *tctx, v_quark_t quark, void *fun, void *aux)
{
    auto &gctx = *static_cast<base_global_ctx_t *>(tctx);
    auto &lctx = *gctx.local_ctx;

    lctx.decls.intrinsics_insert({quark, {fun, aux}});
}

//---------------------------------------------------------------------
hook_initialize_t
voidc_types_ctx_t::get_initialize_hook(int k, void **paux)
{
    return  hook_initialize_t(get_hook(this, initialize_quarks[k], paux));
}

void
voidc_types_ctx_t::set_initialize_hook(int k, hook_initialize_t fun, void *aux)
{
    set_hook(this, initialize_quarks[k], (void *)fun, aux);
}

hook_obtain_llvm_type_t
voidc_types_ctx_t::get_obtain_llvm_type_hook(int k, void **paux)
{
    return  hook_obtain_llvm_type_t(get_hook(this, obtain_llvm_type_quarks[k], paux));
}

void
voidc_types_ctx_t::set_obtain_llvm_type_hook(int k, hook_obtain_llvm_type_t fun, void *aux)
{
    set_hook(this, obtain_llvm_type_quarks[k], (void *)fun, aux);
}


//-----------------------------------------------------------------
//- ...
//-----------------------------------------------------------------
void voidc_types_static_initialize(void)
{
    static_assert(sizeof(size_t)    == sizeof(void *));     //- Sic!!!
    static_assert(sizeof(intptr_t)  == sizeof(void *));     //- Sic!!!
    static_assert(sizeof(uintptr_t) == sizeof(void *));     //- Sic!!!

    auto q = v_quark_from_string;

#define DEF(kind) \
    obtain_llvm_type_quarks[v_type_t::k_##kind] = q("v.type_obtain_llvm_type_" #kind); \
    initialize_quarks[v_type_t::k_##kind] = q("v.type_initialize_" #kind);

    DEF(void)
    DEF(f16)
    DEF(f32)
    DEF(f64)
    DEF(f128)
    DEF(int)
    DEF(uint)
    DEF(function)
    DEF(pointer)
    DEF(reference)
    DEF(struct)
    DEF(array)
    DEF(vector)
    DEF(svector)
    DEF(generic)

#undef DEF
}

//-----------------------------------------------------------------
void voidc_types_static_terminate(void)
{
}

//-----------------------------------------------------------------
void voidc_types_make_voidc_constants(void)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    auto int_type = vctx.int_type;
    auto int_llvm_type = int_type->llvm_type();

#define DEF(kind) \
    auto kind##_q = v_quark_from_string("v_type_kind_" #kind); \
    vctx.decls.constants_insert({kind##_q, int_type}); \
    vctx.constant_values.insert({kind##_q, \
        LLVMConstInt(int_llvm_type, v_type_t::k_##kind, 0)});

    DEF(void)
    DEF(f16)
    DEF(f32)
    DEF(f64)
    DEF(f128)
    DEF(int)
    DEF(uint)
    DEF(function)
    DEF(pointer)
    DEF(reference)
    DEF(struct)
    DEF(array)
    DEF(vector)
    DEF(svector)
    DEF(generic)

#undef DEF

#define DEF(kind) \
    auto kind##_q = v_quark_from_string("v_type_generic_arg_kind_" #kind); \
    vctx.decls.constants_insert({kind##_q, int_type}); \
    vctx.constant_values.insert({kind##_q, \
        LLVMConstInt(int_llvm_type, v_type_generic_t::arg_t::k_##kind, 0)});

    DEF(number)
    DEF(string)
    DEF(quark)
    DEF(type)
    DEF(cons)

#undef DEF
}

//-----------------------------------------------------------------
static void dummy_fun(void *, v_type_t *) {}

void voidc_types_make_level_0_intrinsics(base_global_ctx_t &gctx)
{
#define DEF(tag, fun) \
    gctx.decls.intrinsics_insert({obtain_llvm_type_quarks[v_type_t::k_##tag], {(void *)obtain_llvm_type_##fun, nullptr}}); \
    gctx.decls.intrinsics_insert({initialize_quarks[v_type_t::k_##tag], {(void *)dummy_fun, nullptr}});

#define DEF2(tag)   DEF(tag, tag)

    DEF2(void)

    DEF2(f16)
    DEF2(f32)
    DEF2(f64)
    DEF2(f128)

    DEF(int,  integer)
    DEF(uint, integer)

    DEF2(function)

    DEF(pointer,   refptr)
    DEF(reference, refptr)

    DEF2(struct)
    DEF2(array)

    DEF2(vector)
    DEF2(svector)

    DEF2(generic)

#undef DEF2
#undef DEF
}


//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void
v_type_set_cached_llvm_type(v_type_t *typ, LLVMTypeRef llvm_typ)
{
    typ->cached_llvm_type = llvm_typ;
}


//---------------------------------------------------------------------
int
v_type_get_kind(v_type_t *type)
{
    return type->kind();
}

LLVMTypeRef
v_type_get_llvm_type(v_type_t *type)
{
    return type->llvm_type();
}

v_type_t *
v_type_get_scalar_type(v_type_t *type)
{
    if (auto *p = dynamic_cast<v_type_vector_t *>(type))
    {
        return p->element_type();
    }

    return type;
}


//---------------------------------------------------------------------
v_type_t *
v_void_type(void)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_void_type();
}

bool
v_type_is_void(v_type_t *type)
{
    return (type->kind() == v_type_t::k_void);
}


//---------------------------------------------------------------------
v_type_t *
v_f16_type(void)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_f16_type();
}

v_type_t *
v_f32_type(void)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_f32_type();
}

v_type_t *
v_f64_type(void)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_f64_type();
}

v_type_t *
v_f128_type(void)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_f128_type();
}

bool
v_type_is_floating_point(v_type_t *type)
{
    switch(type->kind())
    {
    case v_type_t::k_f16:
    case v_type_t::k_f32:
    case v_type_t::k_f64:
    case v_type_t::k_f128:

        return true;

    default:

        return false;
    }
}

unsigned
v_type_floating_point_get_width(v_type_t *type)
{
    switch(type->kind())
    {
    case v_type_t::k_f16:   return 16;
    case v_type_t::k_f32:   return 32;
    case v_type_t::k_f64:   return 64;
    case v_type_t::k_f128:  return 128;

    default:                return 0;   //- ?
    }
}


//---------------------------------------------------------------------
v_type_t *
v_int_type(unsigned bits)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_int_type(bits);
}

v_type_t *
v_uint_type(unsigned bits)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_uint_type(bits);
}

bool
v_type_is_integer(v_type_t *type)
{
    return bool(dynamic_cast<v_type_integer_t *>(type));
}

bool
v_type_integer_is_signed(v_type_t *type)
{
    return static_cast<v_type_integer_t *>(type)->is_signed();
}

unsigned
v_type_integer_get_width(v_type_t *type)
{
    return static_cast<v_type_integer_t *>(type)->width();
}


//---------------------------------------------------------------------
v_type_t *
v_function_type(v_type_t *ret, v_type_t * const *args, unsigned count, bool var_arg)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_function_type(ret, args, count, var_arg);
}

bool
v_type_is_function(v_type_t *type)
{
    return bool(dynamic_cast<v_type_function_t *>(type));
}

bool
v_type_function_is_var_arg(v_type_t *type)
{
    return static_cast<v_type_function_t *>(type)->is_var_arg();
}

v_type_t *
v_type_function_get_return_type(v_type_t *type)
{
    return static_cast<v_type_function_t *>(type)->return_type();
}

unsigned
v_type_function_get_param_count(v_type_t *type)
{
    return static_cast<v_type_function_t *>(type)->param_count();
}

v_type_t * const *
v_type_function_get_param_types(v_type_t *type)
{
    return static_cast<v_type_function_t *>(type)->param_types();
}


//---------------------------------------------------------------------
v_type_t *
v_pointer_type(v_type_t *elt, unsigned addr_space)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_pointer_type(elt, addr_space);
}

v_type_t *
v_reference_type(v_type_t *elt, unsigned addr_space)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_reference_type(elt, addr_space);
}

bool
v_type_is_pointer(v_type_t *type)
{
    return bool(dynamic_cast<v_type_pointer_t *>(type));        //- ?
}

bool
v_type_is_reference(v_type_t *type)
{
    return bool(dynamic_cast<v_type_reference_t *>(type));      //- ?
}

v_type_t *
v_type_refptr_get_element_type(v_type_t *type)
{
    return static_cast<v_type_refptr_t *>(type)->element_type();
}

unsigned
v_type_refptr_get_address_space(v_type_t *type)
{
    return static_cast<v_type_refptr_t *>(type)->address_space();
}


//---------------------------------------------------------------------
v_type_t *
v_struct_type_named_q(v_quark_t qname)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_struct_type(qname);
}

v_type_t *
v_struct_type_named(const char *name)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_struct_type(v_quark_from_string(name));
}

v_type_t *
v_struct_type(v_type_t * const *elts, unsigned count, bool packed)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_struct_type(elts, count, packed);
}

bool
v_type_is_struct(v_type_t *type)
{
    return bool(dynamic_cast<v_type_struct_t *>(type));
}

v_quark_t
v_type_struct_get_name_q(v_type_t *type)
{
    return  static_cast<v_type_struct_t *>(type)->name();
}

const char *
v_type_struct_get_name(v_type_t *type)
{
    auto q = static_cast<v_type_struct_t *>(type)->name();

    return v_quark_to_string(q);
}

bool
v_type_struct_is_opaque(v_type_t *type)
{
    return static_cast<v_type_struct_t *>(type)->is_opaque();
}

bool
v_type_struct_is_packed(v_type_t *type)
{
    return static_cast<v_type_struct_t *>(type)->is_packed();
}

void
v_type_struct_set_body(v_type_t *type, v_type_t * const *elts, unsigned count, bool packed)
{
    static_cast<v_type_struct_t *>(type)->set_body(elts, count, packed);
}

unsigned
v_type_struct_get_element_count(v_type_t *type)
{
    return static_cast<v_type_struct_t *>(type)->element_count();
}

v_type_t * const *
v_type_struct_get_element_types(v_type_t *type)
{
    return static_cast<v_type_struct_t *>(type)->element_types();
}


//---------------------------------------------------------------------
v_type_t *
v_array_type(v_type_t *elt, uint64_t count)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_array_type(elt, count);
}

bool
v_type_is_array(v_type_t *type)
{
    return bool(dynamic_cast<v_type_array_t *>(type));
}

v_type_t *
v_type_array_get_element_type(v_type_t *type)
{
    return static_cast<v_type_array_t *>(type)->element_type();
}

uint64_t
v_type_array_get_length(v_type_t *type)
{
    return static_cast<v_type_array_t *>(type)->length();
}


//---------------------------------------------------------------------
v_type_t *
v_vector_type(v_type_t *elt, unsigned count)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_vector_type(elt, count);
}

v_type_t *
v_svector_type(v_type_t *elt, unsigned count)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_svector_type(elt, count);
}

bool
v_type_is_vector(v_type_t *type)
{
    return bool(dynamic_cast<v_type_vector_t *>(type));
}

v_type_t *
v_type_vector_get_element_type(v_type_t *type)
{
    return static_cast<v_type_vector_t *>(type)->element_type();
}

unsigned
v_type_vector_get_size(v_type_t *type)
{
    return static_cast<v_type_vector_t *>(type)->size();
}

bool
v_type_vector_is_scalable(v_type_t *type)
{
    return static_cast<v_type_vector_t *>(type)->is_scalable();
}


//---------------------------------------------------------------------
v_type_t *
v_generic_type(v_quark_t cons, v_type_generic_t::arg_t * const *args, unsigned count)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_generic_type(cons, args, count);
}

bool
v_type_is_generic(v_type_t *type)
{
    return bool(dynamic_cast<v_type_generic_t *>(type));
}

v_quark_t
v_type_generic_get_cons(v_type_t *type)
{
    return static_cast<v_type_generic_t *>(type)->cons();
}

unsigned
v_type_generic_get_arg_count(v_type_t *type)
{
    return static_cast<v_type_generic_t *>(type)->arg_count();
}

v_type_generic_t::arg_t * const *
v_type_generic_get_args(v_type_t *type)
{
    return static_cast<v_type_generic_t *>(type)->args();
}

//---------------------------------------------------------------------
int
v_type_generic_arg_get_kind(v_type_generic_t::arg_t *arg)
{
    return arg->kind();
}

//---------------------------------------------------------------------
v_type_generic_t::arg_t *
v_type_generic_number_arg(uint64_t num)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_number_arg(num);
}

bool
v_type_generic_arg_is_number(v_type_generic_t::arg_t *arg)
{
    return bool(dynamic_cast<v_type_generic_t::arg_number_t *>(arg));
}

uint64_t
v_type_generic_arg_number_get_number(v_type_generic_t::arg_t *arg)
{
    return static_cast<v_type_generic_t::arg_number_t *>(arg)->number();
}

//---------------------------------------------------------------------
v_type_generic_t::arg_t *
v_type_generic_string_arg(const std::string *str)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_string_arg(*str);
}

bool
v_type_generic_arg_is_string(v_type_generic_t::arg_t *arg)
{
    return bool(dynamic_cast<v_type_generic_t::arg_string_t *>(arg));
}

const std::string *
v_type_generic_arg_string_get_string(v_type_generic_t::arg_t *arg)
{
    return &static_cast<v_type_generic_t::arg_string_t *>(arg)->string();
}

//---------------------------------------------------------------------
v_type_generic_t::arg_t *
v_type_generic_quark_arg(v_quark_t q)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_quark_arg(q);
}

bool
v_type_generic_arg_is_quark(v_type_generic_t::arg_t *arg)
{
    return bool(dynamic_cast<v_type_generic_t::arg_quark_t *>(arg));
}

v_quark_t
v_type_generic_arg_quark_get_quark(v_type_generic_t::arg_t *arg)
{
    return static_cast<v_type_generic_t::arg_quark_t *>(arg)->quark();
}

//---------------------------------------------------------------------
v_type_generic_t::arg_t *
v_type_generic_type_arg(v_type_t *t)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_type_arg(t);
}

bool
v_type_generic_arg_is_type(v_type_generic_t::arg_t *arg)
{
    return bool(dynamic_cast<v_type_generic_t::arg_type_t *>(arg));
}

v_type_t *
v_type_generic_arg_type_get_type(v_type_generic_t::arg_t *arg)
{
    return static_cast<v_type_generic_t::arg_type_t *>(arg)->type();
}

//---------------------------------------------------------------------
v_type_generic_t::arg_t *
v_type_generic_cons_arg(v_quark_t cons, v_type_generic_t::arg_t * const *args, unsigned count)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_cons_arg(cons, args, count);
}

bool
v_type_generic_arg_is_cons(v_type_generic_t::arg_t *arg)
{
    return bool(dynamic_cast<v_type_generic_t::arg_cons_t *>(arg));
}

v_quark_t
v_type_generic_arg_cons_get_cons(v_type_generic_t::arg_t *arg)
{
    return static_cast<v_type_generic_t::arg_cons_t *>(arg)->cons();
}

unsigned
v_type_generic_arg_cons_get_arg_count(v_type_generic_t::arg_t *arg)
{
    return static_cast<v_type_generic_t::arg_cons_t *>(arg)->arg_count();
}

v_type_generic_t::arg_t * const *
v_type_generic_arg_cons_get_args(v_type_generic_t::arg_t *arg)
{
    return static_cast<v_type_generic_t::arg_cons_t *>(arg)->args();
}


//---------------------------------------------------------------------
hook_initialize_t
v_type_get_initialize_hook(int k, void **paux)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.get_initialize_hook(k, paux);
}

void
v_type_set_initialize_hook(int k, hook_initialize_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.set_initialize_hook(k, fun, aux);
}

hook_obtain_llvm_type_t
v_type_get_obtain_llvm_type_hook(int k, void **paux)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.get_obtain_llvm_type_hook(k, paux);
}

void
v_type_set_obtain_llvm_type_hook(int k, hook_obtain_llvm_type_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.set_obtain_llvm_type_hook(k, fun, aux);
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


