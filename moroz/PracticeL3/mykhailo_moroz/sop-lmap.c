#include "lmap.h"

void usage(int argc, char** argv)
{
    fprintf(stderr, "USAGE: %s iterations bucket_count test_count worker_count output_file\n", argv[0]);
    exit(EXIT_FAILURE);
}
int main(int argc, char** argv)
{
    if (argc!=6) {
        usage(argc,argv);
    }

    int iterations = atoi(argv[1]);
    int bucket_count = atoi(argv[2]);
    int test_count = atoi(argv[3]);
    int worker_count = atoi(argv[4]);
    char* output_file = argv[5];

    if (iterations<=0 || bucket_count<=0 || test_count<=0 || worker_count<=0) {
        usage(argc,argv);
    }

    srand(time(NULL));
    int* res = calloc(test_count*bucket_count, sizeof(int));
    if (!res) {
        ERR("calloc");
    }

    for (int i=0;i<test_count;i++) {
        for (int j=0;j<iterations;j++) {
            int bucketId = rand() % bucket_count;
            res[i*bucket_count + bucketId] +=1;
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
    return 0;
}
