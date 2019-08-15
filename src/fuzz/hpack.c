#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>

#include <hpack/vhp.h>

void hpack_fuzzer(void* data, size_t len)
{
    struct vhd_decode dec;
    VHD_Init(&dec);

    struct vht_table vht;
    VHT_Init(&vht, 256);

    // enum vhd_ret_e  VHD_Decode(struct vhd_decode *, struct vht_table *,
    //     const uint8_t *in, size_t inlen, size_t *p_inused,
    //     char *out, size_t outlen, size_t *p_outused);
    size_t inlen = 0;
    char out[65536];
    size_t outlen = 0;
    VHD_Decode(&dec, &vht, data, len, &inlen, out, sizeof(out), &outlen);
}
