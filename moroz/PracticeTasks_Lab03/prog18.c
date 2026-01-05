#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXLINE 4096
#define DEFAULT_N 10000
#define DEFAULT_K 10
#define BIN_COUNT 11
#define NEXT_DOUBLE(seedptr) ((double)rand_r(seedptr) / (double)RAND_MAX)
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

typedef struct Thrower {
    pthread_t tid;
    unsigned int seed;
    int* ballsThrown;
    int* ballsWaiting;
    int* bins;
    pthread_mutex_t* mxBins;
    pthread_mutex_t* mxBallsThrown;
    pthread_mutex_t* mxBallsWaiting;
    int beansPerThread;
}Thrower;

void ReadArgs(int argc, char* argv[], int* throwersCount, int* ballsCount) {
    *throwersCount = DEFAULT_K;
    *ballsCount = DEFAULT_N;
    if (argc>=2) {
        *ballsCount = atoi(argv[1]);
        if (*ballsCount<=0) {
            printf("invalid number of throwers\n");
            exit(EXIT_FAILURE);
        }
    }
    if (argc>=3) {
        *throwersCount = atoi(argv[2]);
        if (*throwersCount<=0) {
            printf("invalid number of balls\n");
            exit(EXIT_FAILURE);
        }
    }
}

int throwBall(unsigned int* seed) {
    int res = 0;
    for (int i=0;i<BIN_COUNT-1;i++) {
        if (NEXT_DOUBLE(seed)>0.5) {
            res++;
        }
    }
    return res;
}

void* work(void* args) {
    Thrower* th = (Thrower*) args;
    while (1) {
        pthread_mutex_lock(th->mxBallsWaiting);
        if (*(th->ballsWaiting)>0) {
            *(th->ballsWaiting)-=1;
            pthread_mutex_unlock(th->mxBallsWaiting);
        }
        else {
            pthread_mutex_unlock(th->mxBallsWaiting);
            break;
        }

        int binNo = throwBall(&(th->seed));
        pthread_mutex_lock(&th->mxBins[binNo]);
        th->bins[binNo]+=1;
        pthread_mutex_unlock(&th->mxBins[binNo]);
        pthread_mutex_lock(th->mxBallsThrown);
        *(th->ballsThrown) +=1;
        pthread_mutex_unlock(th->mxBallsThrown);
        (th->beansPerThread)+=1;
    }
    printf("Beans per this thread: %d\n", th->beansPerThread);
    return NULL;
}

void create_throwers(Thrower* throwersArr, int throwersCount) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    for (int i=0;i<throwersCount;i++) {
        int e = pthread_create(&(throwersArr[i].tid), &attr, work, &(throwersArr[i]));
        if (e!=0) {
            ERR("pthread_create failed");
        }
    }
    pthread_attr_destroy(&attr);
}

int main(int argc, char* argv[]) {
    int throwersCount, ballsCount;
    ReadArgs(argc, argv, &throwersCount, &ballsCount);
    int ballsThrown = 0, thrownCounter = 0, ballsWaiting = ballsCount;
    pthread_mutex_t mxBallsThrown = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mxBallsWaiting = PTHREAD_MUTEX_INITIALIZER;
    int bins[BIN_COUNT] = {0};
    pthread_mutex_t mxBins[BIN_COUNT];
    for (int i=0;i<BIN_COUNT;i++) {
        if (pthread_mutex_init(&mxBins[i], NULL)) {
            ERR("pthread_mutex_init failed");
        }
    }

    Thrower* Throwers = malloc(sizeof(Thrower)*throwersCount);
    srand(time(NULL));
    for (int i=0;i<throwersCount;i++) {
        Throwers[i].seed = rand();
        Throwers[i].ballsThrown = &ballsThrown;
        Throwers[i].ballsWaiting = &ballsWaiting;
        Throwers[i].bins = bins;
        Throwers[i].mxBins = mxBins;
        Throwers[i].mxBallsThrown = &mxBallsThrown;
        Throwers[i].mxBallsWaiting = &mxBallsWaiting;
        Throwers[i].beansPerThread = 0;
    }

    create_throwers(Throwers, throwersCount);
    while (thrownCounter < ballsCount) {
        sleep(1);
        pthread_mutex_lock(&mxBallsThrown);
        thrownCounter = ballsThrown;
        pthread_mutex_unlock(&mxBallsThrown);
    }
    
    int realBallsCount = 0;
    double meanBin = 0;
    printf("Bins count:\n");
    for (int i=0;i<BIN_COUNT;i++) {
        printf("Bin%d: %d\n", i, bins[i]);
        realBallsCount+=bins[i];
        meanBin += bins[i]*i;
    }
    meanBin/=realBallsCount;
    printf("The total number of balls thrown: %d\nThe mean bin: %f\n", realBallsCount, meanBin);
    exit(EXIT_SUCCESS);
}