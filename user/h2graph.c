#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/pstat.h"
#include "user/user.h"

#define NUM_PROCESSES 3

static void print_ratio2(int r) { // r = 정수부*100 + 소수부(2자리)
    int w = r / 100;
    int f = r % 100;
    printf("%d.", w);
    if (f < 10) printf("0");  // 수동 0 패딩
    printf("%d", f);
  }

void cpu_work() {
    int i = 0;
    while (1) {
        for (int j = 0; j < 100000; j++) {
            i = (i + j * 17 + 1) % 1000000;
        }
    }
}

int
main(int argc, char * argv[]) {
    int pids[NUM_PROCESSES];
    int tickets[NUM_PROCESSES] = {30, 20, 10};
    struct pstat ps;
    int MEASUREMENTS = 100;  // Number of measurements to take
    int SLEEP_INTERVAL = 10;  // Sleep interval in ticks

    printf("Starting scheduler test with %d processes (30:20:10 tickets)\n", NUM_PROCESSES);
    printf("Time\tPID1_ticks\tPID2_ticks\tPID3_ticks\tTickets1\tTickets2\tTickets3\tRatio1\tRatio2\tRatio3\n");

    // Fork child processes
    for (int i = 0; i < NUM_PROCESSES; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            // Child process: Set tickets and run
            printf("Child %d: Setting tickets to %d\n", i + 1, tickets[i]);
            settickets(tickets[i]);
            printf("Child %d: Sleeping for %d ticks\n", i + 1, SLEEP_INTERVAL);
            sleep(SLEEP_INTERVAL);
            printf("Child %d: Done sleeping\n", i + 1);
            cpu_work();
            exit(0);
        } else if (pids[i] < 0) {
            printf("Failed to fork child %d\n", i + 1);
            exit(1);
        }
        printf("pid%d = %d\n", i + 1, pids[i]);
    }

    settickets(10);

    // Monitor and collect data
    sleep(SLEEP_INTERVAL); // Let processes run a bit first


    // printf("getpinfo\n");

    for (int measurement = 0; measurement < MEASUREMENTS; measurement++) {
        // Get process information
        getpinfo(&ps);

        // Find our child processes and their ticks/tickets
        int ticks[NUM_PROCESSES] = {0};
        int found_tickets[NUM_PROCESSES] = {0};
        int found = 0;

        for (int i = 0; i < NPROC && found < NUM_PROCESSES; i++) {
            if (ps.inuse[i]) {
                for (int j = 0; j < NUM_PROCESSES; j++) {
                    if (ps.pid[i] == pids[j]) {
                        ticks[j] = ps.ticks[i];
                        found_tickets[j] = ps.tickets[i];
                        found++;
                        break;
                    }
                }
            }
        }

        // Verify tickets are set correctly
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (found_tickets[i] != tickets[i]) {
                printf("Process %d: expected %d tickets, got %d\n", i + 1, tickets[i], found_tickets[i]);
                exit(1);
            }
        }

        // Calculate ratios (relative to last process)
        int total_ticks = 0;
        for (int i = 0; i < NUM_PROCESSES; i++) {
            total_ticks += ticks[i];
        }
        // printf("total_ticks: %d\n", total_ticks);
        if (total_ticks > 0) {
            int ratios[NUM_PROCESSES];
            // Calculate ratios (relative to last process)
            int baseline_idx = NUM_PROCESSES - 1;
            for (int i = 0; i < NUM_PROCESSES - 1; i++) {
                ratios[i] = (ticks[i] * 100) / (ticks[baseline_idx] > 0 ? ticks[baseline_idx] : 1);
            }
            ratios[baseline_idx] = 100;  // Last process is the baseline

            /* 이슈1) 모든 child가 초기 sleep에서 리턴하지 않는 동안은 다음의 양상을 보이면서 ticks 비율이 1:1:1 돌아가는 현상
             * 1. wakeup()에서 모든 잠들어있던 프로세스를 잠시 깨움
             * 2. 깨어난 프로세스들은 모두 RUNNABLE 상태가 되고, 이 상태에서 다시 sched()가 호출되어 p->ticks가 증가함
             * 3. 스케쥴러가 하나씩 RUNNABLE한 프로세스를 실행하면, 그때마다 하나씩 다시 잠들어서 모든 프로세스가 한 번씩 ticks가 증가
             * 4. 모두 잠들고 나서야 다시 타이머 인터럽트 발생.
             */
            /* 이슈2) CPUS=3 이상부터는 프로세서 자원을 사용하기 위해 경쟁할 필요가 없으므로 ticks 비율이 1:1:1이 되는 현상?
            */
            printf("%d\t", measurement * SLEEP_INTERVAL);
            for (int i = 0; i < NUM_PROCESSES; i++) {
                printf("%d\t\t", ticks[i]);
            }
            for (int i = 0; i < NUM_PROCESSES; i++) {
                printf("%d\t\t", found_tickets[i]);
            }
            for (int i = 0; i < NUM_PROCESSES; i++) {
                print_ratio2(ratios[i]);
                if (i < NUM_PROCESSES - 1) printf("\t");
            }
            printf("\n");
          }

        // printf("sleeping\n");
        sleep(SLEEP_INTERVAL);
    }

    printf("\nTest completed. Expected ratios should be approximately 3:2:1\n");
    printf("Killing child processes...\n");

    // Kill child processes
    for (int i = 0; i < NUM_PROCESSES; i++) {
        kill(pids[i]);
    }

    // Wait for children to exit
    for (int i = 0; i < NUM_PROCESSES; i++) {
        wait(0);
    }

    printf("Done.\n");
    return 0;
}
