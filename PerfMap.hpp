#include <asm/perf_regs.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace Pwatch {

enum {
    R = HW_BREAKPOINT_R,
    W = HW_BREAKPOINT_W,
    RW = HW_BREAKPOINT_RW,
    X = HW_BREAKPOINT_X,
    LEN_1 = HW_BREAKPOINT_LEN_1,
    LEN_2 = HW_BREAKPOINT_LEN_2,
    LEN_4 = HW_BREAKPOINT_LEN_4,
    LEN_8 = HW_BREAKPOINT_LEN_8,
};

struct PerfInfo {
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
    perf_event_attr attr{};
    std::map<int, PerfInfo> perf_infos;
    std::function<void(SampleData*)> handle;

    int addThread(int tid, int data_page_size_exponent);

   public:
    PerfMap();
    int create(int pid, uintptr_t bp_addr, int bp_len, int bp_type, int n);
    void setBreakpoint(int bp_type, uintptr_t bp_addr, int bp_len);
    void setHandle(void (*callback)(SampleData*));
    void enable();
    void disable();
    void destroy();
    int process(bool* loop);
};

std::vector<int> getProcessTasks(int pid);
}  // namespace Pwatch
