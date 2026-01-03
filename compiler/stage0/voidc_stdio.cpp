//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_stdio.h"

#include "voidc_target.h"
#include "voidc_quark.h"
#include "voidc_dllexport.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>

#include <stdarg.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#include <share.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <windows.h>
#endif


//--------------------------------------------------------------------
namespace fs = std::filesystem;


//--------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION

//--------------------------------------------------------------------
std::FILE *
v_fopen(const char *filename, const char *prop)
{

#ifdef _WIN32

    fs::path fpath = fs::u8path(filename);

    int len = std::strlen(prop);

    auto wprop = std::make_unique<wchar_t[]>(len+1);

    for (int i=0; i<=len; ++i)  wprop[i] = (wchar_t)prop[i];        //- ASCII only!

    return _wfopen(fpath.c_str(), wprop.get());

#else

    return std::fopen(filename, prop);

#endif

}

//--------------------------------------------------------------------
int v_fclose(std::FILE *f) { return std::fclose(f); }
int v_fflush(std::FILE *f) { return std::fflush(f); }

//--------------------------------------------------------------------
std::FILE *
v_popen(const char *command, const char *prop)
{
    return ::popen(command, prop);
}

//--------------------------------------------------------------------
int v_pclose(std::FILE *p) { return ::pclose(p); }

//--------------------------------------------------------------------
int
v_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vprintf(format, args);
    va_end(args);

    return ret;
}

//--------------------------------------------------------------------
int
v_scanf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vscanf(format, args);
    va_end(args);

    return ret;
}

//--------------------------------------------------------------------
int
v_fprintf(std::FILE *f, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vfprintf(f, format, args);
    va_end(args);

    return ret;
}

//--------------------------------------------------------------------
int
v_fscanf(std::FILE *f, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vfscanf(f, format, args);
    va_end(args);

    return ret;
}

//--------------------------------------------------------------------
int
v_snprintf(char *s, size_t n, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(s, n, format, args);
    va_end(args);

    return ret;
}

//--------------------------------------------------------------------
int
v_sprintf(char *s, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsprintf(s, format, args);
    va_end(args);

    return ret;
}

//--------------------------------------------------------------------
int
v_sscanf(const char *s, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsscanf(s, format, args);
    va_end(args);

    return ret;
}

//--------------------------------------------------------------------
int v_fgetc(std::FILE *f) { return std::fgetc(f); }
char *v_fgets(char *s, int n, std::FILE *f) { return std::fgets(s, n, f); }
int v_fputc(int c, std::FILE *f) { return std::fputc(c, f); }
int v_fputs(const char *s, std::FILE *f) { return std::fputs(s, f); }

int v_getc(std::FILE *f) { return std::getc(f); }
int v_getchar(void) { return std::getchar(); }
int v_putc(int c, std::FILE *f) { return std::putc(c, f); }
int v_putchar(int c) { return std::putchar(c); }
int v_puts(const char *s) { return std::puts(s); }

int v_ungetc(int c, std::FILE *f) { return std::ungetc(c, f); }

//--------------------------------------------------------------------
size_t
v_fread(void *buf, size_t sz, size_t nm, std::FILE *f)
{
    return std::fread(buf, sz, nm, f);
}

//--------------------------------------------------------------------
size_t
v_fwrite(const void *buf, size_t sz, size_t nm, std::FILE *f)
{
    return std::fwrite(buf, sz, nm, f);
}

//--------------------------------------------------------------------
int v_feof(std::FILE *f) { return std::feof(f); }
int v_ferror(std::FILE *f) { return std::ferror(f); }

void v_perror(const char *s) { std::perror(s); }


VOIDC_DLLEXPORT_END

}   //- extern "C"


//--------------------------------------------------------------------
//- ...
//--------------------------------------------------------------------
void voidc_stdio_static_initialize(void)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    auto q = v_quark_from_string;

#define DEF(name) \
    vctx.decls.aliases_insert({ q(#name), q("v_" #name) });

    DEF(fopen)
    DEF(fclose)
    DEF(fflush)
    DEF(popen)
    DEF(pclose)
    DEF(printf)
    DEF(scanf)
    DEF(fprintf)
    DEF(fscanf)
    DEF(snprintf)
    DEF(sprintf)
    DEF(sscanf)
    DEF(fgetc)
    DEF(fgets)
    DEF(fputc)
    DEF(fputs)
    DEF(getc)
    DEF(getchar)
    DEF(putc)
    DEF(putchar)
    DEF(puts)
    DEF(ungetc)
    DEF(fread)
    DEF(fwrite)
    DEF(feof)
    DEF(ferror)
    DEF(perror)

#undef DEF

    //- ...

}

//--------------------------------------------------------------------
void voidc_stdio_static_terminate(void) {}


