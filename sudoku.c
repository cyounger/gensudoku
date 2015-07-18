#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "util.h"
#include "sudoku.h"
#include "solver.h"

static void seed(sudoku *s);
static void init_shuffled_array(int *numbers, size_t n, int start);
static bool *get_dlx_cells(sudoku *s, size_t *count, size_t *ncols, size_t *nrows);
static void get_masks(sudoku *s, int *rows, int *cols, int *secs);
static void get_section_idxs(int sec_idx, int *array);
static void fill_solution(sudoku *s, int *set, size_t n);
static void remove_deduced_hints(sudoku *s, int *order, size_t n);
static void remove_non_unique_hints(sudoku *s, int *order, size_t n);
static void add_extra_hints(sudoku *s, sudoku *solution, int extra_hints);

// Get the index into the sudoku grid array
#define GRID_IDX(x, y) ((y)*(SUDOKU_SIZE) + (x))

// Get the grid x and y position from the sudoku grid index
#define GRID_X(idx) ((idx)%(SUDOKU_SIZE))
#define GRID_Y(idx) ((idx)/(SUDOKU_SIZE))

// Get the section index given the sudoku cell position
#define SEC_IDX(x, y) (((y)/3)*3 + (x)/3)

// The size of the DLX array
#define DLX_MAX_ROWS (GRID_SIZE*SUDOKU_SIZE)
#define DLX_MAX_COLS (4*GRID_SIZE)

// Given a sudoku position and value, get the DLX row index and DLX
// column indices for each of the four types of constraints
#define DLX_ROW(v, x, y) (GRID_SIZE*(y) + SUDOKU_SIZE*(x) + (v))
#define DLX_COL1(v, x, y) (SUDOKU_SIZE*(y) + (x))
#define DLX_COL2(v, x, y) (GRID_SIZE + SUDOKU_SIZE*(y) + (v))
#define DLX_COL3(v, x, y) (2*GRID_SIZE + SUDOKU_SIZE*(x) + (v))
#define DLX_COL4(v, x, y) (3*GRID_SIZE + SUDOKU_SIZE*SEC_IDX(x, y) + (v))

// Get the index into the DLX cell array given a DLX row and column
#define DLX_CELL_IDX(r, c) ((r)*DLX_MAX_COLS + (c))

// Get the sudoku position or value given the DLX row index
#define DLX_X(r) (((r)/9)%9)
#define DLX_Y(r) (((r)/9)/9)
#define DLX_V(r) (((r)%9)+1)

// Solve the sudoku puzzle and fill in the solution
bool sudoku_solve(sudoku *s)
{
  size_t count, ncols, nrows;
  bool *cells = get_dlx_cells(s, &count, &ncols, &nrows);
  solver *slvr = solver_create(count, ncols, nrows);
  bool solved = false;

  // The solution returned by the DLX solver will be a set of DLX row
  // indices that can be transformed to number placements by
  // fill_solution
  int set[GRID_SIZE];

  solver_init_graph(slvr, cells, false);
  solved = solver_run(slvr, DLX_RANDOM, set, GRID_SIZE);
  if (solved) {
    fill_solution(s, set, GRID_SIZE);
  }

  free(cells);
  solver_destroy(slvr);
  return solved;
}

// Initialize a sudoku object to contain an unsolved puzzle, while
// filling in the solution into another sudoku object.
void sudoku_generate(sudoku *s, sudoku *solution, int extra_hints)
{
  assert(s);
  assert(solution);

  // Partially prefill an empty grid (to speed up generation) and
  // solve it.
  seed(s);
  if (!sudoku_solve(s)) {
    warn("could not generate sudoku puzzle");
    return;
  }

  // Copy the solution before removing hints
  memcpy(solution, s, sizeof(sudoku));

  // Go through the hints in random order. If the hint can be deduced
  // from the other hints, remove it.
  int hints[GRID_SIZE];
  init_shuffled_array(hints, GRID_SIZE, 0);
  remove_deduced_hints(s, hints, GRID_SIZE);
  
  // Remove hints that lead to multiple solutions
  remove_non_unique_hints(s, hints, GRID_SIZE);

  // Add back in some hints to make it easier
  add_extra_hints(s, solution, extra_hints);
}

