#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <unistd.h>

#include "shared_block_t.h"

static int is_prime(int number) {
  int divisors_count = 0;

  if (number <= 1) {
    return 0;
  }

  for (int i = 2; i * i <= number; i++) {
    if (number % i == 0) {
      divisors_count++;
    }
  }

  return divisors_count == 0;
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <shm_path> <output_filename>\n", argv[0]);
    return 1;
  }

  const char *shm_path = argv[1];
  const char *filename = argv[2];

  int shm_fd = shm_open(shm_path, O_RDWR, 0666);
  if (shm_fd == -1) {
    perror("child: shm_open");
    return 1;
  }

  shared_block_t *shared = mmap(NULL, sizeof(shared_block_t),
                                PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (shared == MAP_FAILED) {
    perror("child: mmap");
    close(shm_fd);
    return 1;
  }

  close(shm_fd);

  FILE *output_file = fopen(filename, "w");
  if (output_file == NULL) {
    perror("child: fopen");
    munmap(shared, sizeof(shared_block_t));
    return 1;
  }

  while (1) {
    if (sem_wait(&shared->sem_child) == -1) {
      if (errno == EINTR) {
        continue;
      }
      perror("child: sem_wait");
      fclose(output_file);
      munmap(shared, sizeof(shared_block_t));
      return 1;
    }

    int number = shared->number;

    if (number < 0 || is_prime(number)) {
      if (number < 0) {
        printf("Число отрицательное, выход...\n");
      } else {
        printf("Число простое, выход...\n");
      }
      shared->status = 1;
      sem_post(&shared->sem_parent);
      break;
    }

    if (fprintf(output_file, "%d\n", number) < 0) {
      perror("child: fprintf");
      shared->status = 1;
      sem_post(&shared->sem_parent);
      break;
    }

    shared->status = 0;
    sem_post(&shared->sem_parent);
  }

  fclose(output_file);
  munmap(shared, sizeof(shared_block_t));
  return 0;
}
