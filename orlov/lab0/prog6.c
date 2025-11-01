#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

int main(int argc, char **argv)
{
    int index = 0;
    while (environ[index])
        printf("%s\n", environ[index++]);
    return EXIT_SUCCESS;
}

// You can add your own variable in the following way: $ TVAR2="122345" ./prog6 ,
// it will show up on the list but it will not be stored in the parent environment i.e.
// next run ./prog6 will not list it.
//
// You can also store it in the shell environment: export TVAR1='qwert' and
// invoke ./prog6 multiple times, it will keep showing TVAR1 on the list.