#include <complex.h>
#include <math.h> // Для fabs()
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // Для memcpy()
#include <time.h>   // Для clock()

// --- Структура для хранения данных, передаваемых потоку ---
typedef struct {
  double **matrix; // Указатель на расширенную матрицу
  int size;        // Размерность матрицы (N)
  int start_row;   // Начальная строка, которую обрабатывает поток
  int end_row;     // Конечная строка (не включая)
  pthread_barrier_t *iter_barrier;  // Барьер для синхронизации внутри итераций
                                    // (прямой/обратный ход)
  pthread_barrier_t *phase_barrier; // Барьер для синхронизации между фазами
                                    // (прямой -> обратный ход)
  volatile int *
      error_flag; // Флаг для сигнализации об ошибке (например, деление на ноль)
} thread_data_t;

// --- Вспомогательная функция: Выделение памяти для матрицы ---
double **allocate_matrix(int rows, int cols) {
  double **mat = (double **)malloc(rows * sizeof(double *));
  if (mat == NULL) {
    perror("Failed to allocate memory for matrix rows");
    return NULL;
  }
  for (int i = 0; i < rows; ++i) {
    mat[i] = (double *)malloc(cols * sizeof(double));
    if (mat[i] == NULL) {
      perror("Failed to allocate memory for matrix columns");
      // Освободить уже выделенную память
      for (int j = 0; j < i; ++j) {
        free(mat[j]);
      }
      free(mat);
      return NULL;
    }
  }
  return mat;
}

// --- Вспомогательная функция: Освобождение памяти для матрицы ---
void free_matrix(double **mat, int rows) {
  if (mat == NULL)
    return;
  for (int i = 0; i < rows; ++i) {
    free(mat[i]);
  }
  free(mat);
}

// --- Вспомогательная функция: Копирование матрицы ---
void copy_matrix(double **dest, double **src, int rows, int cols) {
  for (int i = 0; i < rows; ++i) {
    memcpy(dest[i], src[i], cols * sizeof(double));
  }
}

// --- Вспомогательная функция: Печать матрицы ---
void print_matrix(const char *label, double **mat, int rows, int cols) {
  char *str = ", ";
  printf("%s", label);
  printf("Matrix (%dx%d):\n", rows, cols);
  for (int i = 0; i < rows; ++i) {
    printf(" [");
    for (int j = 0; j < cols; ++j) {
      if (j == cols - 2) {
        str = "  | ";
      } else if (j == cols - 1) {
        str = " ";
      } else {
        str = ", ";
      }
      printf("%8.4f%s", mat[i][j], str);
    }
    printf("]\n");
  }
  printf("\n");
}

void swap_rows(double **matrix, int row1, int row2, int size) {}

