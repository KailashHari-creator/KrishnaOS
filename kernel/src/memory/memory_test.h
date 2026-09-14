#ifndef KRISHNA_MEMORY_TEST_H
#define KRISHNA_MEMORY_TEST_H

#include <stdbool.h>
#include <stdint.h>

struct memory_test_result {
    bool allocation_passed;
    bool distinct_pages_passed;
    bool mapping_passed;
    bool translation_passed;
    bool write_read_passed;
    bool cleanup_passed;

    uint64_t first_page;
    uint64_t second_page;
    uint64_t contiguous_block;
};

bool memory_run_self_test(struct memory_test_result *result);

#endif
