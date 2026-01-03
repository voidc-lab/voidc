//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_TYPES_H
#define VOIDC_TYPES_H

#include "voidc_quark.h"
#include "voidc_dllexport.h"

#include <cassert>
#include <utility>
#include <map>
#include <vector>
#include <memory>
#include <string>
#include <any>
#include <unordered_map>

#include <llvm-c/Types.h>


//---------------------------------------------------------------------
class voidc_types_ctx_t;


//---------------------------------------------------------------------
//- Base class for voidc`s own types
//---------------------------------------------------------------------
struct v_type_t
{
    virtual ~v_type_t();

    enum kind_t
    {
        k_void,         //-  0 - ...

        k_f16,          //-  1 - "half"
        k_f32,          //-  2 - "float"
        k_f64,          //-  3 - "double"
        k_f128,         //-  4 - "fp128"

        k_int,          //-  5 - Signed integer
        k_uint,         //-  6 - Unsigned integer

        k_function,     //-  7 - ...
        k_pointer,      //-  8 - Typed(!) pointer ((void *) - ok!)
        k_reference,    //-  9 - References...
        k_struct,       //- 10 - (Un-)named structs ("tuples" in fact)
        k_array,        //- 11 - ...

        k_vector,       //- 12 - Fixed vector
        k_svector,      //- 13 - Scalable vector

        k_generic,      //- 14 - ...

        k_count         //- 15 - Number of kinds...
    };

    virtual kind_t kind(void) const = 0;

    voidc_types_ctx_t &context;

protected:
    explicit v_type_t(voidc_types_ctx_t &ctx)
      : context(ctx)
    {}

    friend class voidc_types_ctx_t;

private:
    v_type_t(const v_type_t &) = delete;
    v_type_t &operator=(const v_type_t &) = delete;

private:
    inline
    LLVMTypeRef obtain_llvm_type(void) const;

public:
    mutable LLVMTypeRef cached_llvm_type = LLVMTypeRef(-1);         //- "Not initialized"

    LLVMTypeRef llvm_type(void)
    {
//      assert(cached_llvm_type != LLVMTypeRef(-1));

        if (cached_llvm_type != LLVMTypeRef(-2))  return cached_llvm_type;

        return  obtain_llvm_type();
    }
};

//---------------------------------------------------------------------
template<typename T, v_type_t::kind_t tag>
struct v_type_tag_t : public T
{
    v_type_t::kind_t kind(void) const override
    {
        return tag;
    }

protected:
    template<typename... Types>
    explicit v_type_tag_t(Types&&... args)
      : T(std::forward<Types>(args)...)
    {}

private:
    v_type_tag_t(const v_type_tag_t &) = delete;
    v_type_tag_t &operator=(const v_type_tag_t &) = delete;
};


//---------------------------------------------------------------------
//- Special tag-types ...
//---------------------------------------------------------------------
#define INVIOLABLE_TAG  (reinterpret_cast<v_type_t *>(intptr_t( 0)))
#define UNREFERENCE_TAG (reinterpret_cast<v_type_t *>(intptr_t(-1)))


//---------------------------------------------------------------------
//- "Simple" types: "void" and floating points...
//---------------------------------------------------------------------
template<v_type_t::kind_t tag>
class v_type_simple_t : public v_type_tag_t<v_type_t, tag>
{
    friend class voidc_types_ctx_t;

    explicit v_type_simple_t(voidc_types_ctx_t &ctx)
      : v_type_tag_t<v_type_t, tag>(ctx)
    {}

    v_type_simple_t(const v_type_simple_t &) = delete;
    v_type_simple_t &operator=(const v_type_simple_t &) = delete;
};

//---------------------------------------------------------------------
using v_type_void_t = v_type_simple_t<v_type_t::k_void>;
using v_type_f16_t  = v_type_simple_t<v_type_t::k_f16>;
using v_type_f32_t  = v_type_simple_t<v_type_t::k_f32>;
using v_type_f64_t  = v_type_simple_t<v_type_t::k_f64>;
using v_type_f128_t = v_type_simple_t<v_type_t::k_f128>;


//---------------------------------------------------------------------
//- Integer types: signed/unsigned
//---------------------------------------------------------------------
class v_type_integer_t : public v_type_t
{
    friend class voidc_types_ctx_t;

    const unsigned &bits;

protected:
    explicit v_type_integer_t(voidc_types_ctx_t &ctx, const unsigned &_bits)
      : v_type_t(ctx),
        bits(_bits)
    {}

private:
    v_type_integer_t(const v_type_integer_t &) = delete;
    v_type_integer_t &operator=(const v_type_integer_t &) = delete;

public:
    bool is_signed(void) const { return (kind() == k_int); }

