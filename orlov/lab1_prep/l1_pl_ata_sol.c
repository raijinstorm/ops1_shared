#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * trim_dup_start
 * Skips leading spaces/tabs in 'str' and returns a newly allocated duplicate
 * of the remainder. Caller is responsible for free()'ing the returned string.
 */
static char* trim_dup_start(const char* str) {
    while (*str == ' ' || *str == '\t') str++;
    return strdup(str);
}

/*
 * main
 * Reads a metadata file provided on the command line and prints the first
 * occurrence of the fields: author, title, and genre (or 'missing!' if absent).
 * Returns 0 on success, non-zero on error (usage or fopen failure).
 */
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "r");

    if (f == NULL)
        return 1;

    char *author = NULL, *title = NULL, *genre = NULL;
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;

    // Parse simple key:value lines, collecting the first occurrence per key
    while ((len = getline(&line, &cap, f)) != -1)
    {
        // Split at the first ':'; ignore lines without a key:value format
        char *colon = strchr(line, ':');
        if (!colon)
            continue;

        *colon = '\0';
        const char *key = line;
        const char *val = colon + 1;

        if (!author && strcmp(key, "author") == 0)
        {
            // Duplicate value after trimming leading spaces/tabs
            author = trim_dup_start(val);
        }
        else if (!title && strcmp(key, "title") == 0)
        {
            title = trim_dup_start(val);
        }
        else if (!genre && strcmp(key, "genre") == 0)
        {
            genre = trim_dup_start(val);
        }
    }

    free(line);
    fclose(f);

    if (author) { printf("author: %s\n", author); free(author); }
    else        { printf("author: missing!\n"); }

    if (title)  { printf("title: %s\n", title);   free(title); }
    else        { printf("title: missing!\n"); }

    if (genre)  { printf("genre: %s\n", genre);   free(genre); }
    else        { printf("genre: missing!\n"); }
}



