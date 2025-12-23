#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define EPSILON 1e-9
#define MAX_MATRIX_SIZE 200

typedef struct {
  double **matrix;            // Расширенная матрица системы
  int size;                   // Размерность системы
  int thread_id;              // ID потока (0..num_threads-1)
  int num_threads;            // Общее количество потоков
  pthread_barrier_t *barrier; // Барьер для синхронизации
  volatile int *error_flag;   // Флаг ошибки
} thread_data_t;

typedef struct {
  double sequential_time; // Время последовательного выполнения
  double parallel_time;   // Время параллельного выполнения
  int num_threads;        // Количество потоков
  int matrix_size;        // Размер матрицы
  double speedup;         // Ускорение
  double efficiency;      // Эффективность
} performance_stats_t;

static performance_stats_t stats;

double **allocate_matrix(int rows, int cols) {
  double **mat = (double **)malloc(rows * sizeof(double *));
  if (mat == NULL) {
    return NULL;
  }

  for (int i = 0; i < rows; i++) {
    mat[i] = (double *)malloc(cols * sizeof(double));
    if (mat[i] == NULL) {
      // Освобождаем уже выделенную память
      for (int j = 0; j < i; j++) {
        free(mat[j]);
      }
      free(mat);
      return NULL;
    }
  }
  return mat;
}

void free_matrix(double **mat, int rows) {
  if (mat == NULL)
    return;
  for (int i = 0; i < rows; i++) {
    free(mat[i]);
  }
  free(mat);
}

void copy_matrix(double **dest, double **src, int rows, int cols) {
  for (int i = 0; i < rows; i++) {
    memcpy(dest[i], src[i], cols * sizeof(double));
  }
}

void print_matrix(double **mat, int rows, int cols, const char *title) {
  if (rows > 10 || cols > 10) {
    printf("%s: Matrix %dx%d (too large to print)\n", title, rows, cols);
    return;
  }

  printf("\n%s (%dx%d):\n", title, rows, cols);
  for (int i = 0; i < rows; i++) {
    printf("  [");
    for (int j = 0; j < cols; j++) {
      if (j == cols - 1) {
        printf(" | %8.4f", mat[i][j]);
      } else {
        printf("%8.4f ", mat[i][j]);
      }
    }
    printf("]\n");
  }
  printf("\n");
}

void generate_test_matrix(double **matrix, int size) {
  for (int i = 0; i < size; i++) {
    double sum = 0.0;
    for (int j = 0; j < size; j++) {
      if (i == j) {
        matrix[i][j] = 10.0 + i * 2.0; // Диагональные элементы
      } else {
        matrix[i][j] = 1.0; // Внедиагональные элементы
      }
      sum += matrix[i][j];
    }
    // Правая часть = сумма коэффициентов + небольшой сдвиг
    matrix[i][size] = sum + i * 0.5;
  }
}

double get_time_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// последовательная реализация
int gauss_sequential(double **matrix, int size) {
  // Прямой ход
  for (int k = 0; k < size; k++) {
    // Поиск максимального элемента в столбце (частичный выбор главного
    // элемента)
    int max_row = k;
    double max_val = fabs(matrix[k][k]);
    for (int i = k + 1; i < size; i++) {
      if (fabs(matrix[i][k]) > max_val) {
        max_val = fabs(matrix[i][k]);
        max_row = i;
      }
    }

    // Проверка на вырожденность
    if (fabs(matrix[max_row][k]) < EPSILON) {
      fprintf(stderr, "Error: Matrix is singular at row %d\n", k);
      return -1;
    }

    // Перестановка строк
    if (max_row != k) {
      double *temp = matrix[k];
      matrix[k] = matrix[max_row];
      matrix[max_row] = temp;
    }

    // Исключение
    for (int i = k + 1; i < size; i++) {
      double factor = matrix[i][k] / matrix[k][k];
      for (int j = k; j < size + 1; j++) {
        matrix[i][j] -= factor * matrix[k][j];
      }
    }
  }

  // Обратный ход
  for (int i = size - 1; i >= 0; i--) {
    for (int j = i + 1; j < size; j++) {
      matrix[i][size] -= matrix[i][j] * matrix[j][size];
    }
    if (fabs(matrix[i][i]) < EPSILON) {
      fprintf(stderr, "Error: Division by zero at row %d\n", i);
      return -1;
    }
    matrix[i][size] /= matrix[i][i];
  }

  return 0;
}

