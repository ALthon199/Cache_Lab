#include "cachelab.h"
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum { HIT, MISS, EVICTION } Result;

typedef struct {
    unsigned char valid;
    unsigned char dirty;
    unsigned long tag;
    unsigned long recent_idx;
} Line;

typedef struct {
    unsigned long block_bits;
    unsigned long set_bits;
    unsigned long set_len;
    unsigned long line_len;
    Line *lines;
} Cache;

void free_cache(Cache *cache) {
    if (cache == NULL) {
        return;
    }
    if (cache->lines == NULL) {
        free(cache);
        return;
    }

    free(cache->lines);
    free(cache);
    return;
}

// Returns NULL if fail else Cache
Cache *create_cache(unsigned long block_bits, unsigned long set_bits,
                    unsigned long lines_per_set) {

    Cache *new_cache = NULL;
    new_cache = calloc(1, sizeof(Cache));
    if (new_cache == NULL) {
        free_cache(new_cache);
        printf("Allocation Failure");
        return NULL;
    }

    new_cache->block_bits = block_bits;
    new_cache->set_bits = set_bits;
    new_cache->set_len = 1 << set_bits;
    new_cache->line_len = lines_per_set;

    new_cache->lines =
        calloc(sizeof(Line), new_cache->set_len * new_cache->line_len);

    if (new_cache->lines == NULL) {
        free_cache(new_cache);
        printf("Allocation Failure");
        return NULL;
    }

    return new_cache;
}

void update_lines(Cache *cache, unsigned long set_idx, unsigned long line_idx) {
    unsigned long line_len = cache->line_len;

    // All recent idx less curr_idx need to be incremented
    unsigned long curr_idx =
        cache->lines[set_idx * line_len + line_idx].recent_idx;

    for (unsigned long i = 0; i < line_idx; i++) {
        Line *line = &cache->lines[set_idx * line_len + i];

        if (line->valid == 1 && line->recent_idx < curr_idx) {
            line->recent_idx += 1;
        }
    }

    for (unsigned long i = line_idx + 1; i < line_len; i++) {
        Line *line = &cache->lines[set_idx * line_len + i];

        if (line->valid == 1 && line->recent_idx < curr_idx) {
            line->recent_idx += 1;
        }
    }

    cache->lines[set_idx * line_len + line_idx].recent_idx = 0;
    return;
}

Result update_set(Cache *cache, unsigned long set_idx, unsigned long tag,
                  int is_write, csim_stats_t *stats) {

    unsigned long line_len = cache->line_len;
    // First find if already in cache
    for (unsigned long i = 0; i < line_len; i++) {
        Line *line = &cache->lines[set_idx * line_len + i];

        if (line->valid == 1 && line->tag == tag) {
            // Already in cache so just update recent_idx

            update_lines(cache, set_idx, i);
            stats->hits += 1;

            if (is_write && line->dirty == 0) {
                line->dirty = 1;
                stats->dirty_bytes += (1 << (cache->block_bits));
            }

            return HIT;
        }
    }

    // So not in cache, from here we first see if there's a free block

    for (unsigned long i = 0; i < line_len; i++) {

        Line *line = &cache->lines[set_idx * line_len + i];
        if (line->valid == 0) {
            // Find the greatest recent index among valid lines
            unsigned long greatest_recent_idx = 0;
            for (unsigned long j = 0; j < line_len; j++) {
                Line *line2 = &cache->lines[set_idx * line_len + j];
                if (line2->valid == 1 &&
                    line2->recent_idx > (unsigned long)greatest_recent_idx) {
                    greatest_recent_idx = line2->recent_idx;
                }
            }

            line->recent_idx = (unsigned long)(greatest_recent_idx + 1);

            line->valid = 1;
            line->tag = tag;
            line->dirty = is_write ? 1 : 0;
            update_lines(cache, set_idx, i);
            stats->misses += 1;
            stats->dirty_bytes += is_write ? (1 << (cache->block_bits)) : 0;
            return MISS;
        }
    }

    // We must evict
    unsigned long idx_to_remove = 0;
    for (unsigned long i = 1; i < line_len; i++) {

        Line *line = &cache->lines[set_idx * line_len + i];
        Line *recent = &cache->lines[set_idx * line_len + idx_to_remove];
        if (line->recent_idx > recent->recent_idx) {
            idx_to_remove = i;
        }
    }

    // Because we EVICT CHECK TO MAKE SURE IT'S or NOT DIRTY
    Line *line = &cache->lines[set_idx * line_len + idx_to_remove];

    if (line->dirty == 1) {
        // update stats
        stats->dirty_evictions += (1 << (cache->block_bits));
        stats->dirty_bytes -= (1 << (cache->block_bits));
    }
    line->valid = 1;
    line->tag = tag;
    line->dirty = is_write ? 1 : 0;
    stats->misses += 1;
    stats->evictions += 1;
    stats->dirty_bytes += (is_write ? 1 : 0) * (1 << (cache->block_bits));
    update_lines(cache, set_idx, idx_to_remove);
    return EVICTION;
}