// --- Функция, выполняемая каждым потоком ---
void *thread_calculate_string(void *arg) {
  thread_data_t *data = (thread_data_t *)arg;
  double **matrix = data->matrix;
  int size = data->size;
  int start_row = data->start_row;
  int end_row = data->end_row;
  pthread_barrier_t *iter_barrier = data->iter_barrier;
  pthread_barrier_t *phase_barrier = data->phase_barrier;
  volatile int *error_flag = data->error_flag;

  double pivot_val, factor;

  // --- Прямой ход (приведение к верхнетреугольному виду) ---
  // Итерируемся по ведущим элементам (столбцам k-1, т.к. k идет от 1 до size)
  for (int k = 1; k < size; ++k) {
    // Проверка на ошибку, если другой поток ее обнаружил
    if (*error_flag)
      pthread_exit(NULL);

    // Определение ведущего элемента для данного столбца k-1
    pivot_val = matrix[k - 1][k - 1];

    // --- Проверка на деление на ноль ---
    if (fabs(pivot_val) <
        1e-9) { // Используем небольшую эпсилон для сравнения с нулем
      fprintf(stderr,
              "Error: Division by near zero detected at (%d, %d) during "
              "forward elimination.\n",
              k - 1, k - 1);
      *error_flag = 1; // Устанавливаем флаг ошибки
      // Ждем остальные потоки, чтобы они тоже увидели ошибку и завершились
      pthread_barrier_wait(iter_barrier);
      pthread_barrier_wait(
          phase_barrier); // Убедимся, что все прошли фазовый барьер
      pthread_exit(NULL);
    }

    // Потоки обрабатывают только строки, которые им назначены
    for (int j = start_row; j < end_row; ++j) {
      // Начинаем с k, потому что строки < k уже нули в столбце k-1
      if (j >= k) {
        factor = matrix[j][k - 1] / pivot_val;
        // Обнуляем элемент matrix[j][k-1]
        // Оптимизация: начинаем с k-1, т.к. элементы до него уже нули
        for (int i = k - 1; i < size + 1; ++i) {
          matrix[j][i] -= factor * matrix[k - 1][i];
        }
      }
    }
    // Все потоки должны завершить обработку своих строк для данного k
    pthread_barrier_wait(iter_barrier);
  }

  // printf("\nBefore obr hod\n");
  // print_matrix(matrix, size, size + 1);

  // --- Синхронизация перед обратным ходом ---
  pthread_barrier_wait(phase_barrier);

  // Нормализуем последнюю строку
  if (!(*error_flag) && (size - 1 >= start_row && size - 1 < end_row)) {
    pivot_val = matrix[size - 1][size - 1];
    if (fabs(pivot_val) < 1e-9) {
      fprintf(stderr,
              "Error: Division by near zero detected at (%d, %d) for the last "
              "row.\n",
              size - 1, size - 1);
      *error_flag = 1;
    } else {

      for (int j = size; j >= size - 1; --j) {
        matrix[size - 1][j] /= pivot_val;
      }
    }
  }

  // --- Обратный ход (нахождение решений) ---
  // Итерируемся по строкам снизу вверх, кроме последней, т.к. последняя уже
  // нормализована
  for (int i = size - 2; i >= 0; i--) {
    // Проверка на ошибку
    if (*error_flag)
      pthread_exit(NULL);

    // Потоки обрабатывают только свои строки
    if (i >= start_row && i < end_row) {
      pivot_val = matrix[i][i];

      // --- Проверка на деление на ноль ---
      if (fabs(pivot_val) < 1e-9) {
        fprintf(stderr,
                "Error: Division by near zero detected at (%d, %d) during back "
                "substitution.\n",
                i, i);
        *error_flag = 1;
        pthread_barrier_wait(iter_barrier);
        // phase_barrier здесь уже пройден, ничего не делаем
        pthread_exit(NULL);
      }

      // Обнуление элементов над диагональю
      for (int j = i + 1; j < size; ++j) {
        factor = matrix[i][j] / matrix[j][j];

        matrix[i][size] -= factor * matrix[j][size];
        matrix[i][j] -= factor * matrix[j][j];
      }
      // Нормализация для получения x_i

      for (int j = size; j >= i; --j) {
        matrix[i][j] /= pivot_val;
      }
    }
    // Все потоки должны завершить обработку своих строк для данного i
    pthread_barrier_wait(iter_barrier);
  }

  pthread_exit(NULL);
}

// --- Главная функция для решения СЛАУ методом Гаусса с потоками ---
int solve_gaussian_elimination_threaded(double **array_slau, int size,
                                        int num_threads) {
  if (num_threads <= 0) {
    fprintf(stderr, "Error: Number of threads must be positive.\n");
    return -1;
  }
  if (size <= 0) {
    fprintf(stderr, "Error: Matrix size must be positive.\n");
    return -1;
  }

  // --- 1. Создание копии матрицы ---
  // Работаем с копией, чтобы не изменять исходный массив
  double **matrix_copy = allocate_matrix(size, size + 1);
  if (matrix_copy == NULL)
    return -1;
  copy_matrix(matrix_copy, array_slau, size, size + 1);

  // --- 2. Инициализация барьеров и флага ошибки ---
  pthread_barrier_t iter_barrier;
  pthread_barrier_t phase_barrier;
  volatile int error_flag = 0; // 0 - нет ошибки, 1 - есть ошибка

  if (pthread_barrier_init(&iter_barrier, NULL, num_threads) != 0) {
    perror("pthread_barrier_init (iter_barrier) failed");
    free_matrix(matrix_copy, size);
    return -1;
  }
  if (pthread_barrier_init(&phase_barrier, NULL, num_threads) != 0) {
    perror("pthread_barrier_init (phase_barrier) failed");
    pthread_barrier_destroy(&iter_barrier);
    free_matrix(matrix_copy, size);
    return -1;
  }

  // --- 3. Распределение строк и создание потоков ---
  pthread_t threads[num_threads];
  thread_data_t thread_data[num_threads];

  // Определение количества строк на поток
  int rows_per_thread = size / num_threads;
  int extra_rows = size % num_threads; // Количество строк, которые будут
                                       // распределены по первым потокам

  int current_start_row = 0;

  for (int i = 0; i < num_threads; ++i) {
    thread_data[i].matrix = matrix_copy;
    thread_data[i].size = size;
    thread_data[i].iter_barrier = &iter_barrier;
    thread_data[i].phase_barrier = &phase_barrier;
    thread_data[i].error_flag = &error_flag;

    // Распределение строк
    thread_data[i].start_row = current_start_row;
    // Добавляем 1 строку, если есть "лишние" строки и этот поток среди первых
    // 'extra_rows'
    thread_data[i].end_row =
        current_start_row + rows_per_thread + (i < extra_rows ? 1 : 0);
    current_start_row = thread_data[i].end_row;

    // Создание потока
    if (pthread_create(&threads[i], NULL, thread_calculate_string,
                       &thread_data[i]) != 0) {
      perror("pthread_create failed");
      // Обработка ошибок: отмена уже созданных потоков и очистка
      for (int j = 0; j < i; ++j) {
        pthread_cancel(threads[j]); // Прерываем созданные потоки
      }
      for (int j = 0; j < i; ++j) {
        pthread_join(threads[j], NULL); // Ждем завершения всех потоков
      }
      pthread_barrier_destroy(&iter_barrier);
      pthread_barrier_destroy(&phase_barrier);
      free_matrix(matrix_copy, size);
      return -1;
    }
  }

  // --- 4. Ожидание завершения всех потоков ---
  for (int i = 0; i < num_threads; ++i) {
    pthread_join(threads[i], NULL);
  }

  // --- 5. Очистка барьеров ---
  pthread_barrier_destroy(&iter_barrier);
  pthread_barrier_destroy(&phase_barrier);

  // --- 6. Проверка флага ошибки ---
  if (error_flag) {
    fprintf(stderr, "An error occurred during calculation.\n");
    free_matrix(matrix_copy, size);
    return -1;
  }

  // --- 7. Копирование результатов в исходный массив ---
  copy_matrix(array_slau, matrix_copy, size, size + 1);

  // --- 8. Освобождение выделенной памяти ---
  free_matrix(matrix_copy, size);

  return 0;
}

