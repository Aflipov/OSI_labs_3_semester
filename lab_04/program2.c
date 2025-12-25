#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef float (*PiFunction)(int);
typedef int *(*SortFunction)(int *);

int main() {
  void *lib_handle = NULL;
  PiFunction pi_func = NULL;
  SortFunction sort_func = NULL;

  int lib_number = 1;
  char lib_path[256];

  snprintf(lib_path, sizeof(lib_path), "./lib%d.so", lib_number);
  lib_handle = dlopen(lib_path, RTLD_LAZY);

  if (!lib_handle) {
    fprintf(stderr, "Error opening library %s: %s\n", lib_path, dlerror());
    return 1;
  }

  pi_func = (PiFunction)dlsym(lib_handle, "Pi");
  sort_func = (SortFunction)dlsym(lib_handle, "Sort");

  if (!pi_func || !sort_func) {
    fprintf(stderr, "Error loading functions: %s\n", dlerror());
    dlclose(lib_handle);
    return 1;
  }

  int command;

  printf(
      "Commands:\n"
      " 0 - switch libs\tusage: 0\n"
      " 1 - Pi func\t\tusage: 1 <num_digits>\n"
      " 2 - array sort func\tusage: 2 <N = size> <elem1> <elem2> ... <elemN>\n"
      "-1 - exit\t\tusage: -1\n");

  while (1) {
    printf("Enter command: ");
    scanf("%d", &command);

    if (command == 0) {
      dlclose(lib_handle);

      lib_number = (lib_number == 1) ? 2 : 1;
      snprintf(lib_path, sizeof(lib_path), "./lib%d.so", lib_number);

      lib_handle = dlopen(lib_path, RTLD_LAZY);
      if (!lib_handle) {
        fprintf(stderr, "Error opening library %s: %s\n", lib_path, dlerror());
        return 1;
      }

      pi_func = (PiFunction)dlsym(lib_handle, "Pi");
      sort_func = (SortFunction)dlsym(lib_handle, "Sort");

      if (!pi_func || !sort_func) {
        fprintf(stderr, "Error loading functions: %s\n", dlerror());
        dlclose(lib_handle);
        return 1;
      }

      printf("Switched to lib%d.so\n", lib_number);
      continue;
    }

    if (command == 1) {
      int K;
      scanf("%d", &K);
      float result = pi_func(K);
      printf("Pi: %f\n", result);
    } else if (command == 2) {
      int size;
      scanf("%d", &size);

      if (size <= 0) {
        printf("Invalid array size\n");
        continue;
      }

      int *array = (int *)malloc((size + 1) * sizeof(int));
      if (array == NULL) {
        printf("Memory allocation failed\n");
        continue;
      }

      array[0] = size;
      for (int i = 1; i <= size; i++) {
        scanf("%d", &array[i]);
      }

      int *result = sort_func(array);

      printf("Result: ");
      for (int i = 1; i <= size; i++) {
        printf("%d ", result[i]);
      }
      printf("\n");

      free(array);
    } else if (command == -1) {
      break;
    } else {
      printf("Invalid command\n");
    }
  }

  dlclose(lib_handle);
  return 0;
}
