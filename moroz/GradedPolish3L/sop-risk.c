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


typedef struct Game {
    region_t* regions;
    pthread_mutex_t* regions_mutex;
    int points[2];
    int gave_up[2];
    int num_regions;
    pthread_mutex_t game_mutex;
}Game;

typedef struct Player{
    pthread_t tid;
    char id;
    int index;
    unsigned int seed;
    Game* game;
}Player;

int compare(const void* a, const void* b) {
    return (*(int*)a) - (*(int*)b);
}

void print(Game* game) {

    for (int i=0;i<game->num_regions;i++) {
        region_t* region = &game->regions[i];
        printf("%d [%c] :", i, region->owner);
        for (int j=0;j<region->num_neighbors;j++) {
            printf(" %d", region->neighbors[j]);
            if (j<region->num_neighbors-1) {
                printf(";");
            }
        }
        printf("\n");
    }
}

void* work(void* args) {
    Player* player = (Player*)args;
    Game* game = player->game;
    int frust = 0;

    while (1) {
        if (frust>=FRUSTRATION_LIMIT) {
            break;
        }
        int field_id = rand_r(&player->seed)%player->game->num_regions;
        region_t* field = &player->game->regions[field_id];
        int lockIndexes[MAX_NEIGHBORS+1];
        int count = 0;
        lockIndexes[count++] = field_id;
        for (int i=0;i<field->num_neighbors;i++) {
            lockIndexes[count++] = field->neighbors[i];
        }

        qsort(lockIndexes, MAX_NEIGHBORS+1, sizeof(int), compare);

        for (int i=0;i<count;i++) {
            if (i > 0 && lockIndexes[i] == lockIndexes[i-1]) {
                continue;
            }
            pthread_mutex_lock(&game->regions_mutex[lockIndexes[i]]);
        }

        int f = 0;

        if (field->owner == player->id) {
            printf("Field %d is already taken by %c\n", field_id, field->owner);
            f = 1;
        }
        else {
            int neighbor_flag = 0;
            for (int i=0;i<field->num_neighbors;i++) {
                int neighbor = field->neighbors[i];
                if (game->regions[neighbor].owner == player->id) {
                    neighbor_flag = 1;
                    break;
                }
            }
            if (!neighbor_flag) {
                printf("Field %d has no neighboring fields owned by %c\n", field_id, player->id);
                f = 1;
            }
        }

        if (f) {
            frust++;
        }
        else {
            ms_sleep(MOVE_MS);

            field->owner = player->id;
            pthread_mutex_lock(&game->game_mutex);
            game->points[player->index]++;
            pthread_mutex_unlock(&game->game_mutex);
            frust = 0;
        }

        for (int i=0;i<count;i++) {
            if (i > 0 && lockIndexes[i] == lockIndexes[i-1]) {
                continue;
            }
            pthread_mutex_unlock(&game->regions_mutex[lockIndexes[i]]);
        }
    }
    pthread_mutex_lock(&game->game_mutex);
    game->gave_up[player->index] = 1;
    pthread_mutex_unlock(&game->game_mutex);

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
    int startB = rand()%num_regions;
    while (start==startB) {
        startB = rand()%num_regions;
    }
    regions[start].owner = 'A';
    regions[startB].owner = 'B';

    Game game;
    game.regions = regions;
    game.num_regions = num_regions;
    game.regions_mutex = malloc(sizeof(pthread_mutex_t) * game.num_regions);
    if (pthread_mutex_init(&game.game_mutex, NULL)) {
        ERR("pthread_mutex_init");
    }
    game.gave_up[0] = 0;
    game.gave_up[1] = 0;
    game.points[0] = 0;
    game.points[1] = 0;

    for (int i=0;i<game.num_regions;i++) {
        if (pthread_mutex_init(&game.regions_mutex[i], NULL)) {
            ERR("pthread_mutex_init");
        }
    }

    Player args_p1;
    args_p1.game = &game;
    args_p1.index = 0;
    args_p1.id = 'A';
    args_p1.seed = rand();

    Player args_p2;
    args_p2.game = &game;
    args_p2.index = 1;
    args_p2.id = 'B';
    args_p2.seed = rand();

    if (pthread_create(&args_p1.tid, NULL, work, &args_p1)) {
        ERR("pthread_create");
    }
    if (pthread_create(&args_p2.tid, NULL, work, &args_p2)) {
        ERR("pthread_create");
    }

    while (1) {
        ms_sleep(SHOW_MS);
        print(&game);

        pthread_mutex_lock(&game.game_mutex);
        int any_given_up = (game.gave_up[0] || game.gave_up[1]);
        pthread_mutex_unlock(&game.game_mutex);

        if (any_given_up) break;
    }

    if (pthread_join(args_p1.tid, NULL)) {
        ERR("pthread_join");
    }
    if (pthread_join(args_p2.tid, NULL)) {
        ERR("pthread_join");
    }

    for (int i = 0; i < game.num_regions; i++) {
        pthread_mutex_destroy(&game.regions_mutex[i]);
    }
    free(game.regions_mutex);
    pthread_mutex_destroy(&game.game_mutex);
    free(regions);

    return 0;
}