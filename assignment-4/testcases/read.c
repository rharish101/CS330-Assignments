#include "kvstore.h"
#include <string.h>
#include "debug.h"

int main(int argc, char **argv)
{
    char *buffer;
    int size;
    if (argc != 3) {
        fprintf(stderr, "Usage: %s key filesize\n", argv[0]);
        exit(100);
    }
    size = atoi(argv[2]) + 1;
    buffer = (char *) malloc(size * sizeof(char));
    memset(buffer, 0, size * sizeof(char));
    if (get_key(argv[1], buffer) < 0) {
        dprintf("FAILED\n");
        exit(10);
    } else
        printf("%s", buffer);
    return 0;
}
