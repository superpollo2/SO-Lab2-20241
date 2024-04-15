#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <comando>\n", argv[0]);
        return 1;
    }

    struct timeval start, end;

    // Almacenar el tiempo inicial
    gettimeofday(&start, NULL);

    pid_t pid = fork();

    if (pid == -1) {
        perror("Error en fork");
        return 1;
    } else if (pid == 0) {
        // Proceso hijo
        execvp(argv[1], &argv[1]);
        perror("Error en execvp");
        exit(1);
    } else {
        // Proceso padre
        int status;
        waitpid(pid, &status, 0);

        // Almacenar el tiempo final
        gettimeofday(&end, NULL);

        // Calcular el tiempo transcurrido
        double elapsed = (end.tv_sec - start.tv_sec) +
                         (end.tv_usec - start.tv_usec) / 1000000.0;

        printf("Elapsed time: %.5f\n", elapsed);
    }

    return 0;
}
