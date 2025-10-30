#pragma once

#if defined(assert)
#undef assert
#endif

#include <thread>
#include <io.h>

#ifdef _DEBUG
#define mz_assert(cond, ...) \
    do {                \
        if (!(cond)) {  \
            int mode = _setmode(_fileno(stderr), 0x4000); \
            fprintf(stderr, "%s:%d: Assertion failed in function '%s': %s\n", __FILE__, __LINE__, __FUNCSIG__, #cond); \
            fprintf(stderr, "" __VA_ARGS__); \
            abort();    \
        }               \
    } while (false)
#else
#define mz_assert(cond, ...) \
    do {                \
        if (!(cond)) {  \
            int mode = _setmode(_fileno(stderr), 0x4000); \
            fprintf(stderr, "%s:%d: Assertion failed in function '%s': %s\n", __FILE__, __LINE__, __FUNCSIG__, #cond); \
            fprintf(stderr, "" __VA_ARGS__); \
            std::this_thread::sleep_for(std::chrono::seconds(5)); \
            abort();    \
        }               \
    } while (false)

#endif
