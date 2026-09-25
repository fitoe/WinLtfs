/* SPDX-License-Identifier: LGPL-2.1-only */
#ifndef LTOG_MAM_H
#define LTOG_MAM_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ltfs_error.h"

#define LTOG_MAM_COMMAND 0x83b
#define LTOG_MAM_BUFFER_SIZE (4u + 2u * 65536u)
#define LTOG_MAM_PAGE_SIZE 4032u

/* Native little-endian Windows wire layout. operation: 0=value, 1=list.
 * Partition is the physical partition number (0 or 1), not the LTFS letter.
 */
struct ltog_mam_request {
    uint32_t version;
    uint8_t operation;
    uint8_t partition;
    uint16_t attribute;
    uint32_t offset;
    uint32_t reserved;
};

static int ltog_mam_request_valid(const struct ltog_mam_request *r)
{
    return r->version == 1 && r->operation <= 1 && r->partition <= 1 &&
        !r->reserved && (!r->operation || !r->attribute) &&
        r->offset <= LTOG_MAM_BUFFER_SIZE - 4;
}

static unsigned int ltog_mam_be16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | p[1];
}

/* Validate transport lengths before exposing bytes. Value responses can include
 * subsequent descriptors; return only the exact requested attribute. Preserve
 * the five-byte descriptor header (ID, flags/format, length) and binary value.
 */
static int ltog_mam_payload(const unsigned char *raw, size_t received,
    const struct ltog_mam_request *r, size_t *length)
{
    uint32_t available;
    size_t i, n;
    if (received < 4 || received > LTOG_MAM_BUFFER_SIZE)
        return -LTFS_UNEXPECTED_VALUE;
    available = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) |
        ((uint32_t)raw[2] << 8) | raw[3];
    if (r->operation) {
        if ((available & 1) || available > received - 4)
            return -LTFS_UNEXPECTED_VALUE;
        for (i = 2; i < available; i += 2)
            if (ltog_mam_be16(raw + 4 + i) <= ltog_mam_be16(raw + 2 + i))
                return -LTFS_UNEXPECTED_VALUE;
        n = available;
    } else {
        if (!available)
            return -LTFS_NO_XATTR;
        if (available < 5 || received < 9)
            return -LTFS_UNEXPECTED_VALUE;
        if (ltog_mam_be16(raw + 4) != r->attribute)
            return -LTFS_NO_XATTR;
        n = 5u + ltog_mam_be16(raw + 7);
        if (n > available || n > received - 4)
            return -LTFS_UNEXPECTED_VALUE;
    }
    if (r->offset > n)
        return -LTFS_BAD_ARG;
    *length = n;
    return 0;
}
#endif
