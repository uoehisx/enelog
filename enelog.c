#define _GNU_SOURCE
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>

static unsigned long     interval = 1000000; // Default interval in usecs

static void
usage(void)
{
        printf(
"Usage: enelog [-i interval] [-h]\n"
"Options:\n"
"  -i interval   Set the interval for energy measurement (default: 1 second)\n"
"  -h            Show this help message\n"
        );
}

// get PERF_TYPE_POWER
static unsigned int
get_perf_type(void)
{
        FILE *fp;
        char buf_type[256];

        fp = fopen("/sys/bus/event_source/devices/power/type", "r");
        if (fp == NULL) {
                perror("failed to open /sys/bus/event_source/devices/power/type");
                exit(EXIT_FAILURE);
        }
        if (fgets(buf_type, sizeof(buf_type), fp) == NULL) {
                perror("failed to read power type");
                fclose(fp);
                exit(EXIT_FAILURE);
        }
        fclose(fp);

        unsigned int type;

        type = (unsigned int)strtoul(buf_type, NULL, 10);
        return type;
}

static int
open_perf_event_power(pid_t pid, int cpu, int group_fd, unsigned long flags)
{
        struct perf_event_attr  pea;

        memset(&pea, 0, sizeof(struct perf_event_attr));
        pea.type = get_perf_type();
        pea.size = sizeof(struct perf_event_attr);
        pea.config = 0x02;
        pea.disabled = 0;
        pea.exclude_kernel = 0;
        pea.exclude_hv = 0;
        pea.read_format = 0x1; // PERF_FORMAT_TOTAL_TIME_ENABLED

        int fd = (int)syscall(__NR_perf_event_open, &pea, pid, cpu, group_fd, flags);
        if (fd == -1) {
                perror("perf_event_open");
                exit(EXIT_FAILURE);
        }
        return fd;
}

static uint64_t
read_counter(int fd)
{
        uint64_t        count = 0;

        if (read(fd, &count, sizeof(count)) == -1) {
                perror("read");
                exit(EXIT_FAILURE);
        }
        return count;
}

static inline uint64_t
get_usec_elapsed(struct timespec *start, struct timespec *end)
{
        time_t  sec = end->tv_sec - start->tv_sec;
        long    nsec = end->tv_nsec - start->tv_nsec;

        if (nsec < 0) {
                sec -= 1;
                nsec += 1000000000L;
        }
        return (uint64_t)sec * 1000000 + (uint64_t)(nsec / 1000);
}

static void
setup_current_time_str(char *timebuf)
{
        time_t now = time(NULL);
        struct tm       tm_now;
        localtime_r(&now, &tm_now);

        strftime(timebuf, 32, "%m-%d %H:%M:%S", &tm_now);
}

static void
log_energy(int fd)
{
        char    timebuf[32];
        struct timespec ts_last;
        double  energy_last;

        clock_gettime(CLOCK_MONOTONIC, &ts_last);
        energy_last = read_counter(fd) / 1e6;
        while (1) {
                struct timespec ts_cur;

                clock_gettime(CLOCK_MONOTONIC, &ts_cur);
                uint64_t        elapsed = get_usec_elapsed(&ts_last, &ts_cur);
                if (elapsed >= interval) { // Convert seconds to microseconds
                        double  energy_cur = read_counter(fd) / 1e6;
                        double  energy = energy_cur - energy_last;
                        double  power = energy / (elapsed / 1000000);
                        setup_current_time_str(timebuf);
                        printf("%s %.3f %.3f\n", timebuf, power, energy);
                        energy_last = energy_cur;
                        ts_last = ts_cur;
                        elapsed -= interval;
                }
                usleep(interval - elapsed);
        }
}

static void
parse_args(int argc, char *argv[])
{
        int     c, opt;

        while ((c = getopt(argc, argv, "i:h")) != -1) {
                switch (c) {
                        case 'i':
                                interval = strtoul(optarg, NULL, 10) * 1000000;
                                break;
                        case 'h':
                                usage();
                                exit(0);
                        default:
                                usage();
                                exit(EXIT_FAILURE);
                }
        }
}

int
main(int argc, char *argv[])
{
        int     fd;

        parse_args(argc, argv);

        fd = open_perf_event_power(-1, 0, -1, 0);

        log_energy(fd);

        close(fd);
        return 0;
}