    unsigned width(void) const { return bits; }
};

//---------------------------------------------------------------------
template<v_type_t::kind_t tag>
class v_type_integer_tag_t : public v_type_tag_t<v_type_integer_t, tag>
{
    friend class voidc_types_ctx_t;

    explicit v_type_integer_tag_t(voidc_types_ctx_t &ctx, const unsigned &_bits)
      : v_type_tag_t<v_type_integer_t, tag>(ctx, _bits)
    {}

    v_type_integer_tag_t(const v_type_integer_tag_t &) = delete;
    v_type_integer_tag_t &operator=(const v_type_integer_tag_t &) = delete;
};

//---------------------------------------------------------------------
using v_type_int_t  = v_type_integer_tag_t<v_type_t::k_int>;
using v_type_uint_t = v_type_integer_tag_t<v_type_t::k_uint>;


//---------------------------------------------------------------------
//- Function types
//---------------------------------------------------------------------
class v_type_function_t : public v_type_tag_t<v_type_t, v_type_t::k_function>
{
    friend class voidc_types_ctx_t;

    using key_t = std::pair<std::vector<v_type_t *>, bool>;

    const key_t &key;

    explicit v_type_function_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_tag_t(ctx),
        key(_key)
    {}

    v_type_function_t(const v_type_function_t &) = delete;
    v_type_function_t &operator=(const v_type_function_t &) = delete;

public:
    bool is_var_arg(void) const { return key.second; }

    v_type_t *return_type(void) const { return key.first[0]; }

    unsigned param_count(void) const { return unsigned(key.first.size() - 1); }

    v_type_t * const *param_types(void) const { return key.first.data() + 1; }
};


//---------------------------------------------------------------------
//- Pointer/reference types
//---------------------------------------------------------------------
class v_type_refptr_t : public v_type_t
{
    friend class voidc_types_ctx_t;

protected:
    using key_t = std::pair<v_type_t *, unsigned>;

    const key_t &key;

    explicit v_type_refptr_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_t(ctx),
        key(_key)
    {}

private:
    v_type_refptr_t(const v_type_refptr_t &) = delete;
    v_type_refptr_t &operator=(const v_type_refptr_t &) = delete;

public:
    v_type_t *element_type(void) const { return key.first; }

    unsigned address_space(void) const { return key.second; }

    bool is_reference(void) const { return (kind() == k_reference); }
};

//---------------------------------------------------------------------
template<v_type_t::kind_t tag>
class v_type_refptr_tag_t : public v_type_tag_t<v_type_refptr_t, tag>
{
    friend class voidc_types_ctx_t;

    explicit v_type_refptr_tag_t(voidc_types_ctx_t &ctx, const v_type_refptr_t::key_t &_key)
      : v_type_tag_t<v_type_refptr_t, tag>(ctx, _key)
    {}

    v_type_refptr_tag_t(const v_type_refptr_tag_t &) = delete;
    v_type_refptr_tag_t &operator=(const v_type_refptr_tag_t &) = delete;
};

//---------------------------------------------------------------------
using v_type_pointer_t   = v_type_refptr_tag_t<v_type_t::k_pointer>;
using v_type_reference_t = v_type_refptr_tag_t<v_type_t::k_reference>;


//---------------------------------------------------------------------
//- Structure types: named/unnamed...
//---------------------------------------------------------------------
class v_type_struct_t : public v_type_tag_t<v_type_t, v_type_t::k_struct>
{
    friend class voidc_types_ctx_t;

    using name_key_t = v_quark_t;
    using body_key_t = std::pair<std::vector<v_type_t *>, bool>;

    const name_key_t *name_key = nullptr;
    const body_key_t *body_key = nullptr;

    explicit v_type_struct_t(voidc_types_ctx_t &ctx, const name_key_t &_name)
      : v_type_tag_t(ctx),
        name_key(&_name)
    {}

    explicit v_type_struct_t(voidc_types_ctx_t &ctx, const body_key_t &_body)
      : v_type_tag_t(ctx),
        body_key(&_body)
    {}

    v_type_struct_t(const v_type_struct_t &) = delete;
    v_type_struct_t &operator=(const v_type_struct_t &) = delete;

public:
    v_quark_t name(void) const { return (name_key ? *name_key : 0); }

    bool is_opaque(void) const { return !body_key; }

    void set_body(v_type_t * const *elts, unsigned count, bool packed=false);

    unsigned element_count(void) const { return unsigned(body_key->first.size()); }

