/* Minimal unistd.h shim so upstream XEphem libastro compiles with MSVC.
 * libastro/bdl.c includes <unistd.h> but uses only stdio, so nothing
 * else is needed here. */
#ifndef NEOX_COMPAT_UNISTD_H
#define NEOX_COMPAT_UNISTD_H
#include <io.h>
#include <process.h>
#endif
