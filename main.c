#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum var_type {INT8, UINT8, INT16, UINT16, INT32, UINT32,
               INT64, UINT64, FLOAT, DOUBLE, ARRAY, STRUCT, CELL};

struct structure {
    size_t size;
    char **name;
    struct variable **var;
};

struct variable {
    char *name;
    enum var_type type;
    union {
        struct structure *structure;
        //struct matrix *matrix;
        //struct cell *cell;
        void *value;
    };
};

struct buffer {
    size_t size, count;
    char *buffer;
};

struct superblock {
    uint16_t leaf_node_k;
    uint16_t int_node_k;
    uint64_t eof_addr;
};

struct hdf5_file {
    FILE *file;
    struct buffer *buf;
    struct superblock *super_block;
    size_t size, count;
    struct variable **var;
};

struct variable *
variable_create(char *name, enum var_type type, void *data)
{
    struct variable *out = malloc(sizeof(*out));
    out->name = malloc((1 + strlen(name)) * sizeof(out->name[0]));
    strcpy(out->name, name);
    out->type = type;
    out->value = data;
    return out;
}

void
variable_destroy(struct variable **v)
{
    free((*v)->name);
    free(*v);
    *v = NULL;
}

struct buffer *
buffer_create(void)
{
    struct buffer *out = malloc(sizeof(*out));
    out->count  = 0;
    out->size   = 65536;
    out->buffer = calloc(out->size, sizeof(out->buffer[0]));
    return out;
}

void
buffer_destroy(struct buffer **b)
{
    free((*b)->buffer);
    free(*b);
    *b = NULL;
}

int
buffer_push(struct buffer *b, char *str)
{
    size_t len = strlen(str);
    if ((b->count + len) > b->size) {
        size_t old_size = b->size;
        b->size  *= 2;
        char *tmp = realloc(b->buffer, b->size * sizeof(b->buffer[0]));
        if (tmp) {
            b->buffer = tmp;
            memset(&(b->buffer[old_size]), 0,
                   (b->size - old_size) * sizeof(b->buffer[0]));
        } else
            return -1;
    }
    strcat(b->buffer, str);
    b->count += len;
    return 0;
}

void
buffer_flush(struct buffer *b, FILE *out)
{
    fputs(b->buffer, out);
    b->count = 0;
    memset(b->buffer, 0, b->size * sizeof(b->buffer[0]));
}

struct superblock *
superblock_create(uint16_t leaf, uint16_t inter)
{
    struct superblock *out = malloc(sizeof(*out));
    out->leaf_node_k = leaf;
    out->int_node_k = inter;
    out->eof_addr = 0;
    return out;
}

void
superblock_destroy(struct superblock **s)
{
    free(*s);
    *s = NULL;
}

void
hdf5_write_superblock(struct hdf5_file *h5)
{
    uint8_t u8_0 = 0;
    uint8_t u8_8 = 8;
    uint32_t u32_0 = 0;
    int64_t pos = ftell(h5->file);
    uint64_t eof_pos = 0;
    uint64_t undef = 0xFFFFFFFFFFFFFFFF;
    fprintf(h5->file, "%c%c%c%c%c%c%c%c",
            0x89, 0x48, 0x44, 0x46, 0x0D, 0x0A, 0x1A, 0x0A); // HDF File signature
    fwrite(&u8_0, sizeof(u8_0), 1, h5->file); // Superblock version
    fwrite(&u8_0, sizeof(u8_0), 1, h5->file); // File's free space storage version
    fwrite(&u8_0, sizeof(u8_0), 1, h5->file); // Root Group Symbol Table Entry version
    fwrite(&u8_0, sizeof(u8_0), 1, h5->file); // Reserved
    fwrite(&u8_0, sizeof(u8_0), 1, h5->file); // Shared Header Message format version
    fwrite(&u8_8, sizeof(u8_8), 1, h5->file); // Size of offsets (64-bit addressing)
    fwrite(&u8_8, sizeof(u8_8), 1, h5->file); // Size of lengths (64-bit length when variable)
    fwrite(&u8_0, sizeof(u8_0), 1, h5->file); // Reserved
    fwrite(&h5->super_block->leaf_node_k,                      // Leaf nodes in b-trees have from k
           sizeof(h5->super_block->leaf_node_k), 1, h5->file); // up to 2k+1 members
    fwrite(&h5->super_block->int_node_k,                       // Internal nodes in b-trees have 
           sizeof(h5->super_block->leaf_node_k), 1, h5->file); // from k up to 2k+1 members
    fwrite(&u32_0, sizeof(u32_0), 1, h5->file); // File consistency flags
    fwrite(&pos, sizeof(pos), 1, h5->file); // Base address (where HDF part of file starts)
    fwrite(&undef, sizeof(undef), 1, h5->file); //  File free space info addr (always undefined)
    h5->super_block->eof_addr = ftell(h5->file);    // Save where in file this is to write at end
    fwrite(&eof_pos, sizeof(eof_pos), 1, h5->file); // EOF address - 64-bit 0 as placeholder
    fwrite(&undef, sizeof(undef), 1, h5->file); // Driver Information Block Address (always undefined)
}