    v_type_t * const *element_types(void) const { return body_key->first.data(); }

    bool is_packed(void) const { return body_key->second; }
};


//---------------------------------------------------------------------
//- Array types
//---------------------------------------------------------------------
class v_type_array_t : public v_type_tag_t<v_type_t, v_type_t::k_array>
{
    friend class voidc_types_ctx_t;

    using key_t = std::pair<v_type_t *, uint64_t>;

    const key_t &key;

    explicit v_type_array_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_tag_t(ctx),
        key(_key)
    {}

    v_type_array_t(const v_type_array_t &) = delete;
    v_type_array_t &operator=(const v_type_array_t &) = delete;

public:
    v_type_t *element_type(void) const { return key.first; }

    uint64_t length(void) const { return key.second; }
};


//---------------------------------------------------------------------
//- Vector types: fixed/scalable
//---------------------------------------------------------------------
class v_type_vector_base_t : public v_type_t
{
    friend class voidc_types_ctx_t;

protected:
    using key_t = std::pair<v_type_t *, unsigned>;

    const key_t &key;

    explicit v_type_vector_base_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_t(ctx),
        key(_key)
    {}

private:
    v_type_vector_base_t(const v_type_vector_base_t &) = delete;
    v_type_vector_base_t &operator=(const v_type_vector_base_t &) = delete;

public:
    v_type_t *element_type(void) const { return key.first; }

    unsigned size(void) const { return key.second; }

    bool is_scalable(void) const { return (kind() == k_svector); }
};

//---------------------------------------------------------------------
template<v_type_t::kind_t tag>
class v_type_vector_tag_t : public v_type_tag_t<v_type_vector_base_t, tag>
{
    friend class voidc_types_ctx_t;

    explicit v_type_vector_tag_t(voidc_types_ctx_t &ctx, const v_type_vector_base_t::key_t &_key)
      : v_type_tag_t<v_type_vector_base_t, tag>(ctx, _key)
    {}

    v_type_vector_tag_t(const v_type_vector_tag_t &) = delete;
    v_type_vector_tag_t &operator=(const v_type_vector_tag_t &) = delete;
};

//---------------------------------------------------------------------
using v_type_vector_t  = v_type_vector_tag_t<v_type_t::k_vector>;
using v_type_svector_t = v_type_vector_tag_t<v_type_t::k_svector>;


//---------------------------------------------------------------------
//- Generic types
//---------------------------------------------------------------------
class v_type_generic_t : public v_type_tag_t<v_type_t, v_type_t::k_generic>
{
public:
    class arg_t;
    class arg_number_t;
    class arg_string_t;
    class arg_quark_t;
    class arg_type_t;
    class arg_cons_t;

private:
    friend class voidc_types_ctx_t;

    using key_t = std::pair<v_quark_t, std::vector<arg_t *>>;

    const key_t &key;

    explicit v_type_generic_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_tag_t(ctx),
        key(_key)
    {}

    v_type_generic_t(const v_type_generic_t &) = delete;
    v_type_generic_t &operator=(const v_type_generic_t &) = delete;

public:
    v_quark_t cons(void) const { return key.first; }

    unsigned arg_count(void) const { return unsigned(key.second.size()); }

    arg_t * const *args(void) const { return key.second.data(); }
};

//---------------------------------------------------------------------
struct v_type_generic_t::arg_t
{
    virtual ~arg_t();

    enum kind_t
    {
        k_number,       //-  0 - ...
        k_string,       //-  1 - ...
        k_quark,        //-  2 - ...
        k_type,         //-  3 - ...
        k_cons,         //-  4 - ...

        k_count         //-  5 - Number of kinds...
    };

    virtual kind_t kind(void) const = 0;

    voidc_types_ctx_t &context;

protected:
    explicit arg_t(voidc_types_ctx_t &ctx)
      : context(ctx)
    {}

    friend class voidc_types_ctx_t;

private:
    arg_t(const arg_t &) = delete;
    arg_t &operator=(const arg_t &) = delete;
};

//---------------------------------------------------------------------
template<v_type_generic_t::arg_t::kind_t tag>
struct v_type_generic_arg_tag_t : public v_type_generic_t::arg_t
{
    v_type_generic_t::arg_t::kind_t kind(void) const override
    {
        return tag;
    }

protected:
    explicit v_type_generic_arg_tag_t(voidc_types_ctx_t &ctx)
      : v_type_generic_t::arg_t(ctx)
    {}

private:
    v_type_generic_arg_tag_t(const v_type_generic_arg_tag_t &) = delete;
    v_type_generic_arg_tag_t &operator=(const v_type_generic_arg_tag_t &) = delete;
};

