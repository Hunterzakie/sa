/*****************************************************************************
 * android.c: Android OS-specific initialization
 *****************************************************************************
 * Copyright (C) 2016 VLC authors and VideoLAN
 * Copyright (C) 2026 Alexandre Janniaux <ajanni@videolabs.io>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <vlc_common.h>
#include <jni.h>

/**
 * \file android.c
 *
 * This file provides interoperability against the JavaVM setup by
 * Android by letting the Android JNI subsystem setup the JavaVM pointer
 * so that it can be used by the modules.
 *
 * Modules will need to link against the current file to get access to
 * the vlc_android_GetJVM() public function.
 */

static JavaVM *s_jvm = NULL;

VLC_EXPORT JavaVM* vlc_android_GetJVM(void);

JavaVM* vlc_android_GetJVM(void)
{
    return s_jvm;
}

/* Those function are called when the dynamic library is loaded via the
 * java.lang.System.loadLibrary method or unloaded. They must be exported with
 * default visibility so the Java runtime can resolve them with dlsym(). */

JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved);

JNIEXPORT void JNICALL
JNI_OnUnload(JavaVM* vm, void* reserved);

JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void) reserved;
    s_jvm = vm;
    return JNI_VERSION_1_2;
}


JNIEXPORT void JNICALL
JNI_OnUnload(JavaVM* vm, void* reserved)
{
    (void) vm; (void) reserved;
}


