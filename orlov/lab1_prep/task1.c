#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void print_big_folders(const char *dir_path, const ssize_t min_size)
{
    DIR * dir = opendir(dir_path);
    if (!dir)
    {
        perror("opendir");
        return;
    }

    errno = 0;

    struct dirent * entry;
    ssize_t size_sum = 0;
    while ((entry = readdir(dir)))
    {
        char full_path[PATH_MAX];
        if (strcmp(entry->d_name , ".") == 0 || strcmp(entry->d_name , "..") == 0)
            continue;
        strcpy(full_path, dir_path);
        strcat(full_path, "/");
        strcat(full_path, entry->d_name);
        struct stat filestat;
        const int status = lstat(full_path, &filestat);
        if (status != 0)
        {
            closedir(dir);
            exit(1);
        }
        size_sum += filestat.st_size;
    }

    if (size_sum >= min_size)
        printf("%s\n", dir_path);

    if (errno != 0)
        ERR("readdir");
    if (closedir(dir))
        ERR("closedir");

}

void print_big_folders_low_level(const char *dir_path, const ssize_t min_size)
{
    return;
}


int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (i % 2)
            continue;
        const ssize_t min_size = atoi(argv[i]);

        //if value could not be converted skip this folder
        if (min_size == 0)
            continue;

        print_big_folders(argv[i-1], min_size);
    }
    return EXIT_SUCCESS;
}