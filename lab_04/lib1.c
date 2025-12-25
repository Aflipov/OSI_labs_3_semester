#include "lib.h"
#include <math.h>
#include <stdlib.h>

// Implementation 1: Leibniz series for Pi
float Pi(int K) {
  if (K <= 0)
    return 0.0f;

  float pi = 0.0f;
  for (int n = 0; n < K; n++) {
    float term = pow(-1, n) / (2 * n + 1);
    pi += term;
  }
  return 4.0f * pi;
}

// Implementation 1: Bubble sort
int *Sort(int *array) {
  if (array == NULL)
    return NULL;

  int size = array[0];
  if (size <= 1)
    return array;

  int *data = array + 1;

  for (int i = 0; i < size - 1; i++) {
    for (int j = 0; j < size - 1 - i; j++) {
      if (data[j] > data[j + 1]) {
        int temp = data[j];
        data[j] = data[j + 1];
        data[j + 1] = temp;
      }
    }
  }

  return array;
}
