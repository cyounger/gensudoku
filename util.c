#include <stdio.h>
#include <assert.h>
#include "util.h"

void log_msg(const char *file, int line, const char *fmt, ...)
{
  va_list lst;
  va_start(lst, fmt);
  if (file != NULL) {
    fprintf(stderr, "%s:%d ", file, line);
  }
  vfprintf(stderr, fmt, lst);
  va_end(lst);
  fprintf(stderr, "\n");
}

// Shuffle an array of ints of size n in place
void shuffle(int *a, size_t n)
{
  assert(a);
  for (int i = n-1; i >= 1; i--) {
    int j = rand() % (i+1);
    int tmp = a[i];
    a[i] = a[j];
    a[j] = tmp;
  }
}
