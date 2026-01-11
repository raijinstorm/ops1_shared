#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define MAX_NEIGHBORS 6
#define FRUSTRATION_LIMIT 3
#define MOVE_MS 140
#define SHOW_MS 500
/**
 * @struct region
 * @brief The unit of a playing board
 */
typedef struct region
{
    int8_t owner;                    /* Symbol of the player that controls this region */
    int8_t neighbors[MAX_NEIGHBORS]; /* Array of indexes of neighboring regions */
    int8_t num_neighbors;            /* The number of neighboring regions */
} region_t;

/**
 * @brief Loads a playing board from a file
 *
 * Parses the board file format and initializes all regions' owner to '-'
 *
 * @param file The file to load the board from
 * @param num_regions The value under this pointer will be set to the number of regions in the returned array
 * @return An array containing all regions described in the file
 */
region_t* load_regions(char* file, int* num_regions)
{
    FILE* f = fopen(file, "r");
    if (!f) ERR("fopen");

    int cap = 16;
    int n = 0;

    region_t* regions = malloc(sizeof(region_t) * cap);
    if (!regions) ERR("malloc");

    char* line = NULL;
    size_t capacity = 0;

    while (getline(&line, &capacity, f) != -1)
    {
        if (n == cap) {
            cap *= 2;
            region_t* tmp = realloc(regions, sizeof(region_t) * cap);
            if (!tmp) ERR("realloc");
            regions = tmp;
        }

        region_t* r = &regions[n];
        memset(r, 0, sizeof(*r));
        r->owner = '-';

        char* cur = strtok(line, ";");
        if (cur && *cur != '\n') {
            while (cur != NULL) {
                if (r->num_neighbors >= MAX_NEIGHBORS) {
                    fprintf(stderr, "Exceeded max neighbor count on line %d\n", n);
                    exit(EXIT_FAILURE);
                }
                r->neighbors[r->num_neighbors] = (int8_t)atoi(cur);
                r->num_neighbors++;
                cur = strtok(NULL, ";");
            }
        }

        n++;
    }

    if (!feof(f)) ERR("getline");

    free(line);
    fclose(f);

    *num_regions = n;

    // Optional shrink
    region_t* shr = realloc(regions, sizeof(region_t) * n);
    if (shr) regions = shr;

    return regions;
}

void ms_sleep(unsigned int ms_time)
{
    struct timespec ts = {0, ms_time * 1000000};
    while (nanosleep(&ts, &ts))
        ;
}