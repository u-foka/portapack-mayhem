/*
 * Copyright (C) 2014 Jared Boone, ShareBrained Technology, Inc.
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "chibios_cpp.hpp"

#include <cstdint>

#include <ch.h>
#include <hal.h>

static void to_string_hex_internal(char* p, const uint64_t n, const int32_t l) {
    const uint32_t d = n & 0xf;
    p[l] = (d > 9) ? (d + 55) : (d + 48);
    if (l > 0) {
        to_string_hex_internal(p, n >> 4, l - 1);
    }
}

void* operator new(size_t size) {
    void* p = chHeapAlloc(0x0, size);
    
    if (p == nullptr)
    {
        regarm_t lr;
        asm volatile ("mov     %0, lr" : "=r" (lr) : : "memory");

        struct extctx* ctxp;
        if ((uint32_t)lr & 0x04)
            ctxp = (struct extctx*)__get_PSP();
        else
            ctxp = (struct extctx*)__get_MSP();

        char msg[16] = "OOM: 0x";
        char *p = msg + 7;
        to_string_hex_internal(p, (uint32_t)ctxp->pc, 8 - 1);
        msg[16] = 0;

        chDbgPanic(msg);
    }

    return p;
}

void* operator new[](size_t size) {
    void* p = chHeapAlloc(0x0, size);
    
    if (p == nullptr)
    {
        regarm_t lr;
        asm volatile ("mov     %0, lr" : "=r" (lr) : : "memory");

        struct extctx* ctxp;
        if ((uint32_t)lr & 0x04)
            ctxp = (struct extctx*)__get_PSP();
        else
            ctxp = (struct extctx*)__get_MSP();

        char msg[16] = "OOM: 0x";
        char *p = msg + 7;
        to_string_hex_internal(p, (uint32_t)ctxp->pc, 8 - 1);
        msg[16] = 0;

        chDbgPanic(msg);
    }

    return p;
}

void operator delete(void* p) noexcept {
    chHeapFree(p);
}

void operator delete[](void* p) noexcept {
    chHeapFree(p);
}

void operator delete(void* ptr, std::size_t) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    ::operator delete(ptr);
}

extern uint8_t __heap_base__[];
extern uint8_t __heap_end__[];

namespace chibios {

size_t heap_size() {
    return __heap_end__ - __heap_base__;
}

size_t heap_used() {
    const auto core_free = chCoreStatus();
    size_t heap_free = 0;
    chHeapStatus(NULL, &heap_free);
    return heap_size() - (core_free + heap_free);
}

} /* namespace chibios */
