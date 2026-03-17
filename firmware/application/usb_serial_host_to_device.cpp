/*
 * Copyright (C) 2023 Bernd Herzog
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

#include "usb_serial_host_to_device.hpp"
#include "event_m0.hpp"
#include "usb_serial_device_to_host.h"

extern "C" {
#include <common/usb.h>
#include <hackrf_usb/usb_device.h>
#include <hackrf_usb/usb_endpoint.h>
}

#include <queue>
#include <vector>

static Thread* thread_usb_event = NULL;
usb_serial_input_handler_t usb_serial_active_input_handler = nullptr;

struct usb_bulk_buffer_t {
    uint8_t* data;
    volatile size_t length;
    volatile bool completed;
};

std::queue<usb_bulk_buffer_t*> usb_bulk_buffer_queue;
std::queue<usb_bulk_buffer_t*> usb_bulk_buffer_spare;

void serial_bulk_transfer_complete(void* user_data, unsigned int bytes_transferred) {
    usb_bulk_buffer_t* transfer_data = (usb_bulk_buffer_t*)user_data;

    transfer_data->length = bytes_transferred;
    transfer_data->completed = true;
}

void init_host_to_device() {
    thread_usb_event = chThdSelf();
}

void reset_transfer_queues() {
    chSysLock();
    while (!usb_bulk_buffer_queue.empty()) {
        usb_bulk_buffer_t* p = usb_bulk_buffer_queue.front();
        usb_bulk_buffer_queue.pop();
        chSysUnlock();
        delete[] p->data;
        delete p;
        chSysLock();
    }

    while (!usb_bulk_buffer_spare.empty()) {
        usb_bulk_buffer_t* p = usb_bulk_buffer_spare.front();
        usb_bulk_buffer_spare.pop();
        chSysUnlock();
        delete[] p->data;
        delete p;
        chSysLock();
    }
    chSysUnlock();
}

void schedule_host_to_device_transfer() {
    chSysLock();
    if (usb_bulk_buffer_queue.size() >= 8) {
        chSysUnlock();
        return;
    }
    chSysUnlock();

    static usb_bulk_buffer_t* transfer_data = nullptr;

    int ret;

    do {
        if (transfer_data == nullptr) {
            chSysLock();
            if (!usb_bulk_buffer_spare.empty()) {
                transfer_data = usb_bulk_buffer_spare.front();
                transfer_data->length = 0;
                transfer_data->completed = false;
                usb_bulk_buffer_spare.pop();
                chSysUnlock();
            } else {
                chSysUnlock();
                transfer_data = new usb_bulk_buffer_t{
                    .data = new uint8_t[USB_BULK_BUFFER_SIZE],
                    .length = 0,
                    .completed = false};
            }
        }

        ret = usb_transfer_schedule(
            &usb_endpoint_bulk_out,
            transfer_data->data,
            USB_BULK_BUFFER_SIZE,
            serial_bulk_transfer_complete,
            transfer_data);

        if (ret != -1) {
            chSysLock();
            usb_bulk_buffer_queue.push(transfer_data);
            transfer_data = nullptr;

            const bool queue_full = usb_bulk_buffer_queue.size() >= 8;
            chSysUnlock();
            if (queue_full)
                return;
        }
    } while (ret != -1);
}

void complete_host_to_device_transfer() {
    while (true) {
        chSysLock();
        if (usb_bulk_buffer_queue.empty()) {
            chSysUnlock();
            break;
        }
        
        usb_bulk_buffer_t* transfer_data = usb_bulk_buffer_queue.front();
        chSysUnlock();

        while (transfer_data->completed == false)
            return;

        chSysLock();
        if (usb_serial_active_input_handler) {
            // An input handler is active: route raw bytes directly to it
            chSysUnlock();
            usb_serial_active_input_handler(transfer_data->data, transfer_data->length);
        } else {
            // Normal operation: feed bytes into the shell iqueue
            // Check if iqueue has enough free space for the entire buffer to avoid
            // blocking the event loop when shell thread is busy with SD card I/O.
            int iqueue_free = USBSERIAL_BUFFERS_SIZE - chIQGetFullI(&SUSBD1.iqueue);
            
            if (iqueue_free < (int)transfer_data->length) {
                // Not enough space; return early and retry on next event loop iteration.
                // This prevents the event loop from blocking and allows USB responsiveness
                // even when the shell thread is stalled on slow SD card operations.
                chSysUnlock();
                return;
            }
            
            // Safe to add all bytes since we verified space availability
            for (unsigned int i = 0; i < transfer_data->length; i++) {
                msg_t ret = chIQPutI(&SUSBD1.iqueue, transfer_data->data[i]);
                // Space was pre-verified, so this should not fail; ignore ret
                (void)ret;
            }
            chSysUnlock();
        }

        chSysLock();
        usb_bulk_buffer_queue.pop();
        usb_bulk_buffer_spare.push(transfer_data);
        chSysUnlock();
    }
}
