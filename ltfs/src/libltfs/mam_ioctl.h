/* SPDX-License-Identifier: LGPL-2.1-only */
#ifndef MAM_IOCTL_H
#define MAM_IOCTL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ltfs_error.h"

#define MAM_IOCTL_COMMAND 0x83b
#define MAM_IOCTL_BUFFER_SIZE (4u + 2u * 65536u)
#define MAM_IOCTL_PAGE_SIZE 4032u
#define MAM_IOCTL_VALUE_SIZE (4u + 5u + 65535u)

/* Native little-endian Windows wire layout. operation: 0=value, 1=list.
 * Partition is the physical partition number (0 or 1), not the LTFS letter.
 */
struct mam_ioctl_request {
    uint32_t version;
    uint8_t operation;
    uint8_t partition;
    uint16_t attribute;
    uint32_t offset;
    uint32_t reserved;
};

static int mam_ioctl_request_valid(const struct mam_ioctl_request *r)
{
    return r->version == 1 && r->operation <= 1 && r->partition <= 1 &&
        !r->reserved && (!r->operation || !r->attribute) &&
        r->offset <= MAM_IOCTL_BUFFER_SIZE - 4;
}

static size_t mam_ioctl_alloc(const struct mam_ioctl_request *r)
{
    size_t size = 4u + r->offset + MAM_IOCTL_PAGE_SIZE;
    size_t max = r->operation ? MAM_IOCTL_BUFFER_SIZE : MAM_IOCTL_VALUE_SIZE;
    return size < max ? size : max;
}

static unsigned int mam_ioctl_be16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | p[1];
}

/* Validate transport lengths before exposing bytes. Value responses can include
 * subsequent descriptors; return only the exact requested attribute. Preserve
 * the five-byte descriptor header (ID, flags/format, length) and binary value.
 */
static int mam_ioctl_payload(const unsigned char *raw, size_t received,
    const struct mam_ioctl_request *r, size_t *length)
{
    uint32_t available;
    size_t i, n, need;
    if (received < 4 || received > MAM_IOCTL_BUFFER_SIZE)
        return -LTFS_UNEXPECTED_VALUE;
    available = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) |
        ((uint32_t)raw[2] << 8) | raw[3];
    if (r->operation) {
        if ((available & 1) || available > MAM_IOCTL_BUFFER_SIZE - 4)
            return -LTFS_UNEXPECTED_VALUE;
        n = available;
    } else {
        if (!available)
            return -LTFS_NO_XATTR;
        if (available < 5 || received < 9)
            return -LTFS_UNEXPECTED_VALUE;
        if (mam_ioctl_be16(raw + 4) != r->attribute)
            return -LTFS_NO_XATTR;
        n = 5u + mam_ioctl_be16(raw + 7);
        if (n > available)
            return -LTFS_UNEXPECTED_VALUE;
    }
    /* Reads are sized to this page, so the drive may truncate the rest. */
    need = r->offset + MAM_IOCTL_PAGE_SIZE;
    if (need > n)
        need = n;
    if (need > received - 4)
        return -LTFS_UNEXPECTED_VALUE;
    if (r->operation)
        for (i = 2; i + 2 <= need; i += 2)
            if (mam_ioctl_be16(raw + 4 + i) <= mam_ioctl_be16(raw + 2 + i))
                return -LTFS_UNEXPECTED_VALUE;
    if (r->offset > n)
        return -LTFS_BAD_ARG;
    *length = n;
    return 0;
}
#endif
