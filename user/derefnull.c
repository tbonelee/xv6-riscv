#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int *p = 0;
    printf("%d\n", *p); // 메모리 참조 오류 발생
    return 0;
}