// параллельная реализация
void *thread_forward_elimination(void *arg) {
  thread_data_t *data = (thread_data_t *)arg;
  double **matrix = data->matrix;
  int size = data->size;
  int thread_id = data->thread_id;
  int num_threads = data->num_threads;
  pthread_barrier_t *barrier = data->barrier;
  volatile int *error_flag = data->error_flag;

  // Прямой ход
  for (int k = 0; k < size; k++) {
    // Проверка ошибки
    if (*error_flag)
      pthread_exit(NULL);

    // Поток 0 выполняет выбор главного элемента и перестановку
    if (thread_id == 0) {
      int max_row = k;
      double max_val = fabs(matrix[k][k]);
      for (int i = k + 1; i < size; i++) {
        if (fabs(matrix[i][k]) > max_val) {
          max_val = fabs(matrix[i][k]);
          max_row = i;
        }
      }

      if (fabs(matrix[max_row][k]) < EPSILON) {
        fprintf(stderr, "Error: Matrix is singular at row %d\n", k);
        *error_flag = 1;
        pthread_barrier_wait(barrier);
        pthread_exit(NULL);
      }

      // Перестановка строк
      if (max_row != k) {
        double *temp = matrix[k];
        matrix[k] = matrix[max_row];
        matrix[max_row] = temp;
      }
    }

    // Синхронизация: все потоки ждут завершения выбора главного элемента
    pthread_barrier_wait(barrier);

    if (*error_flag)
      pthread_exit(NULL);

    // Кэшируем ведущую строку и pivot
    double pivot = matrix[k][k];
    double *pivot_row = matrix[k];

    // Распределение работы между потоками
    int rows_to_process = size - k - 1;
    if (rows_to_process <= 0)
      continue;

    // Определяем количество активных потоков
    int active_threads =
        (rows_to_process < num_threads) ? rows_to_process : num_threads;
    if (thread_id >= active_threads) {
      pthread_barrier_wait(barrier);
      continue;
    }

    // Вычисляем диапазон строк для текущего потока
    int rows_per_thread = rows_to_process / active_threads;
    int extra_rows = rows_to_process % active_threads;

    int start_row = k + 1 + thread_id * rows_per_thread;
    if (thread_id < extra_rows) {
      start_row += thread_id;
    } else {
      start_row += extra_rows;
    }
    int end_row =
        start_row + rows_per_thread + (thread_id < extra_rows ? 1 : 0);

    // Исключение для назначенных строк
    for (int i = start_row; i < end_row; i++) {
      double factor = matrix[i][k] / pivot;
      for (int j = k; j < size + 1; j++) {
        matrix[i][j] -= factor * pivot_row[j];
      }
    }

    // Синхронизация: все потоки завершили обработку строк для данного k
    pthread_barrier_wait(barrier);
  }

  pthread_exit(NULL);
}

int gauss_parallel(double **matrix, int size, int num_threads) {
  if (num_threads <= 0 || size <= 0) {
    fprintf(stderr, "Error: Invalid parameters\n");
    return -1;
  }

  // // Для маленьких матриц используем последовательный алгоритм
  // if (size < num_threads) {
  //   return gauss_sequential(matrix, size);
  // }

  // Инициализация барьера и флага ошибки
  pthread_barrier_t barrier;
  volatile int error_flag = 0;

  if (pthread_barrier_init(&barrier, NULL, num_threads) != 0) {
    perror("pthread_barrier_init failed");
    return -1;
  }

  // Создание потоков
  pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
  thread_data_t *thread_data =
      (thread_data_t *)malloc(num_threads * sizeof(thread_data_t));

  if (threads == NULL || thread_data == NULL) {
    perror("Memory allocation failed");
    pthread_barrier_destroy(&barrier);
    return -1;
  }

  // Инициализация данных потоков
  for (int i = 0; i < num_threads; i++) {
    thread_data[i].matrix = matrix;
    thread_data[i].size = size;
    thread_data[i].thread_id = i;
    thread_data[i].num_threads = num_threads;
    thread_data[i].barrier = &barrier;
    thread_data[i].error_flag = &error_flag;

    if (pthread_create(&threads[i], NULL, thread_forward_elimination,
                       &thread_data[i]) != 0) {
      perror("pthread_create failed");
      // Отменяем уже созданные потоки
      error_flag = 1;
      for (int j = 0; j < i; j++) {
        pthread_cancel(threads[j]);
        pthread_join(threads[j], NULL);
      }
      free(threads);
      free(thread_data);
      pthread_barrier_destroy(&barrier);
      return -1;
    }
  }

  // Ожидание завершения всех потоков
  for (int i = 0; i < num_threads; i++) {
    pthread_join(threads[i], NULL);
  }

  // Освобождение ресурсов
  free(threads);
  free(thread_data);
  pthread_barrier_destroy(&barrier);

  if (error_flag) {
    return -1;
  }

  // Обратный ход выполняется последовательно основным потоком
  for (int i = size - 1; i >= 0; i--) {
    for (int j = i + 1; j < size; j++) {
      matrix[i][size] -= matrix[i][j] * matrix[j][size];
    }
    if (fabs(matrix[i][i]) < EPSILON) {
      fprintf(stderr, "Error: Division by zero at row %d\n", i);
      return -1;
    }
    matrix[i][size] /= matrix[i][i];
  }

  return 0;
}