void
hdf5_initialize_root_structures(struct hdf5_file *h5)
{
    // REDO - Don't Hardcode stuff
    uint64_t link_name_offset = 0;
    uint64_t obj_header_addr  = ftell(h5->file) + 40;
    uint32_t cache_type = 1;
    uint32_t reserved   = 0;
    uint64_t b_tree_loc = obj_header_addr; // + todo
    uint64_t heap_loc   = b_tree_loc + 0x18 + (1 + 4*h5->super_block->int_node_k)*8; //todo: check this
    uint8_t obj_ver = 1;
    uint8_t u8_0 = 0;
    uint16_t num_msg = 1;
    uint32_t obj_ref_count = 1;
    uint32_t obj_header_size = 0x18; // todo: check this
    uint16_t msg_type = 0x11;
    uint16_t msg_sz = 0x10;
    uint32_t flags_and_pad = 0;
    // Write root symbol table entry
    fwrite(&link_name_offset, sizeof(link_name_offset), 1, h5->file);
    fwrite(&obj_header_addr, sizeof(obj_header_addr), 1, h5->file);
    fwrite(&cache_type, sizeof(cache_type), 1, h5->file);
    fwrite(&reserved, sizeof(reserved), 1, h5->file);
    fwrite(&b_tree_loc, sizeof(b_tree_loc), 1, h5->file);
    fwrite(&heap_loc, sizeof(heap_loc), 1, h5->file);
    // write root object
    fwrite(&obj_ver, sizeof(&obj_ver), 1, h5->file);
    fwrite(&u8_0, sizeof(u8_0), 1, h5->file);
    fwrite(
    // write root b-tree with zeros
    
}

struct hdf5_file *
hdf5_file_create(const char *fname, uint16_t leaf, uint16_t inter)
{
    struct hdf5_file *out = malloc(sizeof(*out));
    out->file = fopen(fname, "wb");
    out->buf  = buffer_create();
    out->super_block = superblock_create(leaf, inter);
    hdf5_write_superblock(out);
    out->size  = 16;
    out->count = 0;
    out->var   = malloc(out->size * sizeof(out->var[0]));
    return out;
}

void
hdf5_var_push(struct hdf5_file *h5, void *data, char *name, enum var_type type)
{
    if (h5->count == h5->size) {
        h5->size *= 2;
        h5->var   = realloc(h5->var, h5->size * sizeof(h5->var[0]));
    }
    h5->var[h5->count] = variable_create(name, type, data);
    h5->count++;
}

void
hdf5_file_end(struct hdf5_file **h5)
{
    struct hdf5_file *f = *h5;
    if (f->buf->count > 0)
        buffer_flush(f->buf, f->file);
    int64_t pos = ftell(f->file);
    fseek(f->file, f->super_block->eof_addr, SEEK_SET);
    fwrite(&pos, sizeof(pos), 1, f->file);
    buffer_destroy(&f->buf);
    superblock_destroy(&f->super_block);
    fclose(f->file);
    free(*h5);
    *h5 = NULL;
}

int main(void)
{
    struct hdf5_file *h5 = hdf5_file_create("test.h5", 4, 16);
    int32_t a  = 0xcafebabe;
    double b   = 1;
    uint64_t c = 0xdeadbeefdeadbeef;
    hdf5_var_push(h5, (void *)&a, "a", INT32);
    //hdf5_var_push(h5, (void *)&b, "b", DOUBLE);
    //hdf5_var_push(h5, (void *)&c, "c", UINT64);
    hdf5_write_vars(h5);
    hdf5_file_end(&h5);
    return 0;
}
