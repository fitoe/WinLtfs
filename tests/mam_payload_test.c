/* Compile: gcc -Wall -Wextra -Werror -Iltfs/src/libltfs
 * tests/mam_payload_test.c -o build/mam_payload_test.exe */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "ltog_mam.h"

static void header(unsigned char *b, unsigned int n)
{
    b[0] = n >> 24; b[1] = n >> 16; b[2] = n >> 8; b[3] = n;
}

int main(void)
{
    unsigned char *b = calloc(1, LTOG_MAM_BUFFER_SIZE);
    struct ltog_mam_request r = {1, 0, 0, 0, 0, 0};
    size_t n = 999;
    unsigned int i;
    assert(b && sizeof(r) == 16);
    assert(ltog_mam_request_valid(&r));
    r.partition = 2; assert(!ltog_mam_request_valid(&r)); r.partition = 0;
    r.reserved = 1; assert(!ltog_mam_request_valid(&r)); r.reserved = 0;
    r.version = 2; assert(!ltog_mam_request_valid(&r)); r.version = 1;
    r.operation = 2; assert(!ltog_mam_request_valid(&r)); r.operation = 0;
    r.attribute = 1; assert(!ltog_mam_request_valid(&r)); r.attribute = 0;
    assert(ltog_mam_payload(b, 3, &r, &n) == -LTFS_UNEXPECTED_VALUE);
    assert(!ltog_mam_payload(b, 4, &r, &n) && n == 0);
    header(b, 3);
    assert(ltog_mam_payload(b, 7, &r, &n) == -LTFS_UNEXPECTED_VALUE);
    header(b, 4); b[6] = 8;
    assert(ltog_mam_payload(b, 7, &r, &n) == -LTFS_UNEXPECTED_VALUE);
    assert(!ltog_mam_payload(b, 8, &r, &n) && n == 4);
    b[6] = 0;
    assert(ltog_mam_payload(b, 8, &r, &n) == -LTFS_UNEXPECTED_VALUE);
    /* Largest possible ID list: all 65536 IDs, including 0 and vendor IDs. */
    header(b, 131072);
    for (i = 0; i < 65536; ++i) { b[4 + i*2] = i >> 8; b[5 + i*2] = i; }
    assert(!ltog_mam_payload(b, LTOG_MAM_BUFFER_SIZE, &r, &n) && n == 131072);
    r.operation = 1; r.attribute = 0xf100;
    header(b, 5); b[4] = 0xf1; b[5] = 0; b[6] = 0x80; b[7] = b[8] = 0;
    assert(!ltog_mam_payload(b, 9, &r, &n) && n == 5); /* empty value */
    r.attribute++;
    assert(ltog_mam_payload(b, 9, &r, &n) == -LTFS_NO_XATTR);
    r.attribute--;
    header(b, 0);
    assert(ltog_mam_payload(b, 4, &r, &n) == -LTFS_NO_XATTR);
    header(b, 65540); b[7] = b[8] = 255;
    assert(ltog_mam_payload(b, 65543, &r, &n) == -LTFS_UNEXPECTED_VALUE);
    assert(!ltog_mam_payload(b, 65544, &r, &n) && n == 65540);
    r.offset = 64512;
    assert(!ltog_mam_payload(b, 65544, &r, &n) && n - r.offset == 1028);
    r.offset = 65540;
    assert(!ltog_mam_payload(b, 65544, &r, &n));
    r.offset++;
    assert(ltog_mam_payload(b, 65544, &r, &n) == -LTFS_BAD_ARG);
    r.offset = 0;
    header(b, 1000000); /* later descriptors may exceed the allocation */
    assert(!ltog_mam_payload(b, 65544, &r, &n) && n == 65540);
    free(b);
    puts("MAM request and payload boundary tests passed");
    return 0;
}
