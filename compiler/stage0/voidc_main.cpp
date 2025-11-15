//---------------------------------------------------------------------
//- Copyright (C) 2020-2025 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_target.h"
#include "voidc_util.h"
#include "voidc_visitor.h"
#include "voidc_compiler.h"
#include "vpeg_context.h"
#include "vpeg_voidc.h"
#include "voidc_stdio.h"

#include <list>
#include <memory>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>

#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#include <share.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <windows.h>
#endif

#include <llvm-c/Core.h>


//---------------------------------------------------------------------
//- Some utility...
//---------------------------------------------------------------------
#ifndef NDEBUG

extern "C"
const char *__asan_default_options()
{
    return "handle_abort=1";
}

#endif


//---------------------------------------------------------------------
static ast_unit_t
parse_unit(void)
{
    auto &ctx = vpeg::context_data_t::current_ctx;

    static const auto unit_q = v_quark_from_string("unit");

    auto ret = ctx->grammar->parse(ctx->grammar, unit_q, ctx);

    ctx->memo.clear();

    if (auto unit = std::any_cast<ast_unit_t>(&ret))  return *unit;

    size_t column;

    auto line = ctx->get_line_column(ctx->get_buffer_size(), &column);

    auto &vctx = *voidc_global_ctx_t::voidc;        //- Sic!!!
    auto &lctx = *vctx.local_ctx;

    std::string msg = "Parse error in " + lctx.filename + ":"
                      + std::to_string(line) + ":"
                      + std::to_string(column);

    throw  std::runtime_error(msg);
}


//--------------------------------------------------------------------
namespace fs = std::filesystem;


//--------------------------------------------------------------------
static std::list<fs::path> import_paths;

#ifdef _WIN32
#define PATHSEP ';'
#else
#define PATHSEP ':'
#endif

void
import_paths_initialize(fs::path &exe_path)
{
    if (auto paths = std::getenv("VOIDC_IMPORT"))
    {
        while(paths)
        {
            fs::path path;

            if (auto p = std::strchr(paths, PATHSEP))
            {
                path = std::string(paths, p);

                paths = p + 1;
            }
            else
            {
                path = std::string(paths);

                paths = nullptr;
            }

            import_paths.push_back(path);
        }
    }
    else
    {
        import_paths = {"."};
    }

    if (fs::exists(exe_path))
    {
        fs::path p(exe_path);

        p = fs::canonical(p);
        p = p.parent_path();

        //- Dirty hack...

        import_paths.push_back(p / "../compiler/import");
        import_paths.push_back(p / "../library/import");
        import_paths.push_back(p / "..");
    }
}

#undef PATHSEP

//--------------------------------------------------------------------
static fs::path
find_file_for_import(const fs::path &parent, const fs::path &filename)
{
    fs::path ret;

    if (filename.is_relative())
    {
        auto p = parent / filename;

        if (fs::exists(p))
        {
            ret = p;
        }
        else
        {
            for (auto it : import_paths)
            {
                auto p = it / filename;

                if (fs::exists(p))
                {
                    if (fs::is_directory(p))
                    {
                        if (!fs::exists(p / "__import__.void")) continue;
                    }

                    ret = p;

                    break;
                }
            }
        }
    }
    else if (fs::exists(filename))
    {
        ret = filename;
    }

    if (fs::is_directory(ret))
    {
        ret = ret / "__import__.void";
    }

    if (fs::is_regular_file(ret))
    {
        return  fs::canonical(ret);
    }

    return  "";
}


//--------------------------------------------------------------------
static inline
std::FILE *
my_fopen(const fs::path &fpath, bool write=false)
{

#ifdef _WIN32

    return _wfopen(fpath.c_str(), (write ? L"wb" : L"rb"));

#else

    return std::fopen(fpath.c_str(), (write ? "wb" : "rb"));

#endif

}

static inline
void
my_fread(void* buf, size_t sz, size_t cn, std::FILE* f)
{
    auto dummy = std::fread(buf, sz, cn, f);
}


//--------------------------------------------------------------------
extern "C"
LLVMValueRef v_target_global_ctx_get_constant_value(base_global_ctx_t *, v_quark_t);

