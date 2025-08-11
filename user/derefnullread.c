#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int *p = 0;
    printf("%d\n", *p); // 0xd의 트랩 발생 (https://five-embeddev.com/riscv-priv-isa-manual/Priv-v1.12/supervisor.html#sec:scause)
    return 0;
}