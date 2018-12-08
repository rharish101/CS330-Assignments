#include "kvstore.h"
#include "debug.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <oldkey> <newkey>\n", argv[0]);
        exit(100);
    }
    if (rename_key(argv[1], argv[2]) < 0) {
        dprintf("FAILED\n");
        exit(10);
    } else
        dprintf("SUCCESS\n");
    return 0;
}
