#include<stdio.h>
#include<pthread.h>
#include<stdarg.h>
#include<stddef.h>
#include<stdlib.h>
#include<math.h>
#include<string.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define DEFAULT_THREADCOUNT 10
#define DEFAULT_SAMPLESIZE 100

typedef struct Estimation {
    pthread_t tid;
    int sample_size;
    unsigned int seed;
}Estimation;

void read_input(int* threadCount,int* sampleSize, int argc, char* argv[]) {
    *threadCount = DEFAULT_THREADCOUNT;
    *sampleSize = DEFAULT_SAMPLESIZE;
    if (argc >= 2) {
        *threadCount = atoi(argv[1]);
        if (*threadCount <= 0) {
            printf("Invalid number of threads\n");
            exit(EXIT_FAILURE);
        }
    }

    if (argc>=3) {
        *sampleSize = atoi(argv[2]);
        if (*sampleSize <= 0) {
            printf("Invalid sample size\n");
            exit(EXIT_FAILURE);
        }
    }
}

void* pi_approximation(void* args) {
    Estimation* estimation = (Estimation*) args;
    double* result = (double*) malloc(sizeof(double));
    if (result == NULL) {
        ERR("malloc");
        //exit(EXIT_FAILURE);
    }

    int InternalCount = 0;
    for (int i=0;i<estimation->sample_size;i++) {
        double x = (double)rand_r(&(estimation->seed)) / RAND_MAX;
        double y = (double)rand_r(&(estimation->seed))/RAND_MAX;
        if (x*x+y*y <= 1.0) {
            InternalCount++;
        }
    }
    *result = 4.0 * (double)InternalCount / estimation->sample_size;
    return result;
}

int main(int argc, char* argv[]) {
    int threadCount, sampleSize;
    read_input(&threadCount, &sampleSize, argc, argv);
    Estimation* estimations = (Estimation*)malloc(sizeof(Estimation) * threadCount);
    if (estimations == NULL) {
        ERR("malloc");
    }
    srand(time(NULL));
    for (int i=0;i<threadCount;i++) {
        estimations[i].sample_size = sampleSize;
        estimations[i].seed = rand();
    }

    for (int i = 0;i<threadCount;i++) {
        int e = pthread_create(&(estimations[i].tid), NULL, pi_approximation, &(estimations[i]));
        if (e!=0) {
            ERR("pthread_create");
        }
    }
    double cumulativeResult = 0.0;
    double* tempRes;
    for (int i=0;i<threadCount;i++) {
        int e = pthread_join(estimations[i].tid, (void*)&tempRes);
        if (e!=0) {
            ERR("pthread_join");
        }
        if (tempRes != NULL) {
            cumulativeResult += *tempRes;
            free(tempRes);
        }
    }
    printf("PI ~= %f\n", cumulativeResult/threadCount);
    free(estimations);
}