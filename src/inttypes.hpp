#pragma once

#include <cstdint>

typedef int8_t i8;
typedef uint8_t u8;
typedef int16_t i16;
typedef uint16_t u16;
#if defined(TH07_PSP)
// newlib's PSP headers define int32_t as long even though both long and int
// are 32-bit.  The original code relies on i32 being the built-in int type
// for postfix operators, std::min/max deduction, and printf formats.
typedef int i32;
typedef uint32_t u32;
typedef long long i64;
typedef unsigned long long u64;
#else
typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;
#endif
typedef float f32;
typedef double f64;

static_assert(sizeof(i32) == 4 && sizeof(u32) == 4, "TH07 requires 32-bit i32/u32");
static_assert(sizeof(i64) == 8 && sizeof(u64) == 8, "TH07 requires 64-bit i64/u64");
