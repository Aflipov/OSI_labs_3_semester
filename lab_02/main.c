#include <complex.h>
#include <math.h> // Для fabs()
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // Для memcpy()
#include <time.h>   // Для clock()

// --- Структура для хранения данных, передаваемых потоку ---
typedef struct {
  double **matrix;                  // Указатель на расширенную матрицу
  int size;                         // Размерность матрицы (N)
  int thread_id;                    // ID потока (от 0 до num_threads-1)
  int num_threads;                  // Общее количество потоков
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

// --- Функция, выполняемая каждым потоком ---
void *thread_calculate_string(void *arg) {
  thread_data_t *data = (thread_data_t *)arg;
  double **matrix = data->matrix;
  int size = data->size;
  int thread_id = data->thread_id;
  int num_threads = data->num_threads;
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
    // Проверку выполняет только поток 0, чтобы избежать дублирования сообщений
    double local_pivot_val;
    if (thread_id == 0) {
      local_pivot_val = matrix[k - 1][k - 1];
      // --- Проверка на деление на ноль ---
      if (fabs(local_pivot_val) < 1e-9) {
        fprintf(stderr,
                "Error: Division by near zero detected at (%d, %d) during "
                "forward elimination.\n",
                k - 1, k - 1);
        *error_flag = 1;
        pthread_barrier_wait(iter_barrier);
        pthread_barrier_wait(phase_barrier);
        pthread_exit(NULL);
      }
    }

    // Синхронизация: все потоки должны дождаться проверки ведущего элемента
    pthread_barrier_wait(iter_barrier);

    // Если была ошибка, выходим
    if (*error_flag)
      pthread_exit(NULL);

    // Поток 0 уже знает pivot_val, остальные читают его один раз
    if (thread_id == 0) {
      pivot_val = local_pivot_val;
    } else {
      pivot_val = matrix[k - 1][k - 1];
    }

    // Вычисляем диапазон строк для обработки текущим потоком
    // Нужно обработать строки от k до size-1 (всего size - k строк)
    int rows_to_process = size - k;

    // Оптимизация: если работы мало, используем меньше потоков
    // Это уменьшает накладные расходы на синхронизацию
    int effective_threads =
        (rows_to_process < num_threads) ? rows_to_process : num_threads;

    // Если текущий поток не нужен для этой итерации, пропускаем
    if (thread_id >= effective_threads) {
      pthread_barrier_wait(iter_barrier);
      continue;
    }

    int rows_per_thread = rows_to_process / effective_threads;
    int extra_rows = rows_to_process % effective_threads;

    // Определяем диапазон строк для текущего потока
    int start_row = k + thread_id * rows_per_thread;
    if (thread_id < extra_rows) {
      start_row += thread_id;
    } else {
      start_row += extra_rows;
    }
    int end_row =
        start_row + rows_per_thread + (thread_id < extra_rows ? 1 : 0);

    // Кэшируем ведущую строку для уменьшения обращений к памяти
    // (оптимизация для больших матриц)
    double *pivot_row = matrix[k - 1];

    // Обрабатываем назначенные строки
    for (int j = start_row; j < end_row; ++j) {
      factor = matrix[j][k - 1] / pivot_val;
      // Вычитаем ведущую строку (k-1) из текущей строки j
      // Начинаем с k-1, т.к. элементы до него уже нули
      // Используем указатель на ведущую строку для лучшей локальности кэша
      for (int i = k - 1; i < size + 1; ++i) {
        matrix[j][i] -= factor * pivot_row[i];
      }
    }

    // Все потоки должны завершить обработку своих строк для данного k
    pthread_barrier_wait(iter_barrier);
  }

  // --- Синхронизация перед обратным ходом ---
  pthread_barrier_wait(phase_barrier);

  // --- Обратный ход выполняется только потоком 0 (последовательно) ---
  if (thread_id == 0) {
    // Нормализуем последнюю строку
    if (!(*error_flag)) {
      pivot_val = matrix[size - 1][size - 1];
      if (fabs(pivot_val) < 1e-9) {
        fprintf(
            stderr,
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
        break;

      pivot_val = matrix[i][i];

      // --- Проверка на деление на ноль ---
      if (fabs(pivot_val) < 1e-9) {
        fprintf(stderr,
                "Error: Division by near zero detected at (%d, %d) during back "
                "substitution.\n",
                i, i);
        *error_flag = 1;
        break;
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
  }

  // Все потоки ждут завершения обратного хода
  pthread_barrier_wait(phase_barrier);

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

  // --- 3. Создание потоков ---
  pthread_t threads[num_threads];
  thread_data_t thread_data[num_threads];

  for (int i = 0; i < num_threads; ++i) {
    thread_data[i].matrix = matrix_copy;
    thread_data[i].size = size;
    thread_data[i].thread_id = i;
    thread_data[i].num_threads = num_threads;
    thread_data[i].iter_barrier = &iter_barrier;
    thread_data[i].phase_barrier = &phase_barrier;
    thread_data[i].error_flag = &error_flag;

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

/** Вспомогательная функция генерации матриц
 * @brief Генерирует матрицу с диагональным преобладанием для системы линейных
 * уравнений.
 *
 * Диагональные элементы значительно больше внедиагональных, что гарантирует
 * отсутствие обнуления диагональных элементов при прямом ходе метода Гаусса.
 *
 * @param size Количество уравнений (размерность квадратной части матрицы).
 * @param diag_multiplier Множитель для диагонального элемента, зависящий от
 * индекса строки. Большее значение усиливает диагональное преобладание.
 * @param base_diag_value Базовое значение для диагонального элемента.
 * @param off_diag_value Значение внедиагональных элементов. Должно быть меньше,
 *                       чем наименьшее значение диагонального элемента.
 * @param rhs_shift_multiplier Множитель для сдвига правой части (вектора
 * свободных членов).
 * @return Указатель на сгенерированную матрицу (rows x (cols+1)), или NULL в
 * случае ошибки.
 */
int generate_dominant_diagonal_matrix(double **matrix, int size,
                                      double diag_multiplier,
                                      double base_diag_value,
                                      double off_diag_value,
                                      double rhs_shift_multiplier) {
  // Матрица будет size x (size + 1), где последний столбец - вектор свободных
  // членов.
  if (matrix == NULL) {
    return 1; // Ошибка выделения памяти
  }

  for (int i = 0; i < size; ++i) {
    // Вычисляем диагональный элемент
    double diag_val = (double)(i * diag_multiplier) + base_diag_value;
    double sum_coeffs = 0.0; // Для расчета правой части

    for (int j = 0; j < size; ++j) {
      if (i == j) {
        matrix[i][j] = diag_val;
        sum_coeffs += diag_val;
      } else {
        matrix[i][j] = off_diag_value; // Внедиагональные элементы
        sum_coeffs += off_diag_value;
      }
    }

    // Правая часть (вектор свободных членов): сумма коэффициентов + сдвиг
    // Сдвиг зависит от индекса строки i.
    matrix[i][size] = sum_coeffs + (double)(i * rhs_shift_multiplier);

    // Проверка на обнуление диагонального элемента (для большей уверенности,
    // хотя при таком подходе это маловероятно).
    if (fabs(matrix[i][i]) <
        1e-9) { // Использование малой величины для сравнения с нулем
      fprintf(stderr,
              "Warning: Diagonal element became zero at row %d during matrix "
              "generation. Adjust parameters.\n",
              i);
      // В реальном приложении можно либо скорректировать, либо вернуть NULL
      // Для данной задачи, с хорошими параметрами, это маловероятно.
    }
  }
  return 0;
}

/** Вспомогательная функция: Тестовая функция для генерации матриц
 * @brief Генерирует тестовые матрицы различных размеров и типов
 *
 * Поддерживает:
 * - Маленькие тестовые матрицы (1x1, 2x2, 3x3, 4x4)
 * - Автоматическую генерацию матриц с диагональным преобладанием
 *   для размеров от 10x10 до 100x100 с шагом 10
 *
 * @param matrix Указатель на матрицу для заполнения
 * @param size Размерность матрицы
 * @param test_case_id ID тестового случая:
 *   0 - Матрица 1x1 (простой тест)
 *   1 - Матрица 3x3 (стандартный тест)
 *   2 - Матрица 2x2 (простой тест)
 *   3 - Матрица 4x4 (тест с нулями)
 *   10-100 - Автоматическая генерация матрицы size x size с диагональным
 *            преобладанием (размер должен совпадать с test_case_id)
 */
void populate_test_matrix(double **matrix, int size, int test_case_id) {
  // Очищаем матрицу перед заполнением
  for (int i = 0; i < size; ++i) {
    for (int j = 0; j < size + 1; ++j) {
      matrix[i][j] = 0.0;
    }
  }

  switch (test_case_id) {
  case 0: // Матрица 1x1
    // Система: 5x0 = 10
    // Ожидаемое решение: x0=2
    if (size == 1) {
      matrix[0][0] = 5.0;
      matrix[0][1] = 10.0;
    }
    break;

  case 1: // Обычная матрица 3x3
    // Система:
    // 2x + y - z = 8
    // -3x - y + 2z = -11
    // -2x + y + 2z = -3
    // Ожидаемое решение: x=2, y=3, z=-1
    if (size == 3) {
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
    }
    break;

  case 2: // Простая матрица 2x2
    // Система:
    // 2x + y = 5
    // x - y = 1
    // Ожидаемое решение: x=2, y=1
    if (size == 2) {
      matrix[0][0] = 2.0;
      matrix[0][1] = 1.0;
      matrix[0][2] = 5.0;
      matrix[1][0] = 1.0;
      matrix[1][1] = -1.0;
      matrix[1][2] = 1.0;
    }
    break;

  case 3: // Матрица 4x4 (тест с нулями)
    if (size == 4) {
      // Генерируем матрицу 4x4 с диагональным преобладанием
      generate_dominant_diagonal_matrix(matrix, size, 1.5, 8.0, 0.5, 2.0);
    }
    break;

  default:
    // Автоматическая генерация для размеров от 10 до 100
    // test_case_id должен совпадать с size для автоматической генерации
    if (test_case_id >= 10 && test_case_id <= 100 && test_case_id == size) {
      // Генерируем матрицу с диагональным преобладанием
      // Параметры подобраны для обеспечения устойчивости
      double diag_multiplier = 1.0 + (size / 50.0); // Увеличиваем с размером
      double base_diag_value = 10.0 + size * 0.1;   // Базовое значение растет
      double off_diag_value = 0.5;                  // Внедиагональные элементы
      double rhs_shift_multiplier = 2.0;            // Сдвиг правой части

      generate_dominant_diagonal_matrix(matrix, size, diag_multiplier,
                                        base_diag_value, off_diag_value,
                                        rhs_shift_multiplier);
    } else {
      // Если размер не совпадает с test_case_id, используем стандартные
      // параметры
      generate_dominant_diagonal_matrix(matrix, size, 2.0, 10.0, 1.0, 3.0);
    }
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
      // Маленькие тестовые матрицы
      {1, 0}, // Матрица 1x1
      {2, 2}, // Простая 2x2 система
      {3, 1}, // Обычная матрица 3x3
      {4, 3}, // Матрица 4x4
      // Автоматическая генерация матриц разных размеров (10, 20, 30, ..., 100)
      {10, 10},   // 10x10
      {20, 20},   // 20x20
      {30, 30},   // 30x30
      {40, 40},   // 40x40
      {50, 50},   // 50x50
      {60, 60},   // 60x60
      {70, 70},   // 70x70
      {80, 80},   // 80x80
      {90, 90},   // 90x90
      {100, 100}, // 100x100
  };
  int num_test_cases = sizeof(test_cases) / sizeof(test_cases[0]);

  for (int i = 0; i < num_test_cases; ++i) {
    int size = test_cases[i].size;
    int test_case_id = test_cases[i].test_case_id;

    // Нет смысла создавать больше потоков, чем строк в матрице
    // if (num_threads > size) {
    //   num_threads_for_test = size;
    // }

    printf("\n--- Running Test Case %d (Size: %dx%d) ---\n", test_case_id, size,
           size);

    double **matrix = allocate_matrix(size, size + 1);
    if (!matrix)
      return 1;

    populate_test_matrix(matrix, size, test_case_id);

    // Печатаем матрицу только для маленьких размеров (<= 10)
    if (size <= 10) {
      print_matrix("--- Initial Matrix ---\n", matrix, size, size + 1);
    } else {
      printf("--- Initial Matrix: %dx%d (too large to print) ---\n", size,
             size + 1);
    }

    printf("Solving with %d threads...\n", num_threads_for_test);
    clock_t start_time = clock();

    int result =
        solve_gaussian_elimination_threaded(matrix, size, num_threads_for_test);

    clock_t end_time = clock();
    double time_spent_ms =
        ((double)(end_time - start_time) / CLOCKS_PER_SEC) * 1000.0;

    if (result == 0) {
      printf("\n--- Solutions ---\n");
      // Показываем только первые 5 и последние 5 решений для больших матриц
      if (size <= 10) {
        for (int j = 0; j < size; ++j) {
          printf("x[%d] = %f\n", j, matrix[j][size]);
        }
      } else {
        // Первые 5 решений
        for (int j = 0; j < 5 && j < size; ++j) {
          printf("x[%d] = %f\n", j, matrix[j][size]);
        }
        printf("... (showing first 5 and last 5 solutions) ...\n");
        // Последние 5 решений
        int start = (size > 5) ? size - 5 : 5;
        for (int j = start; j < size; ++j) {
          printf("x[%d] = %f\n", j, matrix[j][size]);
        }
      }
      printf("\nCalculation Time: %.2f ms\n", time_spent_ms);
      // Печатаем финальную матрицу только для маленьких размеров
      if (size <= 10) {
        print_matrix("--- Final Matrix ---\n", matrix, size, size + 1);
      } else {
        printf("--- Final Matrix: %dx%d (too large to print) ---\n\n", size,
               size + 1);
      }
    } else {
      fprintf(stderr, "Gaussian elimination failed for Test Case %d.\n",
              test_case_id);
    }

    // Освобождаем память для исходной матрицы теста
    free_matrix(matrix, size);
  }

  return 0;
}