static fs::path
obtain_import_bin_filepath(base_global_ctx_t *gctx, const fs::path &src_filename)
{
    static const v_quark_t prefix_q  = v_quark_from_string("voidc.import_bin_filename_prefix");
    static const v_quark_t suffix_q  = v_quark_from_string("voidc.import_bin_filename_suffix");
    static const v_quark_t rewrite_q = v_quark_from_string("voidc.import_bin_filename_rewrite");

    const char *prefix = "__voidcache__/";      //- ?
    const char *suffix = "c";                   //- ?

    if (auto p = v_target_global_ctx_get_constant_value(gctx, prefix_q))  prefix = (const char *)p;
    if (auto s = v_target_global_ctx_get_constant_value(gctx, suffix_q))  suffix = (const char *)s;

    std::string bin_filename = prefix + src_filename.filename().generic_u8string() + suffix;

    if (auto *p = gctx->decls.intrinsics.find(rewrite_q))
    {
        typedef void (*rewrite_t)(void *aux, const std::string *parent, std::string *fname);

        auto *aux = p->second;
        auto *fun = rewrite_t(p->first);

        std::string parent = src_filename.parent_path().generic_u8string();

        fun(aux, &parent, &bin_filename);
    }

    return  fs::u8path(bin_filename);
}


//--------------------------------------------------------------------
static
const char magic[8] = ".voidc\n";

enum import_state_t
{
    ist_unknown,        //- ...
    ist_good,           //- Usable binary
    ist_bad             //- Need (re)compile
};

static
std::map<std::pair<std::string, std::string>, import_state_t> import_state;         //- ?

static bool
check_import_state(const fs::path &src_filepath, const fs::path &bin_filepath)
{
    auto src_filepath_str = src_filepath.generic_u8string();
    auto bin_filepath_str = bin_filepath.generic_u8string();

    {   auto it = import_state.find({src_filepath_str, bin_filepath_str});

        if (it != import_state.end())
        {
            return  (it->second != ist_bad);            //- "unknown" => kinda "good"...
        }
    }

    //- Perform check

    import_state[{src_filepath_str, bin_filepath_str}] = ist_unknown;

    fs::path parent_path = src_filepath.parent_path();

    fs::path bin_absolute;

    if (bin_filepath.is_absolute()) bin_absolute = bin_filepath;
    else                            bin_absolute = parent_path / bin_filepath;

    bool use_binary = true;

    fs::file_time_type bin_time;

    //- First, check bin_absolute itself

    if (!fs::exists(bin_absolute))
    {
        use_binary = false;
    }
    else
    {
        auto st  = fs::last_write_time(src_filepath);
        bin_time = fs::last_write_time(bin_absolute);

        if (st > bin_time)  use_binary = false;
    }

    if (!use_binary)
    {
        import_state[{src_filepath_str, bin_filepath_str}] = ist_bad;

        return false;
    }

    //- Second, check for header ...

    std::FILE *infs = my_fopen(bin_absolute);

    size_t buf_len = sizeof(magic);
    auto buf = std::make_unique<char[]>(buf_len);

    std::memset(buf.get(), 0, buf_len);

    my_fread(buf.get(), buf_len, 1, infs);

    if (std::strcmp(magic, buf.get()) != 0)
    {
        import_state[{src_filepath_str, bin_filepath_str}] = ist_bad;

        std::fclose(infs);

        return false;
    }

    //- Now, check for imports ...

    my_fread(buf.get(), sizeof(size_t), 1, infs);

    std::fseek(infs, *((size_t *)buf.get()), SEEK_SET);

    while(use_binary)
    {
        size_t len;

        my_fread(&len, sizeof(len), 1, infs);

        if (std::feof(infs))  break;        //- WTF ?!?!?

        if (len == 0)  break;

        if (buf_len < len)
        {
            buf = std::make_unique<char[]>(len);

            buf_len = len;
        }

        my_fread(buf.get(), len, 1, infs);

        std::string name(buf.get(), len);

        fs::path imp_filename = fs::u8path(name);

        imp_filename = find_file_for_import(parent_path, imp_filename);

        if (!fs::exists(imp_filename))
        {
            throw std::runtime_error("Import file not found: " + name);
        }

        my_fread(&len, sizeof(len), 1, infs);

        if (buf_len < len)
        {
            buf = std::make_unique<char[]>(len);

            buf_len = len;
        }

        my_fread(buf.get(), len, 1, infs);

        std::string bstr(buf.get(), len);

        fs::path bfil = fs::u8path(bstr);

        use_binary = check_import_state(imp_filename, bfil);

        if (use_binary)
        {
            fs::path bin_impfile;

            if (bfil.is_absolute()) bin_impfile = bfil;
            else                    bin_impfile = imp_filename.parent_path() / bfil;

            auto bt = fs::last_write_time(bin_impfile);

            if (bt > bin_time)  use_binary = false;
        }
    }

    std::fclose(infs);

    import_state[{src_filepath_str, bin_filepath_str}] = (use_binary ? ist_good : ist_bad);

    return use_binary;
}


