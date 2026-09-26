#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>


void print_help(void){
    printf( "Mandatory arguments missing or zero.\n"
            ":Usage: ./csim -ref [-v] -s <s> -E <E> -b <b> -t <trace>\n"
            "./csim -ref -h\n"
            "-h Print this help message and exit\n"
            "-v Verbose mode: report effects of each memory operation\n"
            "-s <s> Number of set index bits (there are 2**s sets)\n"
            "-b <b> Number of block bits (there are 2**b blocks)\n"
            "-E <E> Number of lines per set ( associativity )\n"
            "-t <trace > File name of the memory trace to process\n"
            "The -s, -b, -E, and -t options must be supplied for all simulations .");
}



int main(int argc , char ** argv)
{
    unsigned long set_bits;
    unsigned long block_bits;
    unsigned long lines_per_set;
    unsigned long verbose = 0;
    const char* file;
    int opt;

    while ((opt = getopt(argc, argv, "hvs:b:E:t:")) != -1){
        switch(opt){
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
    
    if (argc < 10){
        print_help();
        return 0;
    }

    if (set_bits + block_bits >= 64){
        printf("Error: s + b is too large (s = 2, b = 300)\n");
        return 0;
    }

  
    return 0;
}

