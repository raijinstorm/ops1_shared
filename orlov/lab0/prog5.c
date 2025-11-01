#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 3)
        exit(EXIT_FAILURE);
    const char *name = argv[1];
    int cnt = atoi(argv[2]);

    if (cnt <= 0)
        exit(EXIT_FAILURE);
    for (int i = 0; i < cnt; i++)
        printf("%s\n", name);
    return EXIT_SUCCESS;
}