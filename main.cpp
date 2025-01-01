#include <cstdint>
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

    Pwatch::PerfMap map(atoi(argv[1]));

    map.create(strtoul(argv[2], NULL, 16), Pwatch::LEN_4, Pwatch::W);
    map.setHandle([](Pwatch::SampleData* data) {
        int i = 0;
        for (uint64_t reg : data->regs) {
            if (i < 31)
                printf("x%d: 0x%lx ", i, reg);
            else if (i > 31)
                printf("pc: 0x%lx ", reg);
            else {
                printf("sp: 0x%lx ", reg);
            }
            ++i;
        }
        printf("\n");
    });
    map.process(nullptr);
    map.disable();
    map.destroy();

    return 0;
}
