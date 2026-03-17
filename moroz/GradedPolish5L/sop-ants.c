#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MAX_GRAPH_NODES 32
#define MAX_PATH_LENGTH (2 * MAX_GRAPH_NODES)

#define FIFO_NAME "/tmp/colony_fifo"

volatile sig_atomic_t my_read_fd=-1;

void sigint_handler(int sig) {
    if (my_read_fd != -1) {
        close(my_read_fd);
        my_read_fd = -1;
    }
}

int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}

void msleep(int ms)
{
    struct timespec tt;
    tt.tv_sec = ms / 1000;
    tt.tv_nsec = (ms % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1)
    {
    }
}

void usage(int argc, char* argv[])
{
    printf("%s graph start dest\n", argv[0]);
    printf("  graph - path to file containing colony graph\n");
    printf("  start - starting node index\n");
    printf("  dest - destination node index\n");
    exit(EXIT_FAILURE);
}

typedef struct graph {
    int num_vertices;
    int deg[MAX_GRAPH_NODES];
    int neighbors[MAX_GRAPH_NODES][MAX_GRAPH_NODES];
}graph;

void read_graph(const char* filename, graph* graph) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        ERR("fopen");
    }

    if (fscanf(f, "%d", &graph->num_vertices) != 1) {
        ERR("fscanf");
    }

    for (int i = 0; i < graph->num_vertices; i++) {
        graph->deg[i] = 0;
    }
    int u,v;
    while (fscanf(f, "%d %d", &u, &v) == 2) {
        if (u>=0 && u<graph->num_vertices && graph->deg[u] < MAX_GRAPH_NODES) {
            graph->neighbors[u][graph->deg[u]++]=v;
        }
    }
    fclose(f);
}

void child_work(int id, graph* graph, int pipes[MAX_GRAPH_NODES][2]) {
    set_handler(sigint_handler, SIGINT);

    my_read_fd = pipes[id][0];

    for (int i=0;i<graph->num_vertices;i++) {
        if (i==id) {
            close(pipes[i][1]);
        }
        else {
            close(pipes[i][0]);

            int is_neighbor = 0;
            for (int j=0;j<graph->deg[id];j++) {
                if (graph->neighbors[id][j] == i) {
                    is_neighbor = 1;
                    break;
                }
            }
            if (!is_neighbor) {
                close(pipes[i][1]);
            }
        }
    }

    printf("%d: ", id);
    for (int i=0;i<graph->deg[id];i++) {
        printf(" %d", graph->neighbors[id][i]);
    }
    printf("\n");

    char buffer;
    while (read(my_read_fd, &buffer, 1) > 0) {
    }

    for (int i=0;i<graph->deg[id];i++) {
        close(pipes[graph->neighbors[id][i]][1]);
    }

    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[])
{
    if (argc != 4)
        usage(argc, argv);

    graph graph;
    read_graph(argv[1], &graph);

    int start_node=atoi(argv[2]);
    //int end_node=atoi(argv[3]);
    int pipes[MAX_GRAPH_NODES][2];
    for (int i=0;i<graph.num_vertices;i++) {
        if (pipe(pipes[i]) == -1) {
            ERR("pipe");
        }
    }

    set_handler(sigint_handler, SIGINT);

    for (int i=0;i<graph.num_vertices;i++) {
        pid_t pid = fork();
        if (pid == 0) {
            child_work(i, &graph, pipes);
        }
        if (pid < 0) {
            ERR("fork");
        }
    }

    for (int i=0;i<graph.num_vertices;i++) {
        if (close(pipes[i][0])) ERR("close");
        if (i != start_node) {
            if (close(pipes[i][1])) ERR("close");
        }
    }

    while ( wait(NULL) > 0 || errno == EINTR);

    if (close(pipes[start_node][1])) ERR("close");
    exit(EXIT_SUCCESS);
}