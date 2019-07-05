#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

int LLVMFuzzerTestOneInput(void* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        char** args = {"varnishd"};
        normal_main(1, args);
    }
    return 0;
}
