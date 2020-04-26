#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct buffer {
    size_t size, count, p;
    char *buffer;
};

struct hdf5 {
    FILE *out;
    struct buffer *buf;
    struct {
        size_t file_offset, eof_loc;
    } super_block;
};

struct buffer *
buffer_create(void)
{
    struct buffer *out = malloc(sizeof(*out));
    out->count  = out->p = 0;
    out->size   = 65536;
    out->buffer = malloc(out->size);
    return out;
}

void
buffer_destroy(struct buffer **b)
{
    free((*b)->buffer);
    free(*b);
    *b = NULL;
}

void
buffer_flush(struct buffer *b, struct hdf5 *h5)
{
    fwrite(b->buffer, b->count, 1, h5->out);
    b->count = b->p = 0;
}

size_t
buffer_grow(struct buffer *b, size_t size)
{
    size_t new_size = b->size;
    do
        new_size *= 2;
    while (new_size < (b->p + size));
    return new_size;
}

int
buffer_write(struct buffer *b, const void *ptr, size_t size)
{
    if ((b->p + size) > b->size) {
        size_t new_size = buffer_grow(b, b->p + size);
        char *tmp = realloc(b->buffer, new_size);
        if (tmp == NULL)
            return ENOMEM;
        b->buffer = tmp;
        b->size   = new_size;
    }
    memcpy(b->buffer + b->p, ptr, size);
    b->p += size;
    if (b->count < b->p)
        b->count = b->p;
    return 0;
}

int
buffer_transfer(struct buffer *dst, struct beffer *src)
{
    int out = buffer_write(dst, src->buffer, src->count);
    if (out == ENOMEM)
        return out;
    src->count = src->p = 0;
    return 0;
}

void
buffer_seek(struct buffer *b, size_t dest)
{
    b->p = dest <= b->count ? dest : b->count;
}

void
buffer_seek_end(struct buffer *b)
{
    b->p = b->count;
}

size_t
buffer_tell(struct buffer *b)
{
    return b->p;
}

size_t
file_and_buffer_tell(struct hdf5 *h5)
{
    return (buffer_tell(h5->buf) + ftell(h5->out));
}

const char *MAT_HEADER =
    "MATLAB 7.3 MAT-file, "
    "Created by: APL_MATWRITE";
const char *DATESTR =
    "Created on: %a %b %d %H:%M:%S %Y "
    "HDF5 schema 1.00 .";
const uint16_t VERSION = 0x0200;
const uint16_t ENDIAN  = 0x4d49;
const uint64_t PUSH    = 0;

const char *SB_SIG =
    "\x89HDF\x0d\x0a\x1a\x0a";
const uint8_t SB_VER    = 0x00;
const uint8_t FFSS_VER  = 0x00;
const uint8_t ROOT_STE  = 0x00;
const uint8_t RES_8     = 0x00;
const uint8_t SHM_VER   = 0x00;
const uint8_t OFF       = 0x08;
const uint8_t LEN       = 0x08;
const uint16_t LEAF_K   = 0x0004;
const uint16_t INT_K    = 0x0010;
const uint32_t SB_FLAGS = 0x00000000;
const size_t UNDEF = 0xFFFFFFFFFFFFFFFF;

const uint64_t ROOT_LNO = 0x0000000000000000;
const uint64_t ROOT_OHA = 0x0000000000000060;
const uint32_t ROOT_CACHE = 0x00000001;
const uint32_t RES_32 = 0x00000000;
const uint64_t ROOT_BTREE = 0x0000000000000088;
const uint64_t ROOT_HEAP = 0x00000000000002A8;

