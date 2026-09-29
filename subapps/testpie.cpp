// static, pie

#include <stdio.h>

int main(int argc, char* argv[]) {
    printf("[STATIC-PIE] Hello from Mecocoa Static PIE!\n");
    for (int i = 1; i < argc; i++) {
        printf("%s ", argv[i]);
    }
    printf("\n");
    return 0;
}
