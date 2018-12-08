#include "kvstore.h"
#include "debug.h"

int main(int argc, char **argv)
{
    if (argv[1]) {
        if (delete_key(argv[1]) < 0) {
            dprintf("FAILED\n");
            exit(10);
        } else
            dprintf("SUCCESS\n");
    } else {
        fprintf(stderr, "Usage: %s <key>\n", argv[0]);
        exit(100);
    }
    return 0;
}
