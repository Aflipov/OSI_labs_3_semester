#include "lib.h"
#include <stdlib.h>

// Implementation 2: Wallis formula for Pi
float Pi(int K) {
  if (K <= 0)
    return 0.0f;

  float pi = 1.0f;
  for (int n = 1; n <= K; n++) {
    float numerator = 2.0f * n;
    float denominator = 2.0f * n - 1.0f;
    pi *= (numerator / denominator) * (numerator / (denominator + 2.0f));
  }
  return 2.0f * pi;
}

static int partition(int *arr, int low, int high) {
  int pivot = arr[low];
  int i = low - 1;
  int j = high + 1;

  while (1) {
    do {
      i++;
    } while (arr[i] < pivot);

    do {
      j--;
    } while (arr[j] > pivot);

    if (i >= j) {
      return j;
    }

    int temp = arr[i];
    arr[i] = arr[j];
    arr[j] = temp;
  }
}

static void quickSort(int *arr, int low, int high) {
  if (low < high) {
    int pi = partition(arr, low, high);
    quickSort(arr, low, pi);
    quickSort(arr, pi + 1, high);
  }
}

// Implementation 2: Hoare's sort (Quick Sort)
int *Sort(int *array) {
  if (array == NULL)
    return NULL;

  int size = array[0];
  if (size <= 1)
    return array;

  int *data = array + 1;

  quickSort(data, 0, size - 1);

  return array;
}
