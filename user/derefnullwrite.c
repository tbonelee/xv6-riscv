#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int *p = 0;
    *p = 42; // 0xf의 트랩 발생 (https://five-embeddev.com/riscv-priv-isa-manual/Priv-v1.12/supervisor.html#sec:scause)
    return 0;
}