//---------------------------------------------------------------------
class v_type_generic_t::arg_number_t : public v_type_generic_arg_tag_t<arg_t::k_number>
{
    friend class voidc_types_ctx_t;

    using key_t = uint64_t;

    const key_t &key;

    explicit arg_number_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_generic_arg_tag_t(ctx),
        key(_key)
    {}

    arg_number_t(const arg_number_t &) = delete;
    arg_number_t &operator=(const arg_number_t &) = delete;

public:
    uint64_t number(void) const { return key; }
};

//---------------------------------------------------------------------
class v_type_generic_t::arg_string_t : public v_type_generic_arg_tag_t<arg_t::k_string>
{
    friend class voidc_types_ctx_t;

    using key_t = std::string;

    const key_t &key;

    explicit arg_string_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_generic_arg_tag_t(ctx),
        key(_key)
    {}

    arg_string_t(const arg_string_t &) = delete;
    arg_string_t &operator=(const arg_string_t &) = delete;

public:
    const std::string &string(void) const { return key; }
};

//---------------------------------------------------------------------
class v_type_generic_t::arg_quark_t : public v_type_generic_arg_tag_t<arg_t::k_quark>
{
    friend class voidc_types_ctx_t;

    using key_t = v_quark_t;

    const key_t &key;

    explicit arg_quark_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_generic_arg_tag_t(ctx),
        key(_key)
    {}

    arg_quark_t(const arg_quark_t &) = delete;
    arg_quark_t &operator=(const arg_quark_t &) = delete;

public:
    v_quark_t quark(void) const { return key; }
};

//---------------------------------------------------------------------
class v_type_generic_t::arg_type_t : public v_type_generic_arg_tag_t<arg_t::k_type>
{
    friend class voidc_types_ctx_t;

    using key_t = v_type_t *;

    const key_t &key;

    explicit arg_type_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_generic_arg_tag_t(ctx),
        key(_key)
    {}

    arg_type_t(const arg_type_t &) = delete;
    arg_type_t &operator=(const arg_type_t &) = delete;

public:
    v_type_t *type(void) const { return key; }
};

//---------------------------------------------------------------------
class v_type_generic_t::arg_cons_t : public v_type_generic_arg_tag_t<arg_t::k_cons>
{
    friend class voidc_types_ctx_t;

    using key_t = std::pair<v_quark_t, std::vector<arg_t *>>;

    const key_t &key;

    explicit arg_cons_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_generic_arg_tag_t(ctx),
        key(_key)
    {}

    arg_cons_t(const arg_cons_t &) = delete;
    arg_cons_t &operator=(const arg_cons_t &) = delete;

public:
    v_quark_t cons(void) const { return key.first; }

    unsigned arg_count(void) const { return unsigned(key.second.size()); }

    arg_t * const *args(void) const { return key.second.data(); }
};


//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION


v_type_t *v_type_get_scalar_type(v_type_t *type);

bool     v_type_is_floating_point(v_type_t *type);
unsigned v_type_floating_point_get_width(v_type_t *type);

bool     v_type_is_integer(v_type_t *type);
bool     v_type_integer_is_signed(v_type_t *type);
unsigned v_type_integer_get_width(v_type_t *type);

VOIDC_DLLEXPORT_END


//---------------------------------------------------------------------
typedef void (*hook_initialize_t)(void *aux, v_type_t *typ);

typedef LLVMTypeRef (*hook_obtain_llvm_type_t)(void *aux, const v_type_t *typ);


//---------------------------------------------------------------------
}   //- extern "C"


//=====================================================================
//- Context of types...
//=====================================================================
class voidc_types_ctx_t
{
    bool _is_initialized = false;

    friend class base_global_ctx_t;

public:
    voidc_types_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size);
    ~voidc_types_ctx_t();

    const bool &is_initialized = _is_initialized;

public:
    const LLVMContextRef llvm_ctx;

    const LLVMTypeRef opaque_struct_type;

