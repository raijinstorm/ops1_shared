#include "lmap.h"

void usage(int argc, char** argv)
{
    fprintf(stderr, "USAGE: %s iterations bucket_count test_count worker_count output_file\n", argv[0]);
    exit(EXIT_FAILURE);
}

typedef struct Worker {
    pthread_t tid;
    unsigned int seed;
    int start;
    int end;
    int iterations;
    int bucket_count;
    int* res;
}Worker;

void readArgs(int argc, char** argv, int* iterations, int* bucket_count, int* test_count, int* worker_count) {
    if (argc!=6) {
        usage(argc,argv);
    }

    *iterations = atoi(argv[1]);
    *bucket_count = atoi(argv[2]);
    *test_count = atoi(argv[3]);
    *worker_count = atoi(argv[4]);

    if (*iterations<=0 || *bucket_count<=0 || *test_count<=0 || *worker_count<=0) {
        usage(argc,argv);
    }
}

void* work(void* args) {
    Worker* worker = (Worker*)args;

    for (int i = worker->start; i<worker->end; i++) {
        for (int j=0;j<worker->iterations;j++) {
            int bucketId = rand_r(&worker->seed)%worker->bucket_count;
            worker->res[i*worker->bucket_count + bucketId] += 1;
        }
    }
    return NULL;
}

int main(int argc, char** argv)
{
    int iterations;
    int bucket_count;
    int test_count;
    int worker_count;

    readArgs(argc, argv, &iterations, &bucket_count, &test_count, &worker_count);
    char* output_file = argv[5];

    srand(time(NULL));
    int* res = calloc(test_count*bucket_count, sizeof(int));
    if (!res) {
        ERR("calloc");
    }

    Worker* workers = malloc(sizeof(Worker)*worker_count);
    if (!workers) {
        ERR("malloc");
    }
    int chunk = test_count/worker_count;
    int r = test_count%worker_count;
    int currId = 0;

    for (int i=0;i<worker_count;i++) {
        int cnt = chunk;
        if (i<r) {
            cnt+=1;
        }
        workers[i].seed = rand();
        workers[i].start = currId;
        workers[i].end = currId + cnt;
        workers[i].iterations = iterations;
        workers[i].bucket_count = bucket_count;
        workers[i].res = res;

        currId += cnt;
        if (pthread_create(&workers[i].tid, NULL, work, &workers[i])) {
            ERR("pthread_create");
        }
    }

    for (int i=0;i<worker_count;i++) {
        if (pthread_join(workers[i].tid, NULL)) {
            ERR("pthread_join");
        }
    }

    int min_val = res[0];
    int max_val = res[0];
    for (int i = 0;i<test_count*bucket_count;i++) {
        if (res[i] < min_val) {
            min_val = res[i];
        }
        if (res[i] > max_val) {
            max_val = res[i];
        }
    }
    FILE* file = fopen(output_file, "w");
    if (file==NULL) {
        ERR("fopen");
    }
    pgm_header(file, bucket_count, test_count);
    for (int i=0;i<test_count*bucket_count;i++) {
        int p = 0;
        if (max_val>min_val) {
            p = scale_i(res[i], min_val, max_val, 0, 255);
        }
        fprintf(file, "%d", p);
        if ((i+1)%bucket_count==0) {
            fprintf(file, "%c", '\n');
        }
        else {
            fprintf(file, "%c", ' ');
        }
    }
    if(fclose(file)) {
        ERR("fclose");
    }
    free(res);
    free(workers);
    return 0;
}