//--------------------------------------------------------------------
//- ...
//--------------------------------------------------------------------
struct out_binary_t
{
    explicit out_binary_t(const fs::path &filename)
      : out_path(filename),
        tmp_path(filename)
    {
        fs::create_directories(filename.parent_path());         //- Sic !?!

        tmp_path += "_XXXXXX";

        fs::path::string_type tmp_name = tmp_path.native();

#ifdef _WIN32

        int fd = -1;

        static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

        int len = int(tmp_name.size());

        auto *template_name = tmp_name.data();

        for (int i=0; i >= 0; i++)
        {
            for (int j=len-6; j < len; j++)
            {
                template_name[j] = letters[std::rand()/(RAND_MAX/61)];
            }

            fd = _wsopen(template_name,
                         _O_RDWR|_O_CREAT|_O_EXCL|_O_BINARY,
                         _SH_DENYRW,
                         _S_IREAD|_S_IWRITE
                        );

            if (fd != -1  ||  errno != EEXIST) break;
        }

        assert(fd != -1);       //- ?..

#else

        int fd = mkstemp(tmp_name.data());

#endif

        tmp_path = tmp_name.c_str();

        f = ::fdopen(fd, "wb");
    }

    ~out_binary_t()
    {
        std::fclose(f);

        fs::rename(tmp_path, out_path);
    }

public:
    FILE *f = nullptr;

private:
    const fs::path out_path;

    fs::path tmp_path;
};


//--------------------------------------------------------------------
//- Intrinsics (functions)
//--------------------------------------------------------------------
static bool trace_imports = false;

