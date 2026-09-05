#include <cstddef>
#include <cstdio>
#include "message_txt.h"

int main() {
    unsigned sum = 0;
    for (std::size_t i = 0; i < fixture::message_txt_size; ++i) sum += fixture::message_txt[i];
    std::printf("size=%zu sum=%u text=%s", fixture::message_txt_size, sum,
                reinterpret_cast<const char*>(fixture::message_txt));
    return fixture::message_txt[fixture::message_txt_size] == 0 ? 0 : 1;
}
