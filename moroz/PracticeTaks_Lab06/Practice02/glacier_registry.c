#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TEMP_SHM_NAME "/glacier-temperature"

#define MIN_WORKERS 2
#define MAX_WORKERS 20
#define MIN_DIM 2
#define MAX_DIM 16

#define ERR(source)                             \
    do                                          \
    {                                           \
        fprintf(stderr, "%s:%d\n",              \
                __FILE__, __LINE__);            \
        perror(source);                         \
        kill(0, SIGKILL);                       \
        exit(EXIT_FAILURE);                     \
    } while (0)


typedef struct
{
    int r1;
    int c1;
    int r2;
    int c2;
} branch_t;

typedef struct Drawers
{
    int R;
    int C;
    int drawers[MAX_DIM*MAX_DIM];
    int common_branches[MAX_DIM*MAX_DIM][MAX_DIM*MAX_DIM];
    int deg[MAX_DIM*MAX_DIM];
}Drawers;


typedef struct SharedState
{
    int cnt;
    pthread_barrier_t barrier;
    int end_flag;
    pthread_mutex_t end_mtx;
    pthread_mutex_t drawers_mtx[];
}SharedState;

void usage(char* program_name)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "\t%s p layout_file\n", program_name);
    fprintf(stderr, "\t p - number of operators, %d <= p <= %d\n", MIN_WORKERS, MAX_WORKERS);
    fprintf(stderr, "\t layout_file - path to input file\n");
    exit(EXIT_FAILURE);
}

void ms_sleep(unsigned int milli)
{
    time_t sec = (time_t)(milli / 1000);
    milli = milli - (sec * 1000);

    struct timespec ts = {0};
    ts.tv_sec = sec;
    ts.tv_nsec = milli * 1000000L;

    if (nanosleep(&ts, &ts))
        ERR("nanosleep");
}

void print_drawers(int* layout, int rows, int cols)
{
    for (int i = 0; i < rows; ++i)
    {
        for (int j = 0; j < cols; ++j)
            printf("%4d ", layout[i * cols + j]);
        printf("\n");
    }
}

