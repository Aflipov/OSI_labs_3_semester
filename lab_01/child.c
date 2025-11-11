#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int is_prime(int number) {
  int divisors_count = 0;

  if (number <= 1) {
    return 0;
  }

  else {
    for (int i = 2; i * i <= number; i++) {
      if (number % i == 0)
        divisors_count++;
    }
    if (divisors_count > 0) {
      return 0;
    } else {
      return 1;
    }
  }
}

int main(int argc, char *argv[]) {
  int pipe1_read_fd = atoi(argv[1]);
  int pipe1_write_fd = atoi(argv[2]);
  int pipe2_read_fd = atoi(argv[3]);
  int pipe2_write_fd = atoi(argv[4]);
  const char *filename = argv[5];

  close(pipe1_write_fd);
  close(pipe2_read_fd);

  FILE *output_file = fopen(filename, "w");
  if (output_file == NULL) {
    perror("child: fopen");
    return 1;
  }

  int number = 0;

  char flag_yes = 1;
  char flag_no = 0;

  // child logic cycle
  while (1) {
    if (read(pipe1_read_fd, &number, sizeof(number)) == -1) {
      perror("child: pipe1 read");
      return 1;
    }

    if (number < 0) {
      printf("Число отрицательное, выход...\n");
      if (write(pipe2_write_fd, &flag_yes, sizeof(char)) == -1) {
        perror("child: pipe2 write");
      }
      break;
    } else if (is_prime(number)) {
      printf("Число простое, выход...\n");
      if (write(pipe2_write_fd, &flag_yes, sizeof(char)) == -1) {
        perror("child: pipe2 write");
      }
      break;
    } else {
      if (write(pipe2_write_fd, &flag_no, sizeof(char)) == -1) {
        perror("child: pipe2 write");
      }
      if (fprintf(output_file, "%d\n", number) < 0) {
        perror("child: fprintf");
        break;
      }
    }
  }

  fclose(output_file);
  close(pipe1_read_fd);
  close(pipe2_write_fd);

  printf("Дочерний процесс завершен.\n");
  return 0;
}