// запуск теста, принимает размер матрицы и кол-во потоков
void measure_performance(int matrix_size, int num_threads) {
  // Выделение памяти
  double **matrix_seq = allocate_matrix(matrix_size, matrix_size + 1);
  double **matrix_par = allocate_matrix(matrix_size, matrix_size + 1);

  if (matrix_seq == NULL || matrix_par == NULL) {
    fprintf(stderr, "Memory allocation failed\n");
    return;
  }

  // Генерация тестовой матрицы
  generate_test_matrix(matrix_seq, matrix_size);
  copy_matrix(matrix_par, matrix_seq, matrix_size, matrix_size + 1);

  // Измерение последовательного выполнения
  double start_time = get_time_ms();
  if (gauss_sequential(matrix_seq, matrix_size) != 0) {
    fprintf(stderr, "Sequential solution failed\n");
    free_matrix(matrix_seq, matrix_size);
    free_matrix(matrix_par, matrix_size);
    return;
  }
  double seq_time = get_time_ms() - start_time;

  // Измерение параллельного выполнения
  start_time = get_time_ms();
  if (gauss_parallel(matrix_par, matrix_size, num_threads) != 0) {
    fprintf(stderr, "Parallel solution failed\n");
    free_matrix(matrix_seq, matrix_size);
    free_matrix(matrix_par, matrix_size);
    return;
  }
  double par_time = get_time_ms() - start_time;

  // Вычисление ускорения и эффективности
  double speedup = seq_time / par_time;
  double efficiency = speedup / num_threads;

  // Сохранение статистики
  stats.sequential_time = seq_time;
  stats.parallel_time = par_time;
  stats.num_threads = num_threads;
  stats.matrix_size = matrix_size;
  stats.speedup = speedup;
  stats.efficiency = efficiency;

  // Вывод результатов
  printf(
      "Size: %3d | Threads: %2d | Sequential: %8.2f ms | Parallel: %8.2f ms | "
      "Speedup: %6.3f | Efficiency: %6.3f\n",
      matrix_size, num_threads, seq_time, par_time, speedup, efficiency);

  // Проверка корректности (сравнение решений)
  int correct = 1;
  for (int i = 0; i < matrix_size; i++) {
    if (fabs(matrix_seq[i][matrix_size] - matrix_par[i][matrix_size]) > 1e-5) {
      correct = 0;
      break;
    }
  }

  if (!correct) {
    fprintf(stderr, "Warning: Solutions differ!\n");
  }

  // Освобождение памяти
  free_matrix(matrix_seq, matrix_size);
  free_matrix(matrix_par, matrix_size);
}

void print_usage(const char *program_name) {
  printf("Usage: %s <num_threads> [options]\n", program_name);
  printf("\nOptions:\n");
  printf("  <num_threads>     Number of threads (required)\n");
  printf("  -s <size>         Matrix size (default: 50)\n");
  printf("  -t                 Test mode: run performance tests\n");
  printf("  -d                 Demo mode: show thread information\n");
  printf("  -h                 Show this help message\n");
  printf("\nExamples:\n");
  printf("  %s 4                # Run with 4 threads, default size 50\n",
         program_name);
  printf("  %s 8 -s 100         # Run with 8 threads, matrix size 100\n",
         program_name);
  printf("  %s 4 -t             # Run performance tests with 4 threads\n",
         program_name);
  printf("  %s 4 -d             # Demo mode: show thread info\n", program_name);
}

