#include <asm/perf_regs.h>
#include <cstdint>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <vector>

namespace Pwatch {
struct PerfInfo {
    int pid;
    int fd;
    void* mmap_addr;
    size_t mmap_size;
    struct perf_event_mmap_page* sample_metadata_page;
    void* data_page;
    uint64_t read_data_size;
};

struct SampleData {
    uint32_t pid;
    uint32_t tid;
    uint64_t abi;
    uint64_t regs[PERF_REG_ARM64_MAX];
};

class PerfMap {
  private:
    int pid;
    std::vector<PerfInfo> perf_infos;

  public:
    int create(const std::vector<int> pids, uintptr_t bp_addr, int bp_len,
               int bp_type, int n);

    void enable();
    void disable();
    void destroy();
    int process(void (*handle)(SampleData*), bool* loop);
};

std::vector<int> getProcessTasks(int pid);
} // namespace Pwatch