void sudoku_print(sudoku *s, FILE *fp)
{
  assert(s);
  assert(fp);

  for (int y = 0; y < 9; y++) {
    if (y % 3 == 0 && y != 0) {
      fprintf(fp, "------+-------+------\n");
    }
    for (int x = 0; x < 9; x++) {
      if (x % 3 == 0 && x != 0) {
        fprintf(fp, "| ");
      }
      if (s->grid[GRID_IDX(x, y)] == 0) {
        fprintf(fp, ". ");
      } else {
        fprintf(fp, "%d ", s->grid[GRID_IDX(x, y)]);
      }
    }
    fprintf( fp, "\n" );
  }
}

// Seed an empty sudoku grid by filling in the first row randomly
void seed(sudoku *s)
{
  assert(s);

  int numbers[SUDOKU_SIZE];
  init_shuffled_array(numbers, SUDOKU_SIZE, 1);
  memset(s->grid, 0, sizeof(s->grid));
  for (int i = 0; i < SUDOKU_SIZE; i++) {
    s->grid[GRID_IDX(i, 0)] = (sudoku_value) numbers[i];
  }
}

// Initialize an array with consecutive integers and then shuffle it
void init_shuffled_array(int *numbers, size_t n, int start)
{
  assert(numbers);
  for (int i = 0; i < n; i++) {
    numbers[i] = i + start;
  }
  shuffle(numbers, n);
}

// Create an array of booleans to be used to build the DLX graph. The
// caller should free the array.
//
// The rows of this array correspond to possible actions, i.e. putting
// a value v in the sudoku cell at grid coordinates x, y. For a 9x9
// sudoku, there are 9*9*9 possible actions.
//
// The columns of this array correspond to contraints imposed by the
// rules of sudoku. There are four different categories of contraints:
//
//  1. Each position in the grid must be filled
//  2. Each row must have the numbers 1-9 (for a 9x9 sudoku)
//  3. Each column must have the numbers 1-9.
//  4. Each section must have the numbers 1-9.
//
// For a 9x9 sudoku, the first category contains 81 constraints. Then,
// there are 9 rows and each must have the numbers 1-9, which adds to
// another 81 constraints. In total, there are 324 constraints.
//
bool *get_dlx_cells(sudoku *s, size_t *count, size_t *ncols, size_t *nrows)
{
  assert(s);
  assert(count);
  assert(ncols);
  assert(nrows);

  bool *cells = calloc(DLX_MAX_COLS*DLX_MAX_ROWS, sizeof(bool));
  if (cells == NULL) {
    fatal("failed to allocate memory for dlx cells");
  }
  *count = 0;
  *ncols = DLX_MAX_COLS;
  *nrows = DLX_MAX_ROWS;

  int row_masks[SUDOKU_SIZE], col_masks[SUDOKU_SIZE], sec_masks[SUDOKU_SIZE];
  get_masks(s, row_masks, col_masks, sec_masks);

  for (int x = 0; x < SUDOKU_SIZE; x++) {
    for (int y = 0; y < SUDOKU_SIZE; y++) {
      if (s->grid[GRID_IDX(x, y)] == 0) {
        int sec = SEC_IDX(x, y);
        int net_mask = row_masks[y] | col_masks[x] | sec_masks[sec];
        for (int v = 0; v < 9; v++) {
          // Skip over rows if they would lead to duplicates in a row,
          // column, or section.
          if ((net_mask & (1<<(v+1))) == 0) {
            int row = DLX_ROW(v, x, y);
            cells[DLX_CELL_IDX(row, DLX_COL1(v, x, y))] = true;
            cells[DLX_CELL_IDX(row, DLX_COL2(v, x, y))] = true;
            cells[DLX_CELL_IDX(row, DLX_COL3(v, x, y))] = true;
            cells[DLX_CELL_IDX(row, DLX_COL4(v, x, y))] = true;
            *count += 4;
          }
        }
      }
    }
  }

  return cells;
}

// Compute masks of which values are in use for each row, column, and
// section of the sudoku grid. The masks are stored in passed in
// arrays, each whose size is SUDOKU_SIZE.
static void get_masks(sudoku *s, int *rows, int *cols, int *secs)
{
  assert(s);
  assert(rows);
  assert(cols);
  assert(secs);

  int sec_idxs[SUDOKU_SIZE] = { 0 };
  for (int i = 0; i < SUDOKU_SIZE; i++) {
    rows[i] = 0;
    cols[i] = 0;
    secs[i] = 0;
    get_section_idxs(i, sec_idxs);
    for (int j = 0; j < SUDOKU_SIZE; j++) {
      rows[i] |= (1 << s->grid[GRID_IDX(j, i)]);
      cols[i] |= (1 << s->grid[GRID_IDX(i, j)]);
      secs[i] |= (1 << s->grid[sec_idxs[j]]);
    }
  }
}

