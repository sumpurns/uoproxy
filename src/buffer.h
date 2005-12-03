/*
 * uoproxy
 * $Id$
 *
 * (c) 2005 Max Kellermann <max@duempel.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __BUFFER_H
#define __BUFFER_H

struct buffer {
    size_t max_length, length;
    unsigned char data[1];
};

struct buffer *buffer_new(size_t max_length);
void buffer_delete(struct buffer *b);

static inline size_t buffer_free(const struct buffer *b) {
    return b->max_length - b->length;
}

static inline int buffer_empty(const struct buffer *b) {
    return b->length == 0;
}

static inline unsigned char *buffer_tail(struct buffer *b) {
    return b->data + b->length;
}

static inline void buffer_expand(struct buffer *b, size_t nbytes) {
    assert(nbytes <= buffer_free(b));

    b->length += nbytes;
}

void buffer_append(struct buffer *b, const unsigned char *data,
                   size_t nbytes);

static inline unsigned char *buffer_peek(struct buffer *b,
                                         size_t *lengthp) {
    if (b->length == 0)
        return NULL;

    *lengthp = b->length;
    return b->data;
}

void buffer_remove_head(struct buffer *b, size_t nbytes);

#endif
