#include <cstdio>

#include "PerfMap.hpp"

using namespace std;

void handle(Pwatch::SampleData* data) {
    for (int i = 0; i < PERF_REG_ARM64_MAX; i++)
        printf("x%d: %lx ", i, data->regs[i]);
    printf("\n");
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Invalid Argument\n");
        return -1;
    }

    Pwatch::PerfMap map;

    map.create(atoi(argv[1]), strtoul(argv[2], NULL, 16), Pwatch::LEN_4,
               Pwatch::W, 1);
    map.setHandle(handle);
    map.process(nullptr);
    map.disable();
    map.destroy();

    return 0;
}
