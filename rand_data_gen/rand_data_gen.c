#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CNT_SCALE 100

int main(int argc, char **argv)
{
    char verbs[6][10] = {"make", "eat", "store", "buy", "sell", "trash"};
    char nouns[6][10] = {"apple", "banana", "carrot", "taco", "elephant", "fish"};

    int base_cnt;
    base_cnt = argv[1] ? atoi(argv[1]) * CNT_SCALE : 10 * CNT_SCALE;

    if (base_cnt==0) 
    {
        base_cnt = 20 * CNT_SCALE;
    }
    fprintf(stderr, "base_cnt: %d\n", base_cnt);

    FILE *f = fopen("file.txt", "w");
    if (f == NULL)
    {
        printf("Error opening file!\n");
        exit(1);
    }

    for (int i = 0; i < base_cnt; i++)
    {
        int verb_idx = rand() % 6;
        int noun_idx = rand() % 6;
        fprintf(f, "%s %d %s\n", verbs[verb_idx], rand(), nouns[noun_idx]);
    }
}