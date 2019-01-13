#include <zjunix/type.h>
#include <driver/sd.h>

#ifndef VFS_DEBUG
/* Read/Write block for FAT (starts from first block of partition 1) */
u32 read_block(u8 *buf, u32 addr, u32 count) {
    return sd_read_block(buf, addr, count);
}

u32 write_block(u8 *buf, u32 addr, u32 count) {
    return sd_write_block(buf, addr, count);
}

/* char to u16/u32 */
u16 get_u16(u8 *ch) {
    return (*ch) + ((*(ch + 1)) << 8);
}

u32 get_u32(u8 *ch) {
    return (*ch) + ((*(ch + 1)) << 8) + ((*(ch + 2)) << 16) + ((*(ch + 3)) << 24);
}

/* u16/u32 to char */
void set_u16(u8 *ch, u16 num) {
    *ch = (u8)(num & 0xFF);
    *(ch + 1) = (u8)((num >> 8) & 0xFF);
}

void set_u32(u8 *ch, u32 num) {
    *ch = (u8)(num & 0xFF);
    *(ch + 1) = (u8)((num >> 8) & 0xFF);
    *(ch + 2) = (u8)((num >> 16) & 0xFF);
    *(ch + 3) = (u8)((num >> 24) & 0xFF);
}

#endif
/* work around */
u32 fs_wa(u32 num) {
    // return the bits of `num`
    u32 i;
    for (i = 0; num > 1; num >>= 1, i++)
        ;
    return i;
}

/* path convertion */
u32 fs_next_slash(u8 *f, u8 *output11)
{
    u32 i, j, k;
    u8 chr11[13];
    for (i = 0; (*(f + i) != 0) && (*(f + i) != '/'); i++)
        ;

    for (j = 0; j < 12; j++) {
        chr11[j] = 0;
        output11[j] = 0x20;
    }
    for (j = 0; j < 12 && j < i; j++) {
        chr11[j] = *(f + j);
        if (chr11[j] >= 'a' && chr11[j] <= 'z')
            chr11[j] = (u8)(chr11[j] - 'a' + 'A');
    }
    chr11[12] = 0;

    for (j = 0; (chr11[j] != 0) && (j < 12); j++) {
        if (chr11[j] == '.')
            break;

        output11[j] = chr11[j];
    }

    if (chr11[j] == '.') {
        j++;
        for (k = 8; (chr11[j] != 0) && (j < 12) && (k < 11); j++, k++) {
            output11[k] = chr11[j];
        }
    }

    output11[11] = 0;

    return i;
}