struct hdf5 *
hdf5_create(FILE *outfile)
{
    struct hdf5 *out = malloc(sizeof(*out));
    out->out = outfile;
    out->buf = buffer_create();

    // Matlab file header
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char datebuf[124];
    strftime(datebuf, sizeof(datebuf), DATESTR, tm);
    char header[124] = {0};
    sprintf(header, "%s%s%s", MAT_HEADER, strlen(datebuf) ? ", " : "", datebuf);

    buffer_write(out->buf, header, sizeof(header));
    buffer_write(out->buf, &VERSION, sizeof(VERSION));
    buffer_write(out->buf, &ENDIAN, sizeof(ENDIAN));
    for (;;) {
        buffer_write(out->buf, &PUSH, sizeof(PUSH));
        if (out->buf->count >= 512)
            break;
    }

    // HDF5 Superblock
    out->super_block.file_offset = buffer_tell(out->buf);
    buffer_write(out->buf, SB_SIG, strlen(SB_SIG));
    buffer_write(out->buf, &SB_VER, sizeof(SB_VER));
    buffer_write(out->buf, &FFSS_VER, sizeof(FFSS_VER));
    buffer_write(out->buf, &ROOT_STE, sizeof(ROOT_STE));
    buffer_write(out->buf, &RES_8, sizeof(RES_8));
    buffer_write(out->buf, &SHM_VER, sizeof(SHM_VER));
    buffer_write(out->buf, &OFF, sizeof(OFF));
    buffer_write(out->buf, &LEN, sizeof(LEN));
    buffer_write(out->buf, &RES_8, sizeof(RES_8));
    buffer_write(out->buf, &LEAF_K, sizeof(LEAF_K));
    buffer_write(out->buf, &INT_K, sizeof(INT_K));
    buffer_write(out->buf, &SB_FLAGS, sizeof(SB_FLAGS));
    buffer_write(out->buf, &out->super_block.file_offset, sizeof(out->super_block.file_offset));
    buffer_write(out->buf, &UNDEF, sizeof(UNDEF));
    out->super_block.eof_loc = buffer_tell(out->buf);
    buffer_write(out->buf, &UNDEF, sizeof(UNDEF));
    buffer_write(out->buf, &UNDEF, sizeof(UNDEF));

    // Root group symbol table entry
    buffer_write(out->buf, &ROOT_LNO, sizeof(ROOT_LNO));
    buffer_write(out->buf, &ROOT_OHA, sizeof(ROOT_OHA));
    buffer_write(out->buf, &ROOT_CACHE, sizeof(ROOT_CACHE));
    buffer_write(out->buf, &RES_32, sizeof(RES_32));
    buffer_write(out->buf, &ROOT_BTREE, sizeof(ROOT_BTREE));
    buffer_write(out->buf, &ROOT_HEAP, sizeof(ROOT_HEAP));
    
    buffer_flush(out->buf, out);

    return out;
}

struct message {
    uint16_t msg_type;
    struct buffer *buf;
};

struct group {
    uint16_t num_obj_msg;
    struct message *msg;
};

void
group_load(struct group *g, unsigned type)
{
    switch (type) {
    default: //case 0:
        g->num_obj_msg = 1;
        g->msg = malloc(sizeof(g->msg[0]));
        g->msg->msg_type = 0x0011;
        g->msg->buf = buffer_create();
        buffer_write(g->msg->buf, &ROOT_BTREE, sizeof(ROOT_BTREE));
        buffer_write(g->msg->buf, &ROOT_HEAP, sizeof(ROOT_HEAP));
        break;
    }
}

struct group *
group_create(unsigned type)
{
    // type will determine components of group
    struct group *out = malloc(sizeof(*out));
    group_load(out, type);
    return out;
}

void
hdf5_group_push(struct hdf5 *h5, struct group *g)
{
    // Objects
    //   Header
    //     Num messages and overall size of whole object determined a posteriori
    // B-Trees
    //   Top node should be formed with blank values and filled in at the end
    // Heaps
    //   Should be formed as new variables come in
    // Symbol Node and/or data
    // Symbol Table Entries
    //   Offset into heap
    //   Object header loc
    //   Cache type  - probably either 0 or 1
    //   Scratch Pad - probably either blank or addr of b-tree and heap
}

void
hdf5_destroy(struct hdf5 **H)
{
    struct hdf5 *h5 = *H;
    buffer_destroy(&h5->buf);
    free(*H);
    *H = NULL;
}

int main (void)
{
    FILE *out = fopen("test.h5", "wb");
    struct hdf5 *h5 = hdf5_create(out);
    hdf5_destroy(&h5);
    return 0;
}
