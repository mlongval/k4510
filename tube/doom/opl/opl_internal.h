//
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//     OPL internal interface.
//


#ifndef OPL_INTERNAL_H
#define OPL_INTERNAL_H

#include "opl.h"

typedef int (*opl_init_func)(unsigned int port_base);
typedef void (*opl_shutdown_func)(void);
typedef unsigned int (*opl_read_port_func)(opl_port_t port);
typedef void (*opl_write_port_func)(opl_port_t port, unsigned int value);
typedef void (*opl_set_callback_func)(uint64_t us,
                                      opl_callback_t callback,
                                      void *data);
typedef void (*opl_clear_callbacks_func)(void);
typedef void (*opl_lock_func)(void);
typedef void (*opl_unlock_func)(void);
typedef void (*opl_set_paused_func)(int paused);
typedef void (*opl_adjust_callbacks_func)(float value);

typedef struct
{
    const char *name;

    opl_init_func init_func;
    opl_shutdown_func shutdown_func;
    opl_read_port_func read_port_func;
    opl_write_port_func write_port_func;
    opl_set_callback_func set_callback_func;
    opl_clear_callbacks_func clear_callbacks_func;
    opl_lock_func lock_func;
    opl_unlock_func unlock_func;
    opl_set_paused_func set_paused_func;
    opl_adjust_callbacks_func adjust_callbacks_func;
} opl_driver_t;

/* [K4510] the machine's driver: it does not synthesise anything, it hands the
 * register writes to MELODY -- the emulator's YM3812 -- through the shared
 * segment DOOM already uses for its frames.  See opl_k4510.c. */
extern opl_driver_t opl_k4510_driver;

// Sample rate to use when doing software emulation.

extern unsigned int opl_sample_rate;


#if (defined(__i386__) || defined(__x86_64__)) && defined(HAVE_IOPERM)
extern opl_driver_t opl_linux_driver;
#endif
#if defined(HAVE_LIBI386) || defined(HAVE_LIBAMD64)
extern opl_driver_t opl_openbsd_driver;
#endif
#ifdef _WIN32
extern opl_driver_t opl_win32_driver;
#endif
/* [K4510] opl_sdl.c is not vendored -- it synthesises the sound itself, which
 * is MELODY's job here -- so nothing defines this.  Left declared, unused, to
 * keep the file diffable against upstream. */
extern opl_driver_t opl_sdl_driver;


#endif /* #ifndef OPL_INTERNAL_H */