static void
v_import_helper(const char *name, bool _export)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    auto *parent_lctx = static_cast<voidc_local_ctx_t *>(vctx.local_ctx);

    fs::path src_filepath = fs::u8path(name);

    fs::path parent_path = fs::path(parent_lctx->filename).parent_path();

    src_filepath = find_file_for_import(parent_path, src_filepath);

    auto src_filepath_str = src_filepath.generic_u8string();

    if (!fs::exists(src_filepath))
    {
        throw std::runtime_error(std::string("Import file not found: ") + name);
    }

    base_compile_ctx_t::export_data_t *export_data = nullptr;

    auto &tctx = *voidc_global_ctx_t::target;

    fs::path bin_filepath = obtain_import_bin_filepath(&tctx, src_filepath);

    auto bin_filepath_str = bin_filepath.generic_u8string();

    parent_lctx->imports.insert({name, bin_filepath_str});

    auto *target_lctx = tctx.local_ctx;

    {   auto [it, ok] = tctx.imported.try_emplace(src_filepath_str);

        if (!ok)    //- Already imported
        {
            auto res_i = target_lctx->imported.insert(src_filepath_str);

            auto &s = it->second;

            if (res_i.second)
            {
                target_lctx->decls.insert(s.first);             //- Insert declarations

                for (auto &e : s.second)  e.first(e.second);    //- Run(!) efforts...
            }

            if (_export  &&  target_lctx->export_data)
            {
                auto res_e = target_lctx->exported.insert(src_filepath_str);

                if (res_e.second)
                {
                    auto &d = *target_lctx->export_data;

                    d.first.insert(s.first);                            //- Insert declarations

                    for (auto &e : s.second)  d.second.push_back(e);    //- Insert efforts...
                }
            }

            return;     //- Sic!
        }

        export_data = &it->second;
    }

    {   bool use_binary = check_import_state(src_filepath, bin_filepath);

        fs::path bin_absolute;

        if (bin_filepath.is_absolute()) bin_absolute = bin_filepath;
        else                            bin_absolute = src_filepath.parent_path() / bin_filepath;

        std::FILE *infs;

        voidc_local_ctx_t lctx(vctx);

        lctx.filename = src_filepath_str;

        if (&tctx == &vctx)         //- ?!?!?!?!?!?!?!?!?!?!?!?!?!?!
        {
            lctx.export_data = export_data;
        }

        if (use_binary)
        {
            infs = my_fopen(bin_absolute);

            size_t buf_len = sizeof(magic);
            auto buf = std::make_unique<char[]>(buf_len);

            std::memset(buf.get(), 0, buf_len);

            my_fread(buf.get(), buf_len, 1, infs);

            assert(std::strcmp(magic, buf.get()) == 0);

            std::fseek(infs, sizeof(size_t), SEEK_CUR);         //- Skip pointer to imports

            auto parent_vpeg_ctx = vpeg::context_data_t::current_ctx;

            vpeg::context_data_t::current_ctx = nullptr;

            while(!std::feof(infs))
            {
                size_t len;

                my_fread(&len, sizeof(len), 1, infs);

                if (std::feof(infs))  break;        //- WTF ?!?!?

                if (len == 0)  break;

                if (buf_len < len)
                {
                    buf = std::make_unique<char[]>(len);

                    buf_len = len;
                }

                my_fread(buf.get(), len, 1, infs);

                lctx.unit_buffer = LLVMCreateMemoryBufferWithMemoryRange(buf.get(), len, "unit_buffer", false);

                lctx.run_unit_action();

                LLVMDisposeMemoryBuffer(lctx.unit_buffer);

                lctx.unit_buffer = nullptr;
            }

            vpeg::context_data_t::current_ctx = parent_vpeg_ctx;
        }
        else        //- !use_binary
        {
            if (trace_imports)  printf("start:  %s\n", src_filepath_str.c_str());

            infs = my_fopen(src_filepath);

            out_binary_t out_binary(bin_absolute);

            const auto &outfs = out_binary.f;

            {   char buf[sizeof(magic)];

                std::memset(buf, 0, sizeof(magic));

                std::fwrite(buf, sizeof(magic), 1, outfs);

                std::fwrite(buf, sizeof(size_t), 1, outfs);         //- Pointer to imports...
            }

            {   auto parent_vpeg_ctx = vpeg::context_data_t::current_ctx;

                auto grm = make_level_0_voidc_grammar();

                vpeg::context_data_t::current_ctx = std::make_shared<vpeg::context_data_t>(infs, grm);

                while(auto unit = parse_unit())
                {
                    voidc_visitor_data_t::visit(lctx.compiler, unit);

                    unit.reset();

                    if (lctx.unit_buffer)   lctx.run_unit_action();

                    if (lctx.unit_buffer)   //- Sic!
                    {
                        size_t len = LLVMGetBufferSize(lctx.unit_buffer);

                        std::fwrite((char *)&len, sizeof(len), 1, outfs);

                        std::fwrite(LLVMGetBufferStart(lctx.unit_buffer), len, 1, outfs);

                        LLVMDisposeMemoryBuffer(lctx.unit_buffer);

                        lctx.unit_buffer = nullptr;
                    }
                }

                vpeg::context_data_t::current_ctx = parent_vpeg_ctx;
            }

            size_t len = 0;

            std::fwrite((char *)&len, sizeof(len), 1, outfs);       //- End of units

            size_t imports_pos = std::ftell(outfs);

            for (auto &it : lctx.imports)
            {
                auto &src_name = it.first;

                len = src_name.length();

                std::fwrite((char *)&len, sizeof(len), 1, outfs);

                std::fwrite(src_name.data(), len, 1, outfs);

                auto &bin_name = it.second;

                len = bin_name.length();

                std::fwrite((char *)&len, sizeof(len), 1, outfs);

                std::fwrite(bin_name.data(), len, 1, outfs);
            }

            len = 0;

            std::fwrite((char *)&len, sizeof(len), 1, outfs);       //- End of imports

            //- Finish it...

            std::fseek(outfs, 0, SEEK_SET);

            std::fwrite(magic, sizeof(magic), 1, outfs);

            std::fwrite((char *)&imports_pos, sizeof(imports_pos), 1, outfs);

            if (trace_imports)  printf("finish: %s\n", src_filepath_str.c_str());
        }

        std::fclose(infs);
    }

    //- ...

    target_lctx->decls.insert(export_data->first);              //- Insert declarations

    for (auto &e : export_data->second)  e.first(e.second);     //- Run(!) efforts...

    target_lctx->imported.insert(src_filepath_str);

    if (_export  &&  target_lctx->export_data)
    {
        target_lctx->exported.insert(src_filepath_str);

        auto &d = *target_lctx->export_data;

        d.first.insert(export_data->first);                             //- Insert declarations

        for (auto &e : export_data->second) d.second.push_back(e);      //- Insert efforts...
    }
}

