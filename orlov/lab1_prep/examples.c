#define _XOPEN_SOURCE 500 //for nftw

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>

#define PATH_MAX 4092 // with _XOPEN_SOURCE 500 it is not defined by default

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

int dirs = 0, files = 0, links = 0, other = 0;

void recursive_ls(const char *dir_path, const int padding, const int additional_statistics)
{
    DIR * dir = opendir(dir_path);
    if (!dir)
        ERR("opendir");

    errno = 0;
    dirs = files = links = other = 0;

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

void scan_dir_v1()
{
    DIR *dirp;
    struct dirent *dp;
    struct stat filestat;
    int dirs = 0, files = 0, links = 0, other = 0;
    if ((dirp = opendir(".")) == NULL)
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

void scan_dir_v2(const char *path)
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

int count_files(const char *fpath, const struct stat *sb,
               int type, struct FTW *f)
{
    switch (type)
    {
    case FTW_DNR:
    case FTW_D:
        {
            dirs++;
            break;
        }
    case FTW_F:
        {
            files++;
            break;
        }
    case FTW_SL:
        {
            links++;
            break;
        }
    default:
        other++;
    }
    return 0;
}


int main(int argc, char **argv)
{
    // curr dir
    // scan_dir_v1();
    //working dir
    // char cur_dir_path[PATH_MAX];
    // if (getcwd(cur_dir_path, PATH_MAX) == NULL)
    //     ERR("getcwd");
    //
    // for (int i = 1; i < argc; i++)
    // {
    //     errno = 0;
    //     if (chdir(argv[i]))
    //         ERR("chdir");
    //     printf("Directory: %s\n", argv[i]);
    //     scan_dir_v1();
    //     if (chdir(cur_dir_path))
    //         ERR("chdir");
    // }

    //Recursive directories
    // for (int i = 1; i < argc; i++)
    // {
    //     if (nftw(argv[i], count_files, 20, 0) == -1)
    //         ERR("ntfw");
    // }
    // printf("[ Files: %d, Dirs: %d, Links: %d, Other: %d ]\n", files, dirs, links, other);
    // dirs = files = links = other = 0;

    //File operations
    // if (argc != 3)
    //     exit(EXIT_FAILURE);
    //
    // const char *filename = argv[1];
    //
    // FILE *file  = fopen(filename, "w");
    //
    // if (file == NULL)
    //     ERR("fopen");
    //
    // int bytes_writtent = fprintf(file, "%s", argv[2]);
    // fprintf(stdout, "1 Number of bytes writtent to a file:%d\n", bytes_writtent);
    //
    // bytes_writtent = fprintf(file, "%s", argv[2]);
    // fprintf(stdout, "2 Number of bytes writtent to a file:%d\n", bytes_writtent);
    //
    // fclose(file);
    // v2:
    // int c;
    // char *name = NULL;
    // mode_t perms = -1;
    // ssize_t size = -1;
    // while ((c = getopt(argc, argv, "p:n:s:")) != -1)
    //     switch (c)
    //     {
    //         case 'p':
    //                 perms = strtol(optarg, (char **)NULL, 8);
    //                 break;
    //         case 's':
    //                 size = strtol(optarg, (char **)NULL, 10);
    //                 break;
    //         case 'n':
    //                 name = optarg;
    //                 break;
    //         default:
    //            ERR("parsing params");
    //     }
    // if ((NULL == name) || ((mode_t) -1 == perms) || (-1 == size))
    //     ERR("parsing response");
    //
    // errno = 0;
    // umask(~perms & 0777); // use leading 0 to indicate OCTAL (system of 8)
    // FILE *f = fopen(name, "w");
    // if (!f)
    //     ERR("fopen");
    //
    //
    // for (int i = 0; i < (size * 10) / 100; i++)
    // {
    //     if (fseek(f, rand() % size, SEEK_SET))
    //         ERR("fseek");
    //     fprintf(f, "%c", 'A' + (i % ('Z' - 'A' + 1)));
    // }
    //
    // if (fclose(f))
    //     ERR("fclose");

    //buffering examples
    // for (int i = 0; i < 15; ++i)
    // {
    //     // Output the iteration number and then sleep 1 second.
    //     fprintf(stderr, "%d", i);
    //     sleep(1);
    // }

    //low-level read / writes

    if (argc != 3)
        exit(1);

    char *source = argv[1];
    char *dest = argv[2];

    int source_fd = open(source, O_RDONLY);
    if (source_fd == -1)
        ERR("opening source file");

    int dest_fd = open(dest, O_WRONLY | O_CREAT , 0644);
    if (dest_fd == -1)
        ERR("opening dest file");

    char buf[4092];
    int bytes_read = read(source_fd, buf, sizeof(buf));
    printf("reading %d bytes\n", bytes_read);

    int bytes_written = write(dest_fd, buf, bytes_read);
    printf("writing %d bytes\n", bytes_written);

    if (close(source_fd))
        ERR("closing source file");
    if (close(dest_fd))
        ERR("closing source file");

    return EXIT_SUCCESS;
}