Result process_op(Cache *cache, char *op, unsigned long addr,
                  csim_stats_t *stats) {
    unsigned long block_bits = cache->block_bits;
    unsigned long set_bits = cache->set_bits;
    Result result;

    if (*op == 'S') {
        unsigned long set_idx = (addr >> block_bits) & ((1L << set_bits) - 1);
        unsigned long tag = addr & (~0UL << (block_bits + set_bits));
        result = update_set(cache, set_idx, tag, 1, stats);
    } else {
        unsigned long set_idx = (addr >> block_bits) & ((1L << set_bits) - 1);
        unsigned long tag = addr & (~0UL << (block_bits + set_bits));
        result = update_set(cache, set_idx, tag, 0, stats);
    }

    return result;
}

void print_help(void) {
    printf("Mandatory arguments missing or zero.\n"
           ":Usage: ./csim -ref [-v] -s <s> -E <E> -b <b> -t <trace>\n"
           "./csim -ref -h\n"
           "-h Print this help message and exit\n"
           "-v Verbose mode: report effects of each memory operation\n"
           "-s <s> Number of set index bits (there are 2**s sets)\n"
           "-b <b> Number of block bits (there are 2**b blocks)\n"
           "-E <E> Number of lines per set ( associativity )\n"
           "-t <trace > File name of the memory trace to process\n"
           "The -s, -b, -E, and -t options must be supplied for all "
           "simulations .");
}

int process_trace_file(const char *trace, csim_stats_t *stats, Cache *cache,
                       char verbose) {
    FILE *tfp = fopen(trace, "rt");
    if (!tfp) {
        fprintf(stderr, "Error opening %s : %s\n", trace, strerror(errno));
        return 1;
    }
    int LINELEN = 512;
    char linebuf[LINELEN]; // How big should LINELEN be?
    int parse_error = 0;

    // Two seperate delims to not allow users to use comma as spaces
    const char *space_delim = " ";
    const char *comma_delim = ",";
    while (fgets(linebuf, LINELEN, tfp)) {

        // Gets Operator and checks validity
        char *op = strtok(linebuf, space_delim);
        if (op == NULL) {
            printf("Error reading trace file\n");
            return 1;
        }
        if (*op == '\n') {
            continue;
        }
        if (strcmp(op, "L") != 0 && strcmp(op, "S") != 0) {
            printf("Invalid operation in trace file\n");
            return 1;
        }

        // Gets addr and check validity
        char *addr = strtok(NULL, comma_delim);
        errno = 0;
        if (addr == NULL) {
            printf("Error reading trace file\n");
            return 1;
        }

        unsigned long address = strtoul(addr, NULL, 16);

        if (errno != 0) {
            printf("Error reading trace file\n");
            return 1;
        }

        // Gets number of bytes and check validity

        char *bytes_c = strtok(NULL, space_delim);
        errno = 0;
        if (bytes_c == NULL) {
            printf("Error reading trace file\n");
            return 1;
        }

        unsigned long bytes = strtoul(bytes_c, NULL, 16);

        if (errno != 0) {
            printf("Error reading trace file\n");
            return 1;
        }

        Result result = process_op(cache, op, address, stats);

        if (verbose) {
            printf("%s %lx,%lx ", op, address, bytes);
            if (result == HIT) {
                printf("hit\n");
            } else if (result == MISS) {
                printf("miss\n");
            } else {
                printf("miss eviction\n");
            }
        }
    }
    printSummary(stats);
    fclose(tfp);
    // Why do I return parse_error here and not 0?
    return parse_error;
}

int main(int argc, char **argv) {
    unsigned long set_bits;
    unsigned long block_bits;
    unsigned long lines_per_set;
    char verbose = 0;
    const char *file;
    int opt;

    while ((opt = getopt(argc, argv, "hvs:b:E:t:")) != -1) {
        switch (opt) {
        case 'h':
            print_help();
            return 0;
        case 'v':
            verbose = 1;
            break;
        case 's':
            set_bits = strtoul(optarg, NULL, 10);
            break;
        case 'b':
            block_bits = strtoul(optarg, NULL, 10);
            break;
        case 'E':
            lines_per_set = strtoul(optarg, NULL, 10);
            break;
        case 't':
            file = optarg;
            break;
        default:
            print_help();
            return 0;
        }
    }

    if (argc < 9) {
        print_help();
        return 0;
    }

    if (set_bits + block_bits >= 64) {
        printf("Error: s + b is too large (s = %lu, b = %lu)\n", set_bits,
               block_bits);
        return 0;
    }

    Cache *cache = create_cache(block_bits, set_bits, lines_per_set);
    csim_stats_t *stats = calloc(sizeof(csim_stats_t), 1);
    if (stats == NULL) {
        printf("Allocation Failure");
        return 1;
    }

    process_trace_file(file, stats, cache, verbose);
    free_cache(cache);
    free(stats);
    return 0;
}
