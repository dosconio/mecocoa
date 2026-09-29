#include <stdio.h>

int main(int argc, char* argv[], char* envp[]) {
    printf("\n========================================\n");
    printf("[LD-MCCA] Hello from the Dummy Interpreter!\n");
    printf("[LD-MCCA] I was successfully loaded by the kernel.\n");
    printf("[LD-MCCA] Argc: %d\n", argc);
    if (argc > 0) {
        printf("[LD-MCCA] Argv[0]: %s\n", argv[0]);
    }
    printf("[LD-MCCA] My job ends here for now.\n");
    printf("========================================\n\n");
    
    // Skip real loading process for now, just exit
    return 0;
}
