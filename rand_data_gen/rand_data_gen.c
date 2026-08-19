/**
 * @file rand_data_gen.c
 * @brief Generates synthetic log data for testing the SparkySIEM forwarder.
 *
 * Writes lines of the form "<verb> <number> <noun>" to file.txt, for example
 * "eat 1681692777 elephant". Point the forwarder at that file, rerun this program or
 * append to the file, and watch the changes arrive on the Kafka topic.
 *
 * Usage:
 *   ./rand_data_gen [count]     # count is multiplied by CNT_SCALE
 *
 *   ./rand_data_gen             # no argument: 10 * 100 = 1000 lines
 *   ./rand_data_gen 5           # 5 * 100  =  500 lines
 *   ./rand_data_gen abc         # atoi() gives 0, so the fallback writes 2000 lines
 *
 * @note The output goes to file.txt in the *current working directory*, not next to
 *       the binary, and it overwrites any file.txt already there.
 * @note srand() is never called, so rand() starts from the same seed on every run and
 *       the "random" data is identical each time. That makes runs reproducible; call
 *       srand(time(NULL)) if you want them to differ.
 *
 * Build:
 *   gcc -Wall -o rand_data_gen rand_data_gen.c
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Multiplier applied to the line count, so a small argument yields a useful file.
#define CNT_SCALE 100

int main(int argc, char **argv)
{
    // Word pools the lines are built from. The % 6 below depends on these holding
    // exactly six entries each.
    char verbs[6][10] = {"make", "eat", "store", "buy", "sell", "trash"};
    char nouns[6][10] = {"apple", "banana", "carrot", "taco", "elephant", "fish"};

    int base_cnt;
    // argv[argc] is guaranteed NULL, so this reads as "if an argument was supplied".
    // argc itself is unused.
    base_cnt = argv[1] ? atoi(argv[1]) * CNT_SCALE : 10 * CNT_SCALE;

    // atoi() returns 0 for anything non-numeric, and for a literal "0". Either way
    // fall back to a file big enough to be worth monitoring.
    if (base_cnt==0)
    {
        base_cnt = 20 * CNT_SCALE;
    }
    // Reported on stderr so it does not end up in a redirected data stream.
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
        // The middle field is just another rand() value, standing in for whatever a
        // real log line would carry there.
        fprintf(f, "%s %d %s\n", verbs[verb_idx], rand(), nouns[noun_idx]);
    }
    // No fclose() here: returning from main flushes and closes the stream. Add one if
    // this ever grows into something longer lived.
}
