#include "PerfMap.hpp"
#include <cstdio>
#include <vector>

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

    vector<int> pids = Pwatch::getProcessTasks(atoi(argv[1]));

    for (int pid : pids)
        printf("%d ", pid);
    printf("\n");

    Pwatch::PerfMap map;
    map.create(pids, strtoul(argv[2], NULL, 16), HW_BREAKPOINT_LEN_4,
               HW_BREAKPOINT_W, 1);
    map.enable();
    map.process(handle, nullptr);
    map.disable();
    map.destroy();

    return 0;
}