// --- Вспомогательная функция: Тестовая функция для генерации матриц ---
void populate_test_matrix(double **matrix, int size, int test_case_id) {
  // Очищаем матрицу перед заполнением
  for (int i = 0; i < size; ++i) {
    for (int j = 0; j < size + 1; ++j) {
      matrix[i][j] = 0.0;
    }
  }

  switch (test_case_id) {
  case 0: // Обычная матрица 3x3
    // Система:
    // 2x + y - z = 8
    // -3x - y + 2z = -11
    // -2x + y + 2z = -3
    // Ожидаемое решение: x=2, y=3, z=-1
    matrix[0][0] = 2.0;
    matrix[0][1] = 1.0;
    matrix[0][2] = -1.0;
    matrix[0][3] = 8.0;
    matrix[1][0] = -3.0;
    matrix[1][1] = -1.0;
    matrix[1][2] = 2.0;
    matrix[1][3] = -11.0;
    matrix[2][0] = -2.0;
    matrix[2][1] = 1.0;
    matrix[2][2] = 2.0;
    matrix[2][3] = -3.0;
    break;
  case 1: // Простая матрица 2x2
    // Система:
    // 1x + 1y = 2
    // 2x - 1y = 1
    // Ожидаемое решение: x=1, y=1
    matrix[0][0] = 1.0;
    matrix[0][1] = 1.0;
    matrix[0][2] = 2.0;
    matrix[1][0] = 2.0;
    matrix[1][1] = -1.0;
    matrix[1][2] = 1.0;
    break;
  case 2: // Матрица 4x4 с нулями в диагонали
    // Система:
    // 5x0 + 2x1 + 0x2 + 1x3 = 18
    // 0x0 + 1x1 + 3x2 - 1x3 = 7
    // 2x0 + 0x1 + 4x2 + 1x3 = 16
    // 1x0 + 0x1 + 0x2 + 2x3 = 9
    // Ожидаемое решение: x0=1, x1=2, x2=1, x3=4
    matrix[0][0] = 5.0;
    matrix[0][1] = 2.0;
    matrix[0][2] = 0.0;
    matrix[0][3] = 1.0;
    matrix[0][4] = 18.0;
    matrix[1][0] = 0.0;
    matrix[1][1] = 1.0;
    matrix[1][2] = 3.0;
    matrix[1][3] = -1.0;
    matrix[1][4] = 7.0;
    matrix[2][0] = 2.0;
    matrix[2][1] = 0.0;
    matrix[2][2] = 4.0;
    matrix[2][3] = 1.0;
    matrix[2][4] = 16.0;
    matrix[3][0] = 1.0;
    matrix[3][1] = 0.0;
    matrix[3][2] = 0.0;
    matrix[3][3] = 2.0;
    matrix[3][4] = 9.0;
    break;
  case 3: // Матрица 3x3 с отрицательными числами
    // Система:
    // -x0 + 2x1 - 3x2 = 1
    // 2x0 - 4x1 + 1x2 = -5 <- обнуляется гиагональный элемент
    // -3x0 + 1x1 - 2x2 = 6
    // Ожидаемое решение: x0=-1, x1=-2, x2=-1
    matrix[0][0] = -1.0;
    matrix[0][1] = 2.0;
    matrix[0][2] = -3.0;
    matrix[0][3] = 1.0;
    matrix[1][0] = 2.0;
    matrix[1][1] = -4.0;
    matrix[1][2] = 1.0;
    matrix[1][3] = -5.0;
    matrix[2][0] = -3.0;
    matrix[2][1] = 1.0;
    matrix[2][2] = -2.0;
    matrix[2][3] = 6.0;
    break;
  case 4: // Матрица 1x1
    // Система:
    // 5x0 = 10
    // Ожидаемое решение: x0=2
    matrix[0][0] = 5.0;
    matrix[0][1] = 10.0;
    break;
  case 5: // Матрица 2х2, требующая pivoting (диагональный элемент 0)
    // Система:
    // 0x + 1y = 2
    // 2x - 1y = 1
    // Для решения нужно поменять строки местами.
    // Ожидаемое решение: x=1, y=2
    matrix[0][0] = 0.0;
    matrix[0][1] = 1.0;
    matrix[0][2] = 2.0;
    matrix[1][0] = 2.0;
    matrix[1][1] = -1.0;
    matrix[1][2] = 1.0;
    break;
  case 6: // Матрица, где max элемент не на диагонали
    // Система:
    // 1x + 2y + 3z = 6
    // 5x + 1y + 0z = 12  <-- max элемент в 0-м столбце находится здесь
    // 2x + 3y + 1z = 10
    // Ожидаемое решение: x=2, y=2, z=0
    matrix[0][0] = 1.0;
    matrix[0][1] = 2.0;
    matrix[0][2] = 3.0;
    matrix[0][3] = 6.0;
    matrix[1][0] = 5.0;
    matrix[1][1] = 1.0;
    matrix[1][2] = 0.0;
    matrix[1][3] = 12.0;
    matrix[2][0] = 2.0;
    matrix[2][1] = 3.0;
    matrix[2][2] = 1.0;
    matrix[2][3] = 10.0;
    break;
  }
}

