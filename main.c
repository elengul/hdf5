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
    struct group *root_group;
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
buffer_transfer(struct buffer *dst, struct buffer *src)
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

void
file_shift(FILE *fid, uint64_t start, uint64_t amt)
{
    fseek(fid, 0, SEEK_END);
    size_t total = ftell(fid) - start;
    char buffer[total];
    fseek(fid, start, SEEK_SET);
    fread(buffer, total, 1, fid);
    fseek(fid, start+amt, SEEK_SET);
    fwrite(buffer, total, 1, fid);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

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

struct var {
    size_t len_name, heap_off, obj_loc;
};

struct group {
    uint64_t b_tree_begin;
    uint64_t heap_begin, heap_end, heap_p;
    size_t size, count;
    struct var **var;
};

struct var *
var_create(size_t len, size_t off)
{
    struct var *out = malloc(sizeof(*out));
    out->len_name = len;
    out->heap_off = off;
    return out;
}

void
var_destroy(struct var **v)
{
    free(*v);
    *v = NULL;
}

void
group_load(struct group *g, unsigned type)
{
    switch (type) {
    default: //case 0:
        g->b_tree_begin = ROOT_BTREE;
        g->heap_begin   = ROOT_HEAP;
        g->heap_end     = ROOT_HEAP + 0x78;
        g->heap_p       = ROOT_HEAP + 0x28;
        break;
    }
}

struct group *
group_create(unsigned type)
{
    // type will determine components of group
    struct group *out = malloc(sizeof(*out));
    group_load(out, type);
    out->size  = 4;
    out->count = 0;
    out->var   = malloc(out->size * sizeof(out->var[0]));
    return out;
}

void
group_destroy(struct group **g)
{
    for (size_t i = 0; i < (*g)->count; i++)
        var_destroy(&(*g)->var[i]);
    free((*g)->var);
    free(*g);
    *g = NULL;
}

void
group_var_push(struct group *g, struct var *v)
{
    if (g->count == g->size) {
        g->size *= 2;
        g->var   = realloc(g->var, g->size * sizeof(g->var[0]));
    }
    g->var[g->count] = v;
    g->count++;
}

void
hdf5_buffer_fill_object_header(struct hdf5 *h5, uint16_t num_msg, uint64_t hdr_size)
{
    uint8_t obj_ver   = 1;
    uint32_t ref_cnt  = 1;
    buffer_write(h5->buf, &obj_ver,  1);
    buffer_write(h5->buf, &RES_8,    1);
    buffer_write(h5->buf, &num_msg,  2);
    buffer_write(h5->buf, &ref_cnt,  4);
    buffer_write(h5->buf, &hdr_size, 8);
}

void
hdf5_buffer_message_0x05(struct hdf5 *h5)
{
    // This message has to exist, but it's always the same
    uint64_t first  = 0x0000000100070001;
    uint64_t second = 0x0000000001020102;
    buffer_write(h5->buf, &first,  8);
    buffer_write(h5->buf, &second, 8);
}

void
hdf5_root_group(struct hdf5 *h5)
{
    struct group *g = group_create(0);
    g->b_tree_begin += h5->super_block.file_offset;
    g->heap_begin   += h5->super_block.file_offset;
    g->heap_end     += h5->super_block.file_offset;
    g->heap_p       += h5->super_block.file_offset;

    // Object header and message for root group
    uint16_t num_msg  = 1;
    uint64_t hdr_size = 0x0000000000000018;
    uint16_t msg_type = 0x0011;
    uint16_t msg_size = 0x0010;
    uint32_t flags    = 0x00000000;
    hdf5_buffer_fill_object_header(h5, num_msg, hdr_size);
    buffer_write(h5->buf, &msg_type, 2);
    buffer_write(h5->buf, &msg_size, 2);
    buffer_write(h5->buf, &flags,    4);
    buffer_write(h5->buf, &ROOT_BTREE, sizeof(ROOT_BTREE));
    buffer_write(h5->buf, &ROOT_HEAP, sizeof(ROOT_HEAP));

    // Setting up root B-Tree and Heap
    const char *tree_sig = "TREE";
    const char *heap_sig = "HEAP";
    uint8_t node_type  = 0x00;
    uint8_t node_level = 0x00;   // Denotes a leaf ... could change depending on num_vars
    uint16_t entries   = 0x0001; // Always this for root
    uint64_t blank64   = 0x0000000000000000;
    uint32_t h_ver_res = 0x00000000;
    uint64_t data_size = g->heap_end - g->heap_begin - 0x20;
    uint64_t data_beg  = g->heap_begin + 0x20 - h5->super_block.file_offset;
    buffer_write(h5->buf, tree_sig, strlen(tree_sig));
    buffer_write(h5->buf, &node_type,  1);
    buffer_write(h5->buf, &node_level, 1);
    buffer_write(h5->buf, &entries,    2);
    buffer_write(h5->buf, &UNDEF,      8);
    buffer_write(h5->buf, &UNDEF,      8);
    for (size_t i = 0; i < (1 + 4*INT_K); i++)
        buffer_write(h5->buf, &blank64, 8); // Blank key and entries
    buffer_write(h5->buf, heap_sig,   strlen(heap_sig));
    buffer_write(h5->buf, &h_ver_res, 4);
    buffer_write(h5->buf, &data_size, 8);
    buffer_write(h5->buf, &blank64,   8);
    buffer_write(h5->buf, &data_beg,  8);
    for (size_t i = 0; i < (data_size / 8); i++)
        buffer_write(h5->buf, &blank64, 8); // Blank heap

    // Fleshed out Symbol Node and blanked out entries for the symbol table
    const char *snod_sig = "SNOD";
    uint16_t snod_ver_res = 0x0001;
    uint16_t num_syms     = 0x0000;
    buffer_write(h5->buf, snod_sig, strlen(snod_sig));
    buffer_write(h5->buf, &snod_ver_res, 2);
    buffer_write(h5->buf, &num_syms,     2);
    for (size_t i = 0; i < 5*2*LEAF_K; i++)
        buffer_write(h5->buf, &blank64, 8);
    
    buffer_flush(h5->buf, h5);
    h5->root_group = g;
}

void
hdf5_destroy(struct hdf5 **H)
{
    struct hdf5 *h5 = *H;
    if (buffer_tell(h5->buf) > 0)
        buffer_flush(h5->buf, h5);
    uint64_t eof_mark = ftell(h5->out);
    fseek(h5->out, h5->super_block.eof_loc, SEEK_SET);
    fwrite(&eof_mark, sizeof(eof_mark), 1, h5->out);
    fclose(h5->out);
    buffer_destroy(&h5->buf);
    group_destroy(&h5->root_group);
    free(*H);
    *H = NULL;
}

enum mat_type {miDOUBLE};

void
hdf5_begin(struct hdf5 *h5, const char *name, enum mat_type type)
{
    // write name to heap and store info
    size_t len_name = strlen(name);
    struct var *v = var_create(len_name, h5->root_group->heap_p);
    if (len_name > (h5->root_group->heap_end - h5->root_group->heap_p + 1)) {
        size_t amt = h5->root_group->heap_end - h5->root_group->heap_begin - 0x20;
        file_shift(h5->out, h5->root_group->heap_end, amt);
        h5->root_group->heap_end += amt;
        for (size_t i = 0; i < h5->root_group->count; i++)
            h5->root_group->var[i]->obj_loc += amt;
    }
    group_var_push(h5->root_group, v);
    fseek(h5->out, h5->root_group->heap_p, SEEK_SET);
    fwrite(name, len_name, 1, h5->out);
    fwrite(&RES_8, 1, 1, h5->out);
    h5->root_group->heap_p += len_name + 1;
    while ((h5->root_group->heap_p % 8) != 0) {
        fwrite(&RES_8, 1, 1, h5->out);
        h5->root_group->heap_p++;
    }
    v->obj_loc = 0x28*(h5->root_group->count - 1) + h5->root_group->heap_end + 0x08;

    // Begin writing object to buffer
    hdf5_buffer_fill_object_header(h5, 5, UNDEF);
    hdf5_buffer_message_0x05(h5);
    // TODO: hdf5_buffer_message_0x03 - need h5, type
    // TODO: hdf5_buffer_message_0x0C - need h5, type
}

int main (void)
{
    FILE *out = fopen("data/test.h5", "wb");
    struct hdf5 *h5 = hdf5_create(out);
    hdf5_root_group(h5);
    
    double test_a = 5.7;
    hdf5_begin(h5, "test_a", miDOUBLE);
    hdf5_dims(h5, 2, 1, 1);
    hdf5_data(h5, &test_a);
    hdf5_end(h5);
    
    hdf5_destroy(&h5);
    return 0;
}
