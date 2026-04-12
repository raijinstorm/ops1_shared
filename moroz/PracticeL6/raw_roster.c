#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include<time.h>

#define RANGE 25

void task1()
{
    FILE* src = fopen("raw_roster.txt", "r");
    if (src == NULL)
    {
        perror("fopen");
    }
    FILE* dst = fopen("clean_roster.txt", "w");
    if (dst == NULL)
    {
        perror("fopen");
    }
    fprintf(dst, "Name   |   Age   |   Role\n");

    char buffer[128];
    while (fgets(buffer, sizeof(buffer), src)!=NULL)
    {
        char name[64];
        int age;
        char role[64];
        if (sscanf(buffer, "Name:%[^,], Age:%d, Role:%s", name, &age, role)!=3)
        {
            perror("sscanf");
        }
        fprintf(dst, "%s  %d  %s\n", name, age, role);
    }

    if (fclose(src))
    {
        perror("fclose");
    }
    if (fclose(dst))
    {
        perror("fclose");
    }
}

void log_incident(int num_aisle, char* culprit, char* action)
{
    char filename[128];
    snprintf(filename, sizeof(filename), "log_aisle_%d.txt", num_aisle);
    FILE* dst = fopen(filename, "a");
    if (dst == NULL)
    {
        perror("fopen");
    }

    char message[512];
    snprintf(message, sizeof(message), "[%s]: %s", culprit, action);
    fprintf(dst, "%s\n", message);
    if (fclose(dst))
    {
        perror("fclose");
    }
}

void task3()
{
    FILE* src = fopen("inventory.csv", "r");
    if (src == NULL) perror("fopen");
    FILE* dst = fopen("inventory_clean.txt", "w");
    if (dst == NULL) perror("fopen");
    fprintf(dst,"ID  |  Name  |  Price  |  Location\n");

    char* buf = NULL;
    size_t size = 0;
    int bytes_read;
    while ((bytes_read = getline(&buf, &size, src))>0){
        int id;
        char name[64];
        float price;
        char location[64];

        if (sscanf(buf, "%d;%[^;];%f;%[^\n]", &id, name, &price, location)!=4)
        {
            perror("sscanf");
        }
        fprintf(dst, "%d  %s  %.3f  %s\n", id, name, price, location);
    }

    if (fclose(src)) perror("fclose");
    if (fclose(dst)) perror("fclose");
    free(buf);
}

int main()
{
    task1();

    srand(time(NULL));
    for (int i=0;i<5;i++)
    {
        int idx = rand()%3;
        int rnd_action_len = rand()%10;
        int rnd_culprit_len = rand()%15;

        char action[rnd_action_len+1];
        char culprit[rnd_culprit_len+1];
        for (int j=0;j<rnd_action_len;j++)
        {
            action[j] = (char)(rand()%RANGE + 97);
        }
        action[rnd_action_len] = '\0';
        for (int j=0;j<rnd_culprit_len;j++)
        {
            culprit[j] = (char)(rand()%RANGE + 97);
        }
        culprit[rnd_culprit_len] = '\0';
        log_incident(idx, culprit, action);
    }

    task3();
    return 0;
}