//--------------------------------------------------------------------
static void
voidc_import_helper(const char *name, bool _export)
{
    auto voidc  = voidc_global_ctx_t::voidc;
    auto target = voidc_global_ctx_t::target;

    if (target == voidc)
    {
        v_import_helper(name, _export);

        return;
    }

    //- target != voidc

    voidc_global_ctx_t::target = voidc;

    v_import_helper(name, _export);

    voidc_global_ctx_t::target = target;
}


//--------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION

//--------------------------------------------------------------------
void
v_export_import(const char *name)
{
    v_import_helper(name, true);
}

//--------------------------------------------------------------------
void
voidc_export_import(const char *name)
{
    voidc_import_helper(name, true);
}

//--------------------------------------------------------------------
void
v_import(const char *name)
{
    v_import_helper(name, false);
}

//--------------------------------------------------------------------
void
voidc_import(const char *name)
{
    voidc_import_helper(name, false);
}


//--------------------------------------------------------------------
void
voidc_guard_target(const char *errmsg)
{
    if (voidc_global_ctx_t::target == voidc_global_ctx_t::voidc)  return;

    throw  std::runtime_error(std::string("voidc_guard_target: ") + errmsg);
}


//--------------------------------------------------------------------
void
v_find_file_for_import(std::string *ret, const char *parent, const char *filename)
{
    auto _parent   = fs::u8path(parent);
    auto _filename = fs::u8path(filename);

    auto path = find_file_for_import(_parent, _filename);

    *ret = path.generic_u8string();
}


VOIDC_DLLEXPORT_END

}   //- extern "C"


//--------------------------------------------------------------------
static void
voidc_flush_output(void)
{
    fflush(stdout);     //- WTF?
    fflush(stderr);     //- WTF?
}


