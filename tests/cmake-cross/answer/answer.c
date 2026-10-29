#include "answer.h"
#include <stddef.h>

/* glibc defines __GLIBC__ in <features.h>, which <stddef.h> does not include;
   <stdio.h> does, and so does every glibc header that a musl one is not. */
#include <stdio.h>

const char* answer_libc(void) {
#if defined(__GLIBC__)
    return "glibc";
#else
    return "not glibc";
#endif
}
