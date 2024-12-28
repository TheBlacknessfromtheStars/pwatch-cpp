#include "PerfMap.hpp"

#include <dirent.h>
#include <malloc.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdio>

namespace Pwatch {
int perf_event_open(perf_event_attr* attr, int pid, int cpu, int group_fd,
                    unsigned long flag) {
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

int PerfMap::create(std::vector<int> pids, uintptr_t bp_addr, int bp_len,
                    int bp_type, int n) {
    perf_event_attr attr{};

    attr.size = sizeof(attr);
    attr.type = PERF_TYPE_BREAKPOINT;
    attr.config = PERF_COUNT_SW_CPU_CLOCK;
    attr.sample_type = PERF_SAMPLE_TID | PERF_SAMPLE_REGS_USER;
    attr.sample_period = 1;
    attr.wakeup_events = 1;
    attr.precise_ip = 2;

    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    attr.bp_type = bp_type;
    attr.bp_addr = bp_addr;
    attr.bp_len = bp_len;

    attr.sample_regs_user = ((1ULL << PERF_REG_ARM64_MAX) - 1);
    attr.mmap = 1;
    attr.comm = 1;
    attr.mmap_data = 1;
    attr.mmap2 = 1;

    for (int pid : pids) {
        PerfInfo info{};

        info.pid = pid;
        info.fd =
            perf_event_open(&attr, info.pid, -1, -1, PERF_FLAG_FD_CLOEXEC);
        if (info.fd < 0) {
            perror("perf_event_open failed");
            return -1;
        }
        ioctl(info.fd, PERF_EVENT_IOC_RESET, 0);
        info.mmap_size = PAGE_SIZE * (1 + (1ULL << n));
        info.mmap_addr = mmap(nullptr, info.mmap_size, PROT_READ | PROT_WRITE,
                              MAP_SHARED, info.fd, 0);
        if (info.mmap_addr == MAP_FAILED) {
            perror("mmap failed");
            close(info.fd);
            return -1;
        }

        info.sample_metadata_page = (perf_event_mmap_page*)info.mmap_addr;
        info.data_page = (void*)((uint64_t)info.mmap_addr + PAGE_SIZE);

        perf_infos.push_back(info);
    }

    return 0;
}

int PerfMap::process(void (*handle)(SampleData*), bool* loop) {
    int epoll_fd = epoll_create(1);
    if (epoll_fd < 0) {
        perror("epoll create failed");
        return -1;
    }

    int maxevents = perf_infos.size();
    epoll_event* event = (epoll_event*)malloc(sizeof(epoll_event) * maxevents);
    for (int i = 0; i < maxevents; i++) {
        perf_infos[i].read_data_size = 0;
        event[i].events = EPOLLIN;
        event[i].data.fd = perf_infos[i].fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, perf_infos[i].fd, &event[i]);
    }
    while (loop == nullptr || *loop) {
        printf("epoll wait...\n");
        if (epoll_wait(epoll_fd, event, maxevents, -1) <= 0)
            continue;
        for (int i = 0; i < maxevents; i++) {
            if (event[i].events & EPOLLIN) {
                uint64_t offset = 0;
                PerfInfo& info = perf_infos[i];
                auto get_addr = [&]() {
                    return (void*)((uint64_t)info.data_page +
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
                info.sample_metadata_page->data_tail = 1;
            } else {
                printf("%d未就绪\n", i);
            }
        }
    }
    close(epoll_fd);
    free(event);

    return 0;
}

void PerfMap::enable() {
    for (PerfInfo& info : perf_infos) {
        ioctl(info.fd, PERF_EVENT_IOC_ENABLE, 0);
    }
}

void PerfMap::disable() {
    for (PerfInfo& info : perf_infos) {
        ioctl(info.fd, PERF_EVENT_IOC_DISABLE, 0);
    }
}

void PerfMap::destroy() {
    for (PerfInfo& info : perf_infos) {
        close(info.fd);
        munmap(info.mmap_addr, info.mmap_size);
    }
}
} // namespace Pwatch