int main(int argc, char *argv[]) {
  if (argc - 1 != 1) {
    fprintf(stderr, "Invalid args amount\nexpected: 1\ngot: %d\n", argc - 1);
    fprintf(stderr, "Usage: ./lab_02_osi_app [threads amount]");
    return 1;
  }
  int num_threads = atoi(argv[1]); // Количество потоков
  int num_threads_for_test =
      num_threads; // Количество потоков для отдельного теста

  // Список тестовых случаев: размерность и ID для populate_test_matrix
  struct {
    int size;
    int test_case_id;
  } test_cases[] = {
      {3, 0}, // Обычный случай 3x3
      {2, 1}, // Простая 2x2 система
      {4, 2}, // 4x4 матрица с нулями
      {3, 3}, // 3x3 матрица с отрицательными числами
      {1, 4}, // 1x1 матрица
      {2, 5}, // 2x2, требующая pivoting
      {3, 6}  // 3x3, требующая pivoting
  };
  int num_test_cases = sizeof(test_cases) / sizeof(test_cases[0]);

  for (int i = 0; i < num_test_cases; ++i) {
    int size = test_cases[i].size;
    int test_case_id = test_cases[i].test_case_id;

    // Нет смысла создавать больше потоков, чем строк в матрице
    // if (num_threads > size) {
    //   num_threads_for_test = size;
    // }

    printf("\n--- Running Test Case %d (Size: %d) ---\n", test_case_id, size);

    double **matrix = allocate_matrix(size, size + 1);
    if (!matrix)
      return 1;

    populate_test_matrix(matrix, size, test_case_id);

    print_matrix("--- Initial Matrix ---\n", matrix, size, size + 1);

    printf("Solving with %d threads...\n", num_threads_for_test);
    clock_t start_time = clock();

    int result =
        solve_gaussian_elimination_threaded(matrix, size, num_threads_for_test);

    clock_t end_time = clock();
    double time_spent_ms =
        ((double)(end_time - start_time) / CLOCKS_PER_SEC) * 1000.0;

    if (result == 0) {
      printf("\n--- Solutions ---\n");
      for (int j = 0; j < size; ++j) {
        printf("x[%d] = %f\n", j, matrix[j][size]);
      }
      printf("\nCalculation Time: %.2f ms\n\n", time_spent_ms);
      print_matrix("--- Final Matrix ---\n", matrix, size, size + 1);
    } else {
      fprintf(stderr, "Gaussian elimination failed for Test Case %d.\n",
              test_case_id);
    }

    // Освобождаем память для исходной матрицы теста
    free_matrix(matrix, size);
  }

  return 0;
}
