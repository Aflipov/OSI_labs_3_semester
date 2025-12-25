#include "lib.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
  int command;

  printf(
      "Commands:\n"
      " 1 - Pi func\t\tusage: 1 <num_digits>\n"
      " 2 - array sort func\tusage: 2 <N = size> <elem1> <elem2> ... <elemN>\n"
      "-1 - exit\t\tusage: -1\n");

  while (1) {
    printf("Enter command: ");
    scanf("%d", &command);

    if (command == 1) {
      int K;
      scanf("%d", &K);
      float result = Pi(K);
      printf("Pi: %f\n", result);
    } else if (command == 2) {
      int size;
      scanf("%d", &size);

      if (size <= 0) {
        printf("Invalid array size\n");
        return 1;
      }

      int *array = (int *)malloc((size + 1) * sizeof(int));
      if (array == NULL) {
        printf("Memory allocation failed\n");
        return 1;
      }

      array[0] = size;
      for (int i = 1; i <= size; i++) {
        scanf("%d", &array[i]);
      }

      int *result = Sort(array);

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

  return 0;
}