//--------------------------------------------------------------------
//- As is...
//--------------------------------------------------------------------
int
main(int argc, char *argv[])
{
    std::ios::sync_with_stdio(true);

//    (void)setvbuf(stdout, NULL, _IONBF, 0);
//    (void)setvbuf(stderr, NULL, _IONBF, 0);

    std::setbuf(stdout, nullptr);
    std::setbuf(stderr, nullptr);

    std::atexit(voidc_flush_output);

#ifdef _WIN32
    std::srand(std::time(nullptr));
#endif

    {   fs::path exe_path;

#ifdef _WIN32
        wchar_t path[_MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, path, _MAX_PATH);

        exe_path = path;
#else
        exe_path = "/proc/self/exe";
#endif

        import_paths_initialize(exe_path);
    }

    std::list<std::string> sources;

    while (optind < argc)
    {
        char c;

        if ((c = getopt(argc, argv, "-I:s:T")) != -1)
        {
            //- Option argument

            switch (c)
            {
            case 'I':
                import_paths.push_back(optarg);
                break;

            case 's':
                sources.push_back(optarg);
                c = -1;
                break;

            case 'T':
                trace_imports = true;
                break;

            case 1:
                sources.push_back(optarg);
                break;

            default:
                break;
            }
        }

        if (c == -1)  break;
    }

    if (sources.empty())  sources.push_back("-");

    voidc_global_ctx_t::static_initialize();

    auto &gctx = *voidc_global_ctx_t::voidc;

    utility::static_initialize();

    {   v_type_t *import_f_type = gctx.make_function_type(gctx.void_type, &gctx.char_ptr_type, 1, false);

        auto q = v_quark_from_string;

        gctx.decls.symbols_insert({q("v_export_import"),     import_f_type});
        gctx.decls.symbols_insert({q("voidc_export_import"), import_f_type});
        gctx.decls.symbols_insert({q("v_import"),            import_f_type});
        gctx.decls.symbols_insert({q("voidc_import"),        import_f_type});
        gctx.decls.symbols_insert({q("voidc_guard_target"),  import_f_type});       //- Kind of...

        v_type_t *fun_type = gctx.make_function_type(gctx.void_type, nullptr, 0, false);

        gctx.decls.symbols_insert({q("voidc_clear_unit_buffer"), fun_type});

#ifdef _WIN32

        static FILE *my_stdout = stdout;                    //- WTF !?!
        static FILE *my_stderr = stderr;                    //- WTF !?!

        gctx.add_symbol_value(q("stdout"), &my_stdout);
        gctx.add_symbol_value(q("stderr"), &my_stderr);

        gctx.add_symbol_value(q("getopt"), (void *)&getopt);
        gctx.add_symbol_value(q("optind"), &optind);
        gctx.add_symbol_value(q("optarg"), &optarg);
#endif

        gctx.add_symbol_value(q("voidc_argc"), &argc);
        gctx.add_symbol_value(q("voidc_argv"), &argv);

        gctx.add_symbol_value(q("voidc_import_paths"), &import_paths);      //- Sic!
    }

    v_ast_static_initialize();
    voidc_visitor_data_t::static_initialize();
    vpeg::grammar_data_t::static_initialize();
    vpeg::context_data_t::static_initialize();

    voidc_stdio_static_initialize();

    voidc_global_ctx_t::voidc->flush_unit_symbols();

    make_level_0_target_compiler();         //- Sic !!!

    {   vpeg::grammar_t current_grammar = make_level_0_voidc_grammar();

        voidc_local_ctx_t lctx(gctx);

        for (auto &src : sources)
        {
            std::string src_name = src;

            std::FILE *istr;

            if (src == "-")
            {
                src_name = "<stdin>";

                istr = stdin;
            }
            else
            {
                fs::path src_path = fs::u8path(src_name);

                src_path = fs::canonical(src_path);

                if (!fs::exists(src_path))
                {
                    throw std::runtime_error("Source file not found: " + src_name);
                }

                istr = my_fopen(src_path);

                src_name = src_path.generic_u8string();
            }

            lctx.filename = src_name;

            auto &grm = current_grammar;

            vpeg::context_data_t::current_ctx = std::make_shared<vpeg::context_data_t>(istr, grm);

            {   auto &ctx = vpeg::context_data_t::current_ctx;

                static const auto shebang_q = v_quark_from_string("shebang");

                if (ctx->grammar->parsers.find(shebang_q))
                {
                    ctx->grammar->parse(ctx->grammar, shebang_q, ctx);

                    ctx->memo.clear();
                }
            }

            while(auto unit = parse_unit())
            {
                voidc_visitor_data_t::visit(lctx.compiler, unit);

                unit.reset();

                lctx.run_unit_action();

                LLVMDisposeMemoryBuffer(lctx.unit_buffer);

                lctx.unit_buffer = nullptr;
            }

            current_grammar = vpeg::context_data_t::current_ctx->grammar;

            vpeg::context_data_t::current_ctx = nullptr;     //- ?

            if (src != "-")   std::fclose(istr);
        }
    }

    voidc_stdio_static_terminate();

    vpeg::context_data_t::static_terminate();
    vpeg::grammar_data_t::static_terminate();
    voidc_visitor_data_t::static_terminate();
    v_ast_static_terminate();

    utility::static_terminate();
    voidc_global_ctx_t::static_terminate();

    return 0;
}


