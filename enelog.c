#define _GNU_SOURCE
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>

#ifndef NO_NVML
#include <nvml.h>
#endif

static unsigned long     interval = 1000000; // Default interval in usecs
static unsigned timeout = 120; // Default timeout in seconds
static int      has_headers = 0;
#ifndef NO_NVML
static int      use_gpu = 0; // Default GPU power measurement disabled
static int      gpu_count;
static nvmlDevice_t     *gpu_handle; // Handle for up to 2 GPUs
static double   *gpu_powers, *gpu_powers_last, *gpu_energies;
#endif

static void
usage(void)
{
        printf(
                "Usage: enelog [-i interval] [-t timeout] [-h]\n"
                "Options:\n"
                "  -i <interval>  Sampling interval in seconds (default: 1 second)\n"
                "  -t <timeout>   Total measurement duration in seconds (default: 120 seconds)\n"
                "  -H             Show field headers in outputs\n"
                "  -h             Show this help message and exit\n"
#ifndef NO_NVML
                "  -g             Enable GPU power measurement\n"
#endif
        );
}

static int
open_powercap(void)
{
        int     fd;

        fd = open("/sys/class/powercap/intel-rapl:0/energy_uj", O_RDONLY);
        if (fd < 0) {
                perror("failed to open /sys/class/powercap/intel-rapl:0/energy_uj");
                exit(EXIT_FAILURE);
        }
        return fd;
}

