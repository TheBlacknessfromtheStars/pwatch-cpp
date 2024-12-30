#include "PerfMap.hpp"

#include <bits/epoll_event.h>
#include <bits/page_size.h>
#include <dirent.h>
#include <linux/eventpoll.h>
#include <linux/inotify.h>
#include <linux/perf_event.h>
#include <malloc.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Pwatch {
inline static int perf_event_open(perf_event_attr* attr, int pid, int cpu,
                                  int group_fd, unsigned long flag) {
    return (int)syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flag);
}

std::vector<int> getProcessTasks(int pid) {
    char path[128]{};
    sprintf(path, "/proc/%d/task", pid);
    DIR* p = opendir(path);
    dirent* dbuf = nullptr;
    std::vector<int> result;

    while ((dbuf = readdir(p)) != nullptr) {
        if (dbuf->d_name[0] == '.')
            continue;
        else if (dbuf->d_type != DT_DIR)
            continue;
        else if (strspn(dbuf->d_name, "0123456789") != strlen(dbuf->d_name))
            continue;
        result.push_back(atoi(dbuf->d_name));
    }

    closedir(p);
    return result;
}

PerfMap::PerfMap() {
    attr.size = sizeof(perf_event_attr);
    attr.type = PERF_TYPE_BREAKPOINT;
    attr.config = PERF_COUNT_SW_CPU_CLOCK;
    attr.sample_type = PERF_SAMPLE_TID | PERF_SAMPLE_REGS_USER;
    attr.sample_period = 1;
    attr.wakeup_events = 1;
    attr.precise_ip = 2;

    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    attr.sample_regs_user = ((1ULL << PERF_REG_ARM64_MAX) - 1);
    attr.mmap = 1;
    attr.comm = 1;
    attr.mmap_data = 1;
    attr.mmap2 = 1;
}

int PerfMap::addThread(int tid, int data_page_size_exponent) {
    PerfInfo info{};

    info.fd = perf_event_open(&attr, tid, -1, -1, PERF_FLAG_FD_CLOEXEC);
    if (info.fd < 0) {
        printf("0x%llx\n", attr.bp_addr);
        fprintf(stderr, "Failed to call perf_event_open on %d: %s\n", pid,
                strerror(errno));
        return -1;
    }
    ioctl(info.fd, PERF_EVENT_IOC_RESET, 0);
    info.mmap_size = PAGE_SIZE * (1 + (1ULL << data_page_size_exponent));
    info.mmap_addr = mmap(nullptr, info.mmap_size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, info.fd, 0);
    if (info.mmap_addr == MAP_FAILED) {
        perror("mmap failed");
        close(info.fd);
        return -1;
    }

    info.data_page = (void*)((uintptr_t)info.mmap_addr + PAGE_SIZE);
    info.sample_metadata_page = (perf_event_mmap_page*)info.mmap_addr;

    perf_infos.insert({tid, info});

    return 0;
}

int PerfMap::create(int pid, uintptr_t bp_addr, int bp_len, int bp_type,
                    int n) {
    std::vector<int> tasks = getProcessTasks(pid);

    this->pid = pid;
    setBreakpoint(bp_type, bp_addr, bp_len);
    for (int tid : tasks) {
        addThread(tid, n);
    }
    if (perf_infos.empty()) return -1;

    return 0;
}

void PerfMap::setBreakpoint(int bp_type, uintptr_t bp_addr, int bp_len) {
    attr.bp_type = bp_type;
    attr.bp_addr = bp_addr;
    attr.bp_len = bp_len;
}

void PerfMap::setHandle(void (*callback)(SampleData*)) { handle = callback; }

int PerfMap::process(bool* loop) {
    if (perf_infos.empty()) {
        fprintf(stderr, "Error: perf_infos is empty, no events to process.\n");
        return -1;
    }
    int epoll_fd = epoll_create(1);
    if (epoll_fd < 0) {
        perror("epoll create failed");
        return -1;
    }

    int maxevents = perf_infos.size();
    epoll_event* event = new epoll_event[maxevents];
    std::map<int, PerfInfo>::iterator it = perf_infos.begin();
    for (int i = 0; i < maxevents; i++) {
        it->second.read_data_size = 0;
        event[i].events = EPOLLIN;
        event[i].data.fd = it->second.fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, it->second.fd, &event[i]);
        ++it;
    }

    enable();
    while (loop == nullptr || *loop) {
        if (epoll_wait(epoll_fd, event, maxevents, -1) <= 0) continue;

        it = perf_infos.begin();
        for (int i = 0; i < maxevents; i++) {
            if (event[i].events & EPOLLIN) {
                uint64_t offset = 0;
                PerfInfo& info = it->second;
                auto get_addr = [&]() {
                    return (void*)((uintptr_t)info.data_page +
                                   ((info.read_data_size + offset) %
                                    info.sample_metadata_page->data_size));
                };
                SampleData data{};
                perf_event_header* header = (perf_event_header*)get_addr();

                if (header->type == PERF_RECORD_SAMPLE) {
                    offset += sizeof(perf_event_header);
                    data.pid = *((uint32_t*)get_addr());
                    offset += 4;
                    data.tid = *((uint32_t*)get_addr());
                    offset += 4;
                    data.abi = *((uint64_t*)get_addr());
                    offset += 8;
                    memcpy(&data.regs, get_addr(), sizeof(data.regs));
                    offset += sizeof(data.regs);
                    info.read_data_size += offset;
                    handle(&data);
                }
                info.sample_metadata_page->data_tail = info.read_data_size;
            }
            ++it;
        }
    }
    close(epoll_fd);
    delete[] event;

    return 0;
}

void PerfMap::enable() {
    for (auto& info : perf_infos) {
        ioctl(info.second.fd, PERF_EVENT_IOC_ENABLE, 0);
    }
}

void PerfMap::disable() {
    for (auto& info : perf_infos) {
        ioctl(info.second.fd, PERF_EVENT_IOC_DISABLE, 0);
    }
}

void PerfMap::destroy() {
    for (auto& info : perf_infos) {
        close(info.second.fd);
        munmap(info.second.mmap_addr, info.second.mmap_size);
    }
    perf_infos.clear();
}
}  // namespace Pwatch
