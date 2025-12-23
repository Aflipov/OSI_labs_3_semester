#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shared_block_t.h"

static int create_shared_block(shared_block_t **block, int *fd,
                               char shm_path[]) {
  pid_t pid = getpid();
  snprintf(shm_path, 64, "/lab03_shm_%d", pid);

  *fd = shm_open(shm_path, O_CREAT | O_RDWR, 0666);
  if (*fd == -1) {
    perror("shm_open");
    return -1;
  }

  if (ftruncate(*fd, sizeof(shared_block_t)) == -1) {
    perror("ftruncate");
    close(*fd);
    shm_unlink(shm_path);
    return -1;
  }

  *block = mmap(NULL, sizeof(shared_block_t), PROT_READ | PROT_WRITE,
                MAP_SHARED, *fd, 0);
  if (*block == MAP_FAILED) {
    perror("mmap");
    close(*fd);
    shm_unlink(shm_path);
    return -1;
  }

  return 0;
}

int main(void) {
  shared_block_t *shared = NULL;
  int shm_fd = -1;
  char shm_path[64] = {0};

  if (create_shared_block(&shared, &shm_fd, shm_path) == -1) {
    return 1;
  }

  if (sem_init(&shared->sem_child, 1, 0) == -1) {
    perror("sem_init child");
    munmap(shared, sizeof(shared_block_t));
    close(shm_fd);
    return 1;
  }

  if (sem_init(&shared->sem_parent, 1, 0) == -1) {
    perror("sem_init parent");
    sem_destroy(&shared->sem_child);
    munmap(shared, sizeof(shared_block_t));
    close(shm_fd);
    return 1;
  }

  char filename[256];
  printf("Введите имя файла: ");
  if (fgets(filename, sizeof(filename), stdin) == NULL) {
    perror("Ошибка чтения имени файла");
    sem_destroy(&shared->sem_child);
    sem_destroy(&shared->sem_parent);
    munmap(shared, sizeof(shared_block_t));
    close(shm_fd);
    return 1;
  }
  filename[strcspn(filename, "\n")] = 0;

  pid_t child_pid = fork();
  if (child_pid == -1) {
    perror("fork");
    sem_destroy(&shared->sem_child);
    sem_destroy(&shared->sem_parent);
    munmap(shared, sizeof(shared_block_t));
    close(shm_fd);
    shm_unlink(shm_path);
    return 1;
  }

  if (child_pid == 0) {
    munmap(shared, sizeof(shared_block_t));
    close(shm_fd);

    execlp("./child", "child", shm_path, filename, (char *)NULL);
    perror("execlp");
    return 1;
  }

  printf("Введите числа для проверки (число, затем Enter).\n");
  printf("Ввод отрицательного или простого числа завершит программу.\n");

  while (1) {
    int number = 0;
    printf("Введите число: ");
    int ret = scanf("%d", &number);
    if (ret != 1) {
      if (feof(stdin)) {
        fprintf(stderr, "EOF, завершаем работу.\n");
      } else {
        perror("scanf");
      }
      shared->number = -1;
      sem_post(&shared->sem_child);
      sem_wait(&shared->sem_parent);
      break;
    }

    shared->number = number;
    sem_post(&shared->sem_child);

    if (sem_wait(&shared->sem_parent) == -1) {
      if (errno == EINTR) {
        continue;
      }
      perror("parent: sem_wait");
      break;
    }

    if (shared->status) {
      break;
    }
  }

  int status = 0;
  waitpid(child_pid, &status, 0);

  sem_destroy(&shared->sem_child);
  sem_destroy(&shared->sem_parent);
  munmap(shared, sizeof(shared_block_t));
  close(shm_fd);
  shm_unlink(shm_path);

  return 0;
}
