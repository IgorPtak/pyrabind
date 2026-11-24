#pragma once

#include <cstdint>

#ifndef ARROW_C_DATA_INTERFACE
#define ARROW_C_DATA_INTERFACE

#define ARROW_FLAG_DICTIONARY_ORDERED 1
#define ARROW_FLAG_NULLABLE 2
#define ARROW_FLAG_MAP_KEYS_SORTED 4

struct ArrowSchema {
    const char* format;
    const char* name;
    const char* metadata;
    std::int64_t flags;
    std::int64_t n_children;
    struct ArrowSchema** children;
    struct ArrowSchema* dictionary;

    void (*release)(struct ArrowSchema*);
    void* private_data;
};

struct ArrowArray {
    std::int64_t length;
    std::int64_t null_count;
    std::int64_t offset;
    std::int64_t n_buffers;
    std::int64_t n_children;
    const void** buffers;
    struct ArrowArray** children;
    struct ArrowArray* dictionary;

    void (*release)(struct ArrowArray*);
    void* private_data;
};

#endif // ARROW_C_DATA_INTERFACE
