/**
 * \file config.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief libnetconf2 various configuration settings.
 *
 * Copyright (c) 2015 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#ifndef NC_CONFIG_H_
#define NC_CONFIG_H_

/*
 * Mark all objects as hidden and export only objects explicitly marked to be part of the public API.
 */
#define API __attribute__((visibility("default")))

#ifdef __GNUC__
#  define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#  define UNUSED(x) UNUSED_ ## x
#endif

/*
 * Support for spinlocks
 */
#define HAVE_SPINLOCK
#ifndef HAVE_SPINLOCK
#  define pthread_spinlock_t pthread_mutex_t
#  define pthread_spin_init(s, opt) pthread_mutex_init(s, NULL)
#  define pthread_spin_lock pthread_mutex_lock
#  define pthread_spin_trylock pthread_mutex_trylock
#  define pthread_spin_unlock pthread_mutex_unlock
#  define pthread_spin_destroy pthread_mutex_destroy
#endif

/*
 * support for pthread_mutex_timedlock
 */
#define HAVE_PTHREAD_MUTEX_TIMEDLOCK

/*
 * Location of installed basic YIN/YANG schemas
 */
#define SCHEMAS_DIR "/workspace/device-simulator/build/external/libnetconf2/share/libnetconf2"

/*
 * Partial message read timeout in seconds
 * (also used as nc_pollsession lock timeout and internal <get-schema> RPC reply timeout)
 */
#define NC_READ_TIMEOUT 30

#endif /* NC_CONFIG_H_ */