void demo_threads(int num_threads) {
  printf("\n=== Thread Demonstration ===\n");
  printf("Process PID: %d\n", getpid());
  printf("Number of threads requested: %d\n", num_threads);
  printf("\nTo view threads in the system, run:\n");
  printf("  ps -T -p %d\n", getpid());
  printf("  top -H -p %d\n", getpid());
  printf("  htop (then press 'H' to show threads)\n");
  printf("\nPress Enter to start computation...\n");
  getchar();

  // Создаем тестовую задачу
  int size = 50;
  double **matrix = allocate_matrix(size, size + 1);
  if (matrix == NULL) {
    fprintf(stderr, "Memory allocation failed\n");
    return;
  }

  generate_test_matrix(matrix, size);
  printf("Starting computation with %d threads...\n", num_threads);
  printf("You can now check threads using the commands above.\n\n");

  double start = get_time_ms();
  if (gauss_parallel(matrix, size, num_threads) == 0) {
    double elapsed = get_time_ms() - start;
    printf("Computation completed in %.2f ms\n", elapsed);
  } else {
    fprintf(stderr, "Computation failed\n");
  }

  free_matrix(matrix, size);
}

int main(int argc, char *argv[]) {
  int num_threads = 0;
  int matrix_size = 50;
  int test_mode = 0;
  int demo_mode = 0;

  // Парсинг аргументов
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  num_threads = atoi(argv[1]);
  if (num_threads <= 0) {
    fprintf(stderr, "Error: Number of threads must be positive\n");
    print_usage(argv[0]);
    return 1;
  }

  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
      matrix_size = atoi(argv[++i]);
      if (matrix_size <= 0 || matrix_size > MAX_MATRIX_SIZE) {
        fprintf(stderr, "Error: Invalid matrix size (1-%d)\n", MAX_MATRIX_SIZE);
        return 1;
      }
    } else if (strcmp(argv[i], "-t") == 0) {
      test_mode = 1;
    } else if (strcmp(argv[i], "-d") == 0) {
      demo_mode = 1;
    } else if (strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  // Режим демонстрации потоков
  if (demo_mode) {
    demo_threads(num_threads);
    return 0;
  }

  // Режим тестирования производительности
  if (test_mode) {
    printf("\n=== Performance Analysis ===\n");
    printf("Matrix Size | Threads | Sequential (ms) | Parallel (ms) | Speedup "
           "| Efficiency\n");
    printf("-------------------------------------------------------------------"
           "--------\n");

    // Тестируем разные размеры матриц
    int sizes[] = {20, 30, 50, 70, 100, 150};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int i = 0; i < num_sizes; i++) {
      measure_performance(sizes[i], num_threads);
    }

    printf("\n=== Testing Different Thread Counts ===\n");
    printf("Matrix Size: %d\n", matrix_size);
    printf(
        "Threads | Sequential (ms) | Parallel (ms) | Speedup | Efficiency\n");
    printf("------------------------------------------------------------\n");

    // Тестируем разное количество потоков
    for (int t = 1; t <= num_threads; t++) {
      measure_performance(matrix_size, t);
    }

    return 0;
  }

  // Обычный режим: решение одной задачи
  printf("Solving system of linear equations using Gaussian elimination\n");
  printf("Matrix size: %dx%d\n", matrix_size, matrix_size);
  printf("Number of threads: %d\n\n", num_threads);

  double **matrix = allocate_matrix(matrix_size, matrix_size + 1);
  if (matrix == NULL) {
    fprintf(stderr, "Memory allocation failed\n");
    return 1;
  }

  generate_test_matrix(matrix, matrix_size);
  print_matrix(matrix, matrix_size, matrix_size + 1, "Initial Matrix");

  double start_time = get_time_ms();
  if (gauss_parallel(matrix, matrix_size, num_threads) != 0) {
    fprintf(stderr, "Solution failed\n");
    free_matrix(matrix, matrix_size);
    return 1;
  }
  double elapsed_time = get_time_ms() - start_time;

  printf("Solution completed in %.2f ms\n\n", elapsed_time);

  // Вывод решений (только первые и последние для больших матриц)
  printf("Solutions:\n");
  if (matrix_size <= 10) {
    for (int i = 0; i < matrix_size; i++) {
      printf("  x[%d] = %12.6f\n", i, matrix[i][matrix_size]);
    }
  } else {
    printf("  (showing first 5 and last 5 solutions)\n");
    for (int i = 0; i < 5; i++) {
      printf("  x[%d] = %12.6f\n", i, matrix[i][matrix_size]);
    }
    printf("  ...\n");
    for (int i = matrix_size - 5; i < matrix_size; i++) {
      printf("  x[%d] = %12.6f\n", i, matrix[i][matrix_size]);
    }
  }

  free_matrix(matrix, matrix_size);
  return 0;
}
