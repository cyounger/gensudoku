#ifndef __SOLVER_H__
#define __SOLVER_H__

#include <stdbool.h>

typedef enum {
  DLX_RANDOM, // Find a random solution
  DLX_UNIQUE, // Check that there is exactly one solution
} dlx_mode;

typedef struct node node;
typedef struct solver solver;

solver *solver_create(size_t inuse, size_t ncols, size_t nrows);
void solver_destroy(solver *s);
void solver_init_graph(solver *s, bool *cells, bool strict);
bool solver_run(solver *s, dlx_mode search_mode, int *solution, size_t size);

#endif
