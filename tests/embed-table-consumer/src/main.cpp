// Nothing here is a module: `table()` writes a plain header, the same as
// `file()`, and this file includes it by name the same way embed-consumer
// includes `message_txt.h`.
#include <cstdio>
#include <cstring>
#include "shader_table.h"

namespace {

bool check(const fixture::shader_entry& row, const char* want_key, const char* want_text) {
    const std::size_t want_len = std::strlen(want_text);
    const bool key_ok  = std::strcmp(row.key, want_key) == 0;
    const bool size_ok = row.size == want_len;
    const bool text_ok = size_ok && std::memcmp(row.data, want_text, want_len) == 0;
    std::printf("%s: key=%s size=%zu %s\n", want_key, row.key, row.size,
                (key_ok && size_ok && text_ok) ? "ok" : "BAD");
    return key_ok && size_ok && text_ok;
}

} // namespace

int main() {
    bool ok = fixture::shader_table_size == 2;

    // The consumer iterates: that is `table()`'s whole point next to
    // `file()`'s one accessor per input.
    for (std::size_t i = 0; i < fixture::shader_table_size; ++i)
        std::printf("row %zu: key=%s size=%zu\n", i, fixture::shader_table[i].key,
                    fixture::shader_table[i].size);

    ok = check(fixture::shader_table[0], "Standard.vert", "vertex stage source\n") && ok;
    ok = check(fixture::shader_table[1], "Standard.frag", "fragment stage source\n") && ok;

    // Cheap insurance that the two rows are two objects. This is not the
    // `#pragma once` collapse `mcpp.rules.spirv` measured -- that needs two
    // generated headers, and a table is one -- but a generator that pointed
    // every row at the last row read would look exactly like this.
    if (fixture::shader_table[0].data == fixture::shader_table[1].data) {
        std::printf("BAD: both rows resolve to one array\n");
        ok = false;
    }

    std::printf(ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