static double
read_energy_cpu(int fd)
{
        unsigned long energy_uj;
        char buf[128];

        lseek(fd, 0, SEEK_SET);
        if (read(fd, buf, sizeof(buf)) == -1) {
                perror("failed to read");
                exit(EXIT_FAILURE);
        }
        energy_uj = strtoul(buf, NULL, 10);
        return (double)energy_uj / 1e6;
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
wait_until_aligned_interval(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        unsigned intv = interval / 1000000; // Convert microseconds to seconds
        unsigned sec = ts.tv_sec % 60;
        unsigned nsec = ts.tv_nsec;
        unsigned wait_sec = (sec % intv == 0) ? 0 : intv - (sec % intv);
        long wait_usec = (1000000000L - nsec) / 1000;

        // total wait time(microsecond)
        long total_wait = wait_sec * 1000000L + wait_usec;
        if (total_wait > 0)
                usleep(total_wait);
}

#ifndef NO_NVML

static void
init_nvml(void)
{
        if (nvmlInit_v2() != NVML_SUCCESS) {
                fprintf(stderr, "NVML init failed\n");
                exit(EXIT_FAILURE);
        }
        if (nvmlDeviceGetCount_v2(&gpu_count) != NVML_SUCCESS || gpu_count == 0) {
                fprintf(stderr, "No NVIDIA GPUs found\n");
                exit(EXIT_FAILURE);
        }
        gpu_handle = (nvmlDevice_t *)malloc(sizeof(nvmlDevice_t) * gpu_count);
        if (!gpu_handle) {
                perror("malloc for gpu_handle");
                exit(EXIT_FAILURE);
        }
        gpu_powers = (double *)malloc(sizeof(double) * gpu_count * 2);
        if (!gpu_powers) {
                perror("malloc for gpu_powers");
                exit(EXIT_FAILURE);
        }
        gpu_energies = (double *)malloc(sizeof(double) * gpu_count);
        if (!gpu_energies) {
                perror("malloc for gpu_energies");
                exit(EXIT_FAILURE);
        }
        for (unsigned int i = 0; i < gpu_count; i++) {
                if (nvmlDeviceGetHandleByIndex_v2(i, &gpu_handle[i]) != NVML_SUCCESS) {
                        fprintf(stderr, "Failed to get handle for GPU %d\n", i);
                        exit(EXIT_FAILURE);
                }
                gpu_energies[i] = 0.0;
        }
}

static void 
shutdown_nvml(void)
{
        if (gpu_handle) {
                free(gpu_handle);
                gpu_handle = NULL;
        }
        nvmlShutdown();
}

#define GET_GPU_POWERS_CUR() \
        (gpu_powers_last == NULL || gpu_powers_last != gpu_powers) ? (gpu_powers): (gpu_powers + gpu_count)

static void
read_energy_gpu(void)
{
        double  *gpu_powers_cur = GET_GPU_POWERS_CUR();

        for (unsigned int i = 0; i < gpu_count; i++) {
                unsigned int    milli_watt;
                if (nvmlDeviceGetPowerUsage(gpu_handle[i], &milli_watt) == NVML_SUCCESS)
                        gpu_powers_cur[i] = milli_watt / 1000.0;
                if (gpu_powers_last != NULL)
                        gpu_energies[i] = (gpu_powers_last[i] + gpu_powers_cur[i]) * interval / 1000000.0 / 2;
        }
}

static void
get_total_power_energy_gpu(double *p_total_power, double *p_total_energy)
{
        double  *gpu_powers_cur = GET_GPU_POWERS_CUR();

        *p_total_power = 0;
        *p_total_energy = 0;
        for (unsigned int i = 0; i < gpu_count; i++) {
                (*p_total_power) += gpu_powers_cur[i];
                (*p_total_energy) += gpu_energies[i];
        }
}

#endif

static void
print_headers(void)
{
        if (!has_headers)
                return;
        printf("# MM-dd HH:mm:ss CPU(W) CPU(J)");
#ifndef NO_NVML
        if (use_gpu) {
                printf(" GPU(W) GPU(J)");
                for (int i = 0; i < gpu_count; i++)
                        printf(" GPU%02d(W) GPU%02d(J)", i, i);
        }
#endif
        printf("\n");
}

static void
log_energy(int fd)
{
        char    timebuf[32];
        struct timespec ts_start, ts_next;
        double  energy_last;

        wait_until_aligned_interval();

        print_headers();

        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        ts_next = ts_start;
        energy_last = read_energy_cpu(fd);
        read_energy_gpu();
        gpu_powers_last = gpu_powers;

        while (1) {
                uint64_t        elapsed_from_start = get_usec_elapsed(&ts_start, &ts_next);
                if (elapsed_from_start >= timeout * 1000000)
                        break;

                ts_next.tv_sec += interval / 1000000;
                ts_next.tv_nsec += (interval % 1000000) * 1000;
                if (ts_next.tv_nsec >= 1000000000) {
                        ts_next.tv_sec += 1;
                        ts_next.tv_nsec -= 1000000000;
                }
                clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts_next, NULL);

                double  energy_cur = read_energy_cpu(fd);
                double  energy = energy_cur - energy_last;
                double  power = energy / (interval / 1000000);
                setup_current_time_str(timebuf);

                printf("%s %.3f %.3f", timebuf, power, energy);
#ifndef NO_NVML
                if (use_gpu) {
                        double  total_power, total_energy;

                        read_energy_gpu();
                        get_total_power_energy_gpu(&total_power, &total_energy);
                        printf(" %.3f %.3f", total_power, total_energy);

                        double  *gpu_powers_cur = GET_GPU_POWERS_CUR();
                        for (unsigned int i = 0; i < gpu_count; i++) {
                                printf(" %.3f %.3f", gpu_powers_cur[i], gpu_energies[i]);
                        }
                        gpu_powers_last = gpu_powers_cur;
                }
#endif
                printf("\n");
                fflush(stdout);
                energy_last = energy_cur;
        }
}

static void
parse_args(int argc, char *argv[])
{
        int     c;

        while ((c = getopt(argc, argv, "i:t:Hhg")) != -1) {
                switch (c) {
                        case 'i':
                                interval = strtoul(optarg, NULL, 10) * 1000000;
                                break;
                        case 't':
                                timeout = strtoul(optarg, NULL, 10);
                                if (timeout <= 0) {
                                        fprintf(stderr, "Invalid timeout value: %s\n", optarg);
                                        exit(EXIT_FAILURE);
                                }
                                break;
                        case 'H':
                                has_headers = 1;
                                break;
                        case 'h':
                                usage();
                                exit(0);
                        case 'g':
#ifndef NO_NVML
                                use_gpu = 1;
                                break;
#endif
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

#ifndef NO_NVML
        if (use_gpu) {
                init_nvml();    
        }
#endif

        fd = open_powercap();
        log_energy(fd);
        close(fd);

#ifndef NO_NVML
        if (use_gpu) {
                shutdown_nvml();
        }
#endif

        return 0;
}
