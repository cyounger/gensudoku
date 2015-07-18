#ifndef __SUDOKU_H__
#define __SUDOKU_H__

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

typedef uint8_t sudoku_value;

#define SUDOKU_SIZE 9
#define GRID_SIZE (SUDOKU_SIZE*SUDOKU_SIZE)

// TODO: to support the sudoku size being set at runtime, the grid
// needs to be created on the heap. In this case sudoku_create/destroy
// should be created, and this struct should be defined in the .c
// file. Also, there are several macros/functions, specifically
// dealing with sections, that would need to somehow be
// defined/provided. These are: SEC_IDX, sudoku_print,
// get_section_idx. These assume sections are 3x3 boxes in a 9x9 grid,
// which won't be the case for a 7x7 sudoku for example.

typedef struct {
  sudoku_value grid[GRID_SIZE];
} sudoku;

bool sudoku_solve(sudoku *s);
void sudoku_generate(sudoku *s, sudoku *solution, int extra_hints);
void sudoku_print(sudoku *s, FILE *fp);

#endif
