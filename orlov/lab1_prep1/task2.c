#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>


#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void recursive_ls(const char *dir_path, const int padding, const int additional_statistics)
{
    DIR * dir = opendir(dir_path);
    if (!dir)
        ERR("opendir");

    errno = 0;
    int dirs = 0, files = 0, links = 0, other = 0;

    struct dirent * entry;
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
        for (int i = 1; i < padding; i++)
            printf(" ");
        if (S_ISREG(filestat.st_mode))
        {
            printf("%*sRegular file: %s\n", padding, "", entry->d_name);
            files++;
        }
        else if (S_ISDIR(filestat.st_mode))
        {
            printf("%*sDirectory: %s\n", padding, "", entry->d_name);
            dirs++;
            recursive_ls(full_path, padding + 3, additional_statistics);
        }
        else if (S_ISLNK(filestat.st_mode))
        {
            printf("%*sLink: %s\n", padding, "", entry->d_name);
            links++;
        }
        else
            other++;

    }

    if (additional_statistics)
    {
        printf("%*s[ Files: %d, Dirs: %d, Links: %d, Other: %d ]\n", padding, "" , files, dirs, links, other);
    }


    if (errno != 0)
        ERR("readdir");
    if (closedir(dir))
        ERR("closedir");

}

void scan_dir(const char *path)
{
    DIR *dirp;
    struct dirent *dp;
    struct stat filestat;
    int dirs = 0, files = 0, links = 0, other = 0;
    if ((dirp = opendir(path)) == NULL)
        ERR("opendir");
    do
    {
        errno = 0;
        if ((dp = readdir(dirp)) != NULL)
        {
            if (lstat(dp->d_name, &filestat))
                ERR("lstat");
            if (S_ISDIR(filestat.st_mode))
                dirs++;
            else if (S_ISREG(filestat.st_mode))
                files++;
            else if (S_ISLNK(filestat.st_mode))
                links++;
            else
                other++;
        }
    } while (dp != NULL);

    if (errno != 0)
        ERR("readdir");
    if (closedir(dirp))
        ERR("closedir");
    printf("[ Files: %d, Dirs: %d, Links: %d, Other: %d ]\n", files, dirs, links, other);
}

int main(int argc, char **argv)
{
    const char *dir_path = "test_dir_1";
    recursive_ls(dir_path, 0, 0);
    // scan_dir("test_dir_1");
    return EXIT_SUCCESS;
}

