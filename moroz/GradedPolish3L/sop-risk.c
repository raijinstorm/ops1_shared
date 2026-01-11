#include "risk.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


void usage(int argc, char** argv)
{
    fprintf(stderr, "USAGE: %s levelname.risk\n", argv[0]);
    exit(EXIT_FAILURE);
}

void print(region_t* regions, int num_regions, pthread_mutex_t* regions_mutex) {
    pthread_mutex_lock(regions_mutex);
    for (int i=0;i<num_regions;i++) {
        printf("%d [%c] :", i, regions[i].owner);
        for (int j=0;j<regions[i].num_neighbors;j++) {
            printf(" %d", regions[i].neighbors[j]);
            if (j<regions[i].num_neighbors-1) {
                printf(";");
            }
        }
        printf("\n");
    }
    pthread_mutex_unlock(regions_mutex);
}

typedef struct Player{
    pthread_t tid;
    region_t* regions;
    pthread_mutex_t* regions_mutex;
    int gave_up;
    int num_regions;
    char id;
    unsigned int seed;
}Player;

void* work(void* args) {
    Player* player = (Player*)args;
    int count = 0;

    while (1) {
        if (count>=FRUSTRATION_LIMIT) {
            break;
        }
        int field_id = rand_r(&player->seed)%player->num_regions;
        region_t* field = &player->regions[field_id];
        int f = 0;

        if (field->owner == player->id) {
            printf("Field %d is already taken by %c\n", field_id, field->owner);
            f = 1;
        }
        else {
            int neighbor_flag = 0;
            pthread_mutex_lock(player->regions_mutex);
            for (int i=0;i<field->num_neighbors;i++) {
                int neighbor = field->neighbors[i];
                if (player->regions[neighbor].owner == player->id) {
                    neighbor_flag = 1;
                    break;
                }
            }
            if (!neighbor_flag) {
                printf("Field %d has no neighboring fields owned by %c\n", field_id, player->id);
                f = 1;
                pthread_mutex_unlock(player->regions_mutex);
            }
            else {
                pthread_mutex_unlock(player->regions_mutex);
                ms_sleep(MOVE_MS);

                pthread_mutex_lock(player->regions_mutex);
                field->owner = player->id;
                pthread_mutex_unlock(player->regions_mutex);
                count = 0;
            }
        }
        if (f) {
            count++;
        }
    }

    pthread_mutex_lock(player->regions_mutex);
    player->gave_up = 1;
    pthread_mutex_unlock(player->regions_mutex);
    return NULL;
}

int main(int argc, char** argv) {
    if (argc!=2) {
        usage(argc, argv);
    }
    srand(time(NULL));
    int num_regions;
    region_t* regions = load_regions(argv[1], &num_regions);
    int start = rand()%num_regions;
    regions[start].owner = 'A';

    pthread_mutex_t regions_mutex = PTHREAD_MUTEX_INITIALIZER;

    Player args_p1;
    args_p1.num_regions = num_regions;
    args_p1.regions = regions;
    args_p1.id = 'A';
    args_p1.seed = rand();
    args_p1.regions_mutex = &regions_mutex;

    if (pthread_create(&args_p1.tid, NULL, work, &args_p1)) {
        ERR("pthread_create");
    }

    while (1) {
        ms_sleep(SHOW_MS);
        print(regions, num_regions, args_p1.regions_mutex);
        pthread_mutex_lock(&regions_mutex);
        int flag = args_p1.gave_up;
        pthread_mutex_unlock(&regions_mutex);
        if (flag) {
            break;
        }
    }

    if (pthread_join(args_p1.tid, NULL)) {
        ERR("pthread_join");
    }
    pthread_mutex_destroy(&regions_mutex);
    free(regions);

    return 0;
}