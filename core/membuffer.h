// SPDX-License-Identifier: GPL-2.0
/*
 * Helper functions used to deal with string manipulation
 * 'membuffer' functions will manage memory allocation avoiding performance
 * issues related to superfluous re-allocation. See 'make_room' function
 *
 * Before using it membuffer struct should be properly initialized
 *
 *     struct membuffer mb = { 0 };
 *
 * Internal membuffer buffer will not by default contain null terminator,
 * adding it should be done using 'mb_cstring' function
 *
 *     mb_cstring(&mb);
 *
 * String concatenation is done with consecutive calls to put_xxx functions
 *
 *     put_string(&mb, "something");
 *     put_string(&mb, ", something else");
 *     printf("%s", mb_cstring(&mb));
 *
 * Will result in
 *
 *     "something, something else"
 *
 * Unless ownership to the buffer is given away say to a caller
 *
 *     mb_cstring(&mb);
 *     return detach_buffer(&mb);
 *
 * or via a callback
 *
 *     mb_cstring(&mb);
 *     cb(detach_buffer(&mb));
 *
 * otherwise allocated memory should be freed
 *
 *     free_buffer(&mb);
 */
#ifndef MEMBUFFER_H
#define MEMBUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ctype.h>
#include "units.h"

struct membuffer {
	unsigned int len, alloc;
	char *buffer;
};

#ifdef __GNUC__
#define __printf(x, y) __attribute__((__format__(__printf__, x, y)))
#else
#define __printf(x, y)
#endif

extern char *detach_buffer(struct membuffer *b);
extern void free_buffer(struct membuffer *);
extern void make_room(struct membuffer *b, unsigned int size);
extern void flush_buffer(struct membuffer *, FILE *);
extern void put_bytes(struct membuffer *, const char *, int);
extern void put_string(struct membuffer *, const char *);
extern void put_quoted(struct membuffer *, const char *, int, int);
extern void strip_mb(struct membuffer *);
extern const char *mb_cstring(struct membuffer *);
extern __printf(2, 0) void put_vformat(struct membuffer *, const char *, va_list);
extern __printf(2, 0) void put_vformat_loc(struct membuffer *, const char *, va_list);
extern __printf(2, 3) void put_format(struct membuffer *, const char *fmt, ...);
extern __printf(2, 3) void put_format_loc(struct membuffer *, const char *fmt, ...);
extern __printf(2, 0) char *add_to_string_va(const char *old, const char *fmt, va_list args);
extern __printf(2, 3) char *add_to_string(const char *old, const char *fmt, ...);

/* Helpers that use membuffers internally */
extern __printf(1, 0) char *vformat_string(const char *, va_list);
extern __printf(1, 2) char *format_string(const char *, ...);


/* Output one of our "milli" values with type and pre/post data */
extern void put_milli(struct membuffer *, const char *, int, const char *);

/*
 * Helper functions for showing particular types. If the type
 * is empty, nothing is done, and the function returns false.
 * Otherwise, it returns true.
 *
 * The two "const char *" at the end are pre/post data.
 *
 * The reason for the pre/post data is so that you can easily
 * prepend and append a string without having to test whether the
 * type is empty. So
 *
 *     put_temperature(b, temp, "Temp=", " C\n");
 *
 * writes nothing to the buffer if there is no temperature data,
 * but otherwise would a string that looks something like
 *
 *     "Temp=28.1 C\n"
 *
 * to the memory buffer (typically the post/pre will be some XML
 * pattern and unit string or whatever).
 */
extern void put_temperature(struct membuffer *, temperature_t, const char *, const char *);
extern void put_depth(struct membuffer *, depth_t, const char *, const char *);
extern void put_duration(struct membuffer *, duration_t, const char *, const char *);
extern void put_pressure(struct membuffer *, pressure_t, const char *, const char *);
extern void put_salinity(struct membuffer *, int, const char *, const char *);
extern void put_degrees(struct membuffer *b, degrees_t value, const char *, const char *);

#ifdef __cplusplus
}
#endif

#endif
