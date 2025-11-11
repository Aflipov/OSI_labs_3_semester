// #include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
  int pipe1[2];
  int pipe2[2];

  // pipe[0] - read
  // pipe[1] - write

  char filename[256];
  pid_t child_pid;

  if (pipe(pipe1) == -1) {
    perror("pipe1");
    return 1;
  }
  if (pipe(pipe2) == -1) {
    perror("pipe2");
    close(pipe1[0]);
    close(pipe1[1]);
    return 1;
  }

  printf("Введите имя файла: ");
  if (fgets(filename, sizeof(filename), stdin) == NULL) {
    perror("Ошибка чтения имени файла");
    return 1;
  }
  filename[strcspn(filename, "\n")] = 0; // 0 = \0 in ASCII

  child_pid = fork(); // вилка хихи

  if (child_pid == -1) {
    perror("fork");
    close(pipe1[0]);
    close(pipe1[1]);
    close(pipe2[0]);
    close(pipe2[1]);
    return 1;
  }

  // for child, preparing to execlp()
  if (child_pid == 0) {
    close(pipe1[1]);
    close(pipe2[0]);

    char pipe1_read_fd_str[10];
    char pipe1_write_fd_str[10];
    char pipe2_read_fd_str[10];
    char pipe2_write_fd_str[10];

    sprintf(pipe1_read_fd_str, "%d", pipe1[0]);
    sprintf(pipe1_write_fd_str, "%d", pipe1[1]);
    sprintf(pipe2_read_fd_str, "%d", pipe2[0]);
    sprintf(pipe2_write_fd_str, "%d", pipe2[1]);

    execlp("./child", "child", pipe1_read_fd_str, pipe1_write_fd_str,
           pipe2_read_fd_str, pipe2_write_fd_str, filename, NULL); // 6 args

    // if execlp returned controll
    perror("execlp failed");
    close(pipe1[0]);
    close(pipe2[1]);
    exit(1);
  }

  // parent logic
  else {
    close(pipe1[0]);
    close(pipe2[1]);

    printf("Введите числа для проверки (число, затем Enter).\n");
    printf("Ввод отрицательного или простого числа завершит программу.\n");

    int number;
    char number_is_invalid;

    while (1) {

      printf("Введите число: ");
      scanf("%d", &number);

      if (write(pipe1[1], &number, sizeof(number)) == -1) {
        perror("write to pipe1");
        break;
      }

      // wait for child feedback
      if (read(pipe2[0], &number_is_invalid, sizeof(char)) == -1) {
        perror("read from pipe2");
        break;
      }
      if (number_is_invalid) {
        break;
      }
    }
  }

  close(pipe1[1]);
  close(pipe2[0]);

  // wait for child
  int child_status;
  pid_t terminated_pid = waitpid(child_pid, &child_status, 0);
  if (terminated_pid == -1) {
    perror("waitpid");
    return 0;
  }

  printf("Родительский процесс завершен.\n");
  return 0;
}
