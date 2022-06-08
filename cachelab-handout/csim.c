#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include "cachelab.h"
#include "csim_lru.h"

int hit = 0, miss = 0, eviction = 0;
int s = 0, E = 0, b = 0;
char* trace_file_path;

void findCache(int tag, LRUCache* cache) {
    if (lRUCacheGet(cache, tag)) {
        hit++;
        return;
    }

    miss++;
    if (!lRUCachePut(cache, tag)) {
        eviction++;
    }
}

int read_opt(int argc, char** argv) {
    int op;
    while ((op = getopt(argc, argv, "hvs:E:b:t:")) != -1) {
        switch (op) {
            case 'v':
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 'E':
                E = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 't':
                trace_file_path = optarg;
                break;
            default:
                return 0;
        }
    }
    return 1;
}

int main(int argc, char** argv) {
    if (!read_opt(argc, argv))
        return 1;

    LRUCache** caches = calloc(1 << s, sizeof(LRUCache*));
    for (int i = 0; i < (1 << s); i++) {
        caches[i] = lRUCacheCreate(E);
    }

    FILE* fp;
    fp = fopen(trace_file_path, "r");

    char oper[256];
    long address;
    int size;
    while (fscanf(fp, "%s %lx,%d", oper, &address, &size) == 3) {
        if (oper[0] == 'I')
            continue;

        int group_idx = (address >> b) & ((1 << s) - 1);
        int tag = (int)(address >> (b + s));

        findCache(tag, caches[group_idx]);
        if (oper[0] == 'M')
            hit++;
    }
    printSummary(hit, miss, eviction);
    return 0;
}