// Fill the provided array with the sudoku grid indices for the given
// numbered section. The array should be SUDOKU_SIZE in length.
static void get_section_idxs(int sec_idx, int *array)
{
  for (int i = 0; i < SUDOKU_SIZE; i++) {
    int sx = (sec_idx%3)*3 + i%3;
    int sy = (sec_idx/3)*3 + i/3;
    array[i] = GRID_IDX(sx, sy);
  }
}

// Fill in the sudoku grid based on the solution set produced by the
// DLX algorithm.
static void fill_solution(sudoku *s, int *set, size_t n)
{
  assert(s);
  assert(set);

  for (int i = 0; i < n; i++) {
    int r = set[i];
    if (r != -1) {
      s->grid[GRID_IDX(DLX_X(r), DLX_Y(r))] = DLX_V(r);
    }
  }
}

// From a completely solved puzzle, remove hints that can be deduced
// from other hints. The order the hints should be processed in is
// specified in the order array of size n. The order array should be
// randomized by the caller.
static void remove_deduced_hints(sudoku *s, int *order, size_t n)
{
  assert(s);
  assert(order);

  int row_masks[SUDOKU_SIZE], col_masks[SUDOKU_SIZE], sec_masks[SUDOKU_SIZE];

  // Since the sudoku is initially solved, set the masks to have all
  // bits on
  int all_on = (1 << (SUDOKU_SIZE+1)) - 2;
  for ( int i = 0; i < SUDOKU_SIZE; i++ ) {
    row_masks[i] = col_masks[i] = sec_masks[i] = all_on;
  }

  // Go through each hint and remove it if it can be deduced from the
  // other hints
  for (int i = 0; i < n; i++) {
    int x = GRID_X(order[i]), y = GRID_Y(order[i]), sec = SEC_IDX(x, y);
    // Compute which numbers are used in this row, column, and
    // section. If all numbers are used then removing the hint at this
    // position, then it can be deduced and is not needed.
    int used = row_masks[y] | col_masks[x] | sec_masks[sec];
    if (used == all_on) {
      int remove = ~ (1 << s->grid[GRID_IDX(x, y)]);
      row_masks[y] &= remove;
      col_masks[x] &= remove;
      sec_masks[sec] &= remove;
      s->grid[GRID_IDX(x, y)] = 0;
    }
  }
}

// Remove hints that lead to multiple solutions. The order the hints
// should be processed in is specified in the order array of size
// n. The order array should be randomized by the caller.
static void remove_non_unique_hints(sudoku *s, int *order, size_t n)
{
  assert(s);
  assert(order);

  size_t count, ncols, nrows;
  int set[GRID_SIZE];

  for (int i = 0; i < n; i++) {
    int x = GRID_X(order[i]), y = GRID_Y(order[i]);
    sudoku_value v = s->grid[GRID_IDX(x, y)];
    if (v != 0) {
      // Tentatively remove the hint, and then search for a unique
      // solution
      s->grid[GRID_IDX(x, y)] = 0;
      bool *cells = get_dlx_cells(s, &count, &ncols, &nrows);
      solver *checker = solver_create(count, ncols, nrows);
      solver_init_graph(checker, cells, false);
      free(cells);
      bool unique = solver_run(checker, DLX_UNIQUE, set, GRID_SIZE);
      // Add the hint back in if a unique solution was found
      if (!unique) {
        s->grid[GRID_IDX(x, y)] = v;
      }
      solver_destroy(checker);
    }
  }
}

// Copy num hints from the solution to make the puzzle easier
void add_extra_hints(sudoku *s, sudoku *solution, int num)
{
  assert(s);
  assert(solution);

  if (num <= 0) {
    return;
  }

  // Find all possible hints and randomize their order
  int hints[GRID_SIZE] = { 0 };
  size_t num_choices = 0;
  for (int i = 0; i < GRID_SIZE; i++) {
    if (s->grid[i] == 0) {
      hints[num_choices++] = i;
    }
  }
  shuffle(hints, num_choices);

  if (num > num_choices) {
    num = num_choices;
  }
  for (int i = 0; i < num; i++) {
    s->grid[hints[i]] = solution->grid[hints[i]];
  }
}