public:
    v_type_void_t      *make_void_type(void);

    v_type_f16_t       *make_f16_type(void);
    v_type_f32_t       *make_f32_type(void);
    v_type_f64_t       *make_f64_type(void);
    v_type_f128_t      *make_f128_type(void);

    v_type_int_t       *make_int_type(unsigned bits);
    v_type_uint_t      *make_uint_type(unsigned bits);

    v_type_function_t  *make_function_type(v_type_t *ret, v_type_t * const *args, unsigned count, bool var_args=false);

    v_type_pointer_t   *make_pointer_type  (v_type_t *et, unsigned addr_space=0);
    v_type_reference_t *make_reference_type(v_type_t *et, unsigned addr_space=0);

    v_type_struct_t    *make_struct_type(v_quark_t name);
    v_type_struct_t    *make_struct_type(v_type_t * const *elts, unsigned count, bool packed=false);

    v_type_array_t     *make_array_type(v_type_t *et, uint64_t count);

    v_type_vector_t    *make_vector_type(v_type_t *et, unsigned count);
    v_type_svector_t   *make_svector_type(v_type_t *et, unsigned count);

    v_type_generic_t   *make_generic_type(v_quark_t cons, v_type_generic_t::arg_t * const *args, unsigned count);

    v_type_generic_t::arg_number_t *make_number_arg(uint64_t num);
    v_type_generic_t::arg_string_t *make_string_arg(const std::string &str);
    v_type_generic_t::arg_quark_t  *make_quark_arg(v_quark_t q);
    v_type_generic_t::arg_type_t   *make_type_arg(v_type_t *t);
    v_type_generic_t::arg_cons_t   *make_cons_arg(v_quark_t cons, v_type_generic_t::arg_t * const *args, unsigned count);

protected:
    std::unique_ptr<v_type_void_t> _void_type;

    std::unique_ptr<v_type_f16_t>  _f16_type;
    std::unique_ptr<v_type_f32_t>  _f32_type;
    std::unique_ptr<v_type_f64_t>  _f64_type;
    std::unique_ptr<v_type_f128_t> _f128_type;

    template <typename T> inline
    T *check_cached_llvm_type(T *t);

    template <typename T> inline
    T *make_type_helper(std::unique_ptr<T> &tptr);

    template <typename T, typename K = typename T::key_t>
    using types_map_t = std::map<K, std::unique_ptr<T>>;

    template <typename T, typename K = typename T::key_t> inline
    T *make_type_helper(types_map_t<T, K> &tmap, const K &key);

    types_map_t<v_type_int_t,  unsigned> int_types;
    types_map_t<v_type_uint_t, unsigned> uint_types;

    types_map_t<v_type_function_t> function_types;

    types_map_t<v_type_pointer_t>   pointer_types;
    types_map_t<v_type_reference_t> reference_types;

    types_map_t<v_type_struct_t, v_type_struct_t::name_key_t> named_struct_types;
    types_map_t<v_type_struct_t, v_type_struct_t::body_key_t> anon_struct_types;

    types_map_t<v_type_array_t> array_types;

    types_map_t<v_type_vector_t>  vector_types;
    types_map_t<v_type_svector_t> svector_types;

    types_map_t<v_type_generic_t> generic_types;

    template <typename T, typename K = typename T::key_t> inline
    T *make_arg_helper(types_map_t<T, K> &tmap, const K &key);

    types_map_t<v_type_generic_t::arg_number_t>  number_args;
    types_map_t<v_type_generic_t::arg_string_t>  string_args;
    types_map_t<v_type_generic_t::arg_quark_t>   quark_args;
    types_map_t<v_type_generic_t::arg_type_t>    type_args;
    types_map_t<v_type_generic_t::arg_cons_t>    cons_args;

public:
    hook_initialize_t get_initialize_hook(int k, void **paux);
    void set_initialize_hook(int k, hook_initialize_t fun, void *aux);

    hook_obtain_llvm_type_t get_obtain_llvm_type_hook(int k, void **paux);
    void set_obtain_llvm_type_hook(int k, hook_obtain_llvm_type_t fun, void *aux);

public:
    v_type_t * const void_type;

    v_type_t * const bool_type;
    v_type_t * const char_type;
    v_type_t * const short_type;
    v_type_t * const int_type;
    v_type_t * const unsigned_type;
    v_type_t * const long_type;
    v_type_t * const long_long_type;
    v_type_t * const intptr_t_type;
    v_type_t * const size_t_type;
    v_type_t * const char32_t_type;
    v_type_t * const uint64_t_type;

    v_type_t * const static_type_type;          //- v_static_type_t !
};


//---------------------------------------------------------------------
inline
LLVMTypeRef
v_type_t::obtain_llvm_type(void) const
{
    void *aux;
    auto *fun = context.get_obtain_llvm_type_hook(kind(), &aux);

    return fun(aux, this);
}


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void voidc_types_static_initialize(void);
void voidc_types_static_terminate(void);

void voidc_types_make_voidc_constants(void);

//---------------------------------------------------------------------
class base_global_ctx_t;

void voidc_types_make_level_0_intrinsics(base_global_ctx_t &gctx);


#endif  //- VOIDC_TYPES_H
