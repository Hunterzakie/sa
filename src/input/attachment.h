/*****************************************************************************
 * attachment.h: attachments carrying an object
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
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

#ifndef LIBVLC_INPUT_ATTACHMENT_H
#define LIBVLC_INPUT_ATTACHMENT_H 1

#include <vlc_input.h>

/**
 * Create an attachment carrying an object rather than data
 *
 * p_data points to the object and i_data is zero: code reading the data of
 * the attachments sees an empty one, and the object never leaves the process.
 *
 * @param object the object, handed over to the attachment on success
 * @param release function releasing the object along with the attachment; it
 * also tells the kinds of objects apart, see input_attachment_GetObject()
 * @return a new attachment, or NULL on allocation error
 */
input_attachment_t *input_attachment_NewObject(const char *name,
                                               const char *mime,
                                               const char *description,
                                               void *object,
                                               void (*release)(void *));

/**
 * Get the object carried by an attachment
 *
 * @param release the function the object was handed over with
 * @return the object, or NULL if the attachment was not created by
 * input_attachment_NewObject() with that release function, which is notably
 * the case of attachments read from a file, whatever their MIME type
 */
void *input_attachment_GetObject(const input_attachment_t *attachment,
                                 void (*release)(void *));

#endif