Drawers* read_file(char* filename)
{
    FILE* file = fopen(filename, "r");
    if (!file)
    {
        ERR("fopen");
    }
    char buf[256];
    fgets(buf, sizeof(buf), file);
    int R,C;
    if (sscanf(buf, "%d %d", &R, &C) != 2)
    {
        ERR("sscanf");
    }
    if (R < MIN_DIM || R > MAX_DIM || C>MAX_DIM || C<MIN_DIM)
    {
        fprintf(stderr,"Invalid number of rows or columns\n");
        exit(EXIT_FAILURE);
    }

    int vault_memsize = sizeof(Drawers);
    Drawers* drawers = (Drawers*)mmap(NULL,vault_memsize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (drawers == MAP_FAILED)
    {
        ERR("mmap");
    }
    drawers->C = C;
    drawers->R = R;
    for (int i=0;i<MAX_DIM;i++)
    {
        drawers->deg[i] = 0;
    }

    int i=0;
    for (int j=0;j<R;j++)
    {
        if (!fgets(buf,sizeof(buf), file))
        {
            fprintf(stderr,"Incorrect file formatting in a vault\n");
            exit(EXIT_FAILURE);
        }
        int temp[MAX_DIM];
        int t_id = 0;
        for (char *p = strtok(buf," "); p != NULL; p = strtok(NULL, " "))
        {
            int val;
            if (sscanf(p,"%d", &val)!=1)
            {
                ERR("sscanf");
            }
            if (val<1 || val>R*C){
                fprintf(stderr,"Incorrect number of drawers in a vault\n");
                exit(EXIT_FAILURE);
            }
            if (i>=R*C)
            {
                fprintf(stderr,"Incorrect number of drawers in a vault\n");
                exit(EXIT_FAILURE);
            }
            if (t_id>=MAX_DIM)
            {
                fprintf(stderr,"Incorrect number of drawers in a vault\n");
                exit(EXIT_FAILURE);
            }
            temp[t_id] = val;
            t_id++;
        }

        if (t_id!=C)
        {
            fprintf(stderr,"Incorrect number of drawers in a vault\n");
            exit(EXIT_FAILURE);
        }

        for (int k=0;k<C;k++)
        {
            drawers->drawers[i] = temp[k];
            i++;
        }
    }

    fgets(buf,sizeof(buf), file);
    int M;
    if (sscanf(buf,"%d", &M) != 1)
    {
        ERR("sscanf");
    }


    for (int i=0;i<M;i++)
    {
        fgets(buf,sizeof(buf), file);
        int r1,c1,r2,c2;
        if (sscanf(buf, "%d %d %d %d", &r1, &c1, &r2, &c2)!=4)
        {
            ERR("sscanf");
        }

        int drawer = r1*C+c1;
        int neighbour = r2*C+c2;
        drawers->common_branches[drawer][drawers->deg[drawer]] = neighbour;
        drawers->deg[drawer]++;
    }

    if (fclose(file)!=0)
    {
        ERR("fclose");
    }
    return drawers;
}

void child_work(Drawers* drawers, SharedState* state)
{
    printf("[%d] Operator reports duty\n", getpid());

    pthread_barrier_wait(&state->barrier);
    int fd = shm_open(TEMP_SHM_NAME, O_RDWR, 0666);
    if (fd<0)
    {
        ERR("shm_open");
    }
    double* temperatures = (double*)mmap(NULL, state->cnt*sizeof(double), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (temperatures == MAP_FAILED)
    {
        ERR("mmap");
    }

    int drawers_num = (drawers->C)*(drawers->R);
    while (1)
    {
        pthread_mutex_lock(&state->end_mtx);
        if (state->end_flag)
        {
            printf("[%d] Shift ended\n", getpid());
            pthread_mutex_unlock(&state->end_mtx);
            break;
        }
        pthread_mutex_unlock(&state->end_mtx);

        // int sorted_flag = 1;
        for (int i=0;i<drawers_num;i++)
        {
            int correct_drawer_value = i+1;
            pthread_mutex_lock(&state->drawers_mtx[i]);
            int is_correct = (drawers->drawers[i] == correct_drawer_value)?1:0;
            pthread_mutex_unlock(&state->drawers_mtx[i]);
            if (!is_correct)
            {
                // sorted_flag = 0;
                int correct_idx = -1;
                for (int j=0;j<drawers_num;j++)
                {
                    pthread_mutex_lock(&state->drawers_mtx[j]);
                    if (drawers->drawers[j] == correct_drawer_value)
                    {
                        correct_idx = j;
                    }
                    pthread_mutex_unlock(&state->drawers_mtx[j]);
                    if (correct_idx != -1)
                    {
                        break;
                    }
                }

                if (correct_idx != -1)
                {
                    int id = i;
                    int first = i;
                    int second = correct_idx;
                    if (id>correct_idx)
                    {
                        int temp = id;
                        id = correct_idx;
                        correct_idx = temp;
                    }
                    pthread_mutex_lock(&state->drawers_mtx[id]);
                    pthread_mutex_lock(&state->drawers_mtx[correct_idx]);

                    if (drawers->drawers[first] == correct_drawer_value || drawers->drawers[second]!=correct_drawer_value)
                    {
                        pthread_mutex_unlock(&state->drawers_mtx[id]);
                        pthread_mutex_unlock(&state->drawers_mtx[correct_idx]);
                        continue;
                    }
                    int temp = drawers->drawers[id];
                    drawers->drawers[id] = drawers->drawers[correct_idx];
                    drawers->drawers[correct_idx] = temp;
                    temperatures[id] += 0.25;
                    temperatures[correct_idx] += 0.25;

                    pthread_mutex_unlock(&state->drawers_mtx[correct_idx]);
                    pthread_mutex_unlock(&state->drawers_mtx[id]);

                    int r1 = id/drawers->C;
                    int c1 = id%drawers->C;
                    int r2 = correct_idx/drawers->C;
                    int c2 = correct_idx%drawers->C;
                    printf("[%d] Swapped [%d, %d] and [%d, %d]\n", getpid(), r1, c1, r2, c2);
                    ms_sleep(200);
                }
            }
        }
        // if (sorted_flag) break;
    }


    int state_memsize = sizeof(SharedState) + sizeof(pthread_mutex_t)*(drawers->C)*(drawers->R);
    if (munmap(state, state_memsize) == -1)
    {
        ERR("munmap");
    }
    if (munmap(drawers, sizeof(Drawers)) == -1)
    {
        ERR("munmap");
    }
    if (msync(temperatures, state->cnt*sizeof(double), MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(temperatures, state->cnt*sizeof(double)) == -1)
    {
        ERR("munmap");
    }
    close(fd);
}

void manager_work(SharedState* state, Drawers* drawers, double* temperatures)
{
    printf("[%d] Supervisor starts shift\n", getpid());

    while (1)
    {
        ms_sleep(500);

        for (int i=0;i<state->cnt;i++)
        {
            pthread_mutex_lock(&state->drawers_mtx[i]);
        }

        int flag_sorted = 1;
        for (int i=1;i<state->cnt;i++)
        {
            if (drawers->drawers[i-1]>drawers->drawers[i])
            {
                flag_sorted = 0;
                break;
            }
        }

        double highest_temp = temperatures[0];
        for (int i=1;i<state->cnt;i++)
        {
            if (temperatures[i]>highest_temp)
            {
                highest_temp = temperatures[i];
            }
        }
        for (int i=0;i<state->cnt;i++)
        {
            printf("%lf ", temperatures[i]);
        }

        for (int i=state->cnt-1;i>=0;i--)
        {
            pthread_mutex_unlock(&state->drawers_mtx[i]);
        }
        if (flag_sorted)
        {
            pthread_mutex_lock(&state->end_mtx);
            state->end_flag = 1;
            pthread_mutex_unlock(&state->end_mtx);
            printf("[%d] Registry restored\n", getpid());
            break;
        }
        if (highest_temp>-16.0)
        {
            pthread_mutex_lock(&state->end_mtx);
            state->end_flag = 1;
            pthread_mutex_unlock(&state->end_mtx);
            printf("[%d] Cooling failure\n", getpid());
            break;
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 3)
        usage(argv[0]);

    int P = atoi(argv[1]);
    if (P < MIN_WORKERS || P > MAX_WORKERS)
        usage(argv[0]);

    shm_unlink(TEMP_SHM_NAME);

    char* filename = argv[2];
    Drawers* drawers = read_file(filename);
    print_drawers(drawers->drawers, drawers->R, drawers->C);

    int state_memsize = sizeof(SharedState) + sizeof(pthread_mutex_t)*(drawers->C)*(drawers->R);
    SharedState* state = (SharedState*)mmap(NULL, state_memsize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (state == MAP_FAILED)
    {
        ERR("mmap");
    }
    state->cnt = (drawers->C)*(drawers->R);
    state->end_flag = 0;
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    for (int i=0;i<state->cnt;i++)
    {
        pthread_mutex_init(&state->drawers_mtx[i], &mutex_attr);
    }
    pthread_mutex_init(&state->end_mtx, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    pthread_barrierattr_t barrier_attr;
    pthread_barrierattr_init(&barrier_attr);
    pthread_barrierattr_setpshared(&barrier_attr, PTHREAD_PROCESS_SHARED);
    pthread_barrier_init(&state->barrier, &barrier_attr, P+1);
    pthread_barrierattr_destroy(&barrier_attr);

    for (int i=0;i<P;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            child_work(drawers, state);

            exit(EXIT_SUCCESS);
        }
    }

    int fd = shm_open(TEMP_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1)
    {
        ERR("shm_open");
    }
    if (ftruncate(fd,state->cnt*sizeof(double)) == -1)
    {
        ERR("ftruncate");
    }
    double* temperatures = (double*)mmap(NULL, state->cnt*sizeof(double), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (temperatures == MAP_FAILED)
    {
        ERR("mmap");
    }
    for (int i=0;i<state->cnt;i++)
    {
        temperatures[i] = -18.0;
    }

    pid_t manager_pid = fork();
    if (manager_pid < 0) ERR("fork");
    if (manager_pid == 0)
    {
        manager_work(state, drawers, temperatures);
        exit(EXIT_SUCCESS);
    }

    pthread_barrier_wait(&state->barrier);

    ms_sleep(300);
    for (int i=0;i<state->cnt;i++)
    {
        pthread_mutex_lock(&state->drawers_mtx[i]);
    }
    print_drawers(drawers->drawers, drawers->R, drawers->C);
    for (int i=state->cnt-1;i>=0;i--)
    {
        pthread_mutex_unlock(&state->drawers_mtx[i]);
    }

    while (wait(NULL)>0);

    print_drawers(drawers->drawers, drawers->R, drawers->C);

    for (int i=0;i<state->cnt;i++)
    {
        pthread_mutex_destroy(&state->drawers_mtx[i]);
    }
    pthread_mutex_destroy(&state->end_mtx);
    pthread_barrier_destroy(&state->barrier);
    if (msync(temperatures, state->cnt*sizeof(double), MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(temperatures, state->cnt*sizeof(double)) == -1)
    {
        ERR("munmap");
    }
    if (munmap(state, state_memsize) == -1)
    {
        ERR("munmap");
    }
    if (munmap(drawers, sizeof(Drawers)) == -1)
    {
        ERR("munmap");
    }
    shm_unlink(TEMP_SHM_NAME);
    close(fd);
    return 0;
}