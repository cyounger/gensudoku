#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include "sudoku.h"
#include "util.h"

static void usage(void)
{
  printf("Usage: gensudoku [options]\n\n"
         "Options:\n"
         "  -s SEED, --seed=SEED      Use a specific seed\n"
         "  -a NUM, --add-hints=NUM   Add NUM extra hints to the puzzle\n"
         "  --solution                Print the solution\n"
         );
}

int main(int argc, char **argv)
{
  sudoku puzzle, solution;
  int c, show_solution = 0, extra_hints = 0;
  unsigned int seed = time(NULL);
  char *end;
  long val;

  const struct option long_options[] = {
    { "solution",  no_argument,       &show_solution, 1   },
    { "seed",      required_argument, 0,              's' },
    { "add-hints", required_argument, 0,              'a' },
    { 0,           0,                 0,              0   },
  };

  while ((c = getopt_long(argc, argv, "s:a:", long_options, NULL)) != -1) {
    switch (c) {
    case 0:
      // getopt_long already set show_solution
      break;
    case 's':
      val = strtol(optarg, &end, 0);
      if (*end != '\0' || errno == ERANGE) {
        warn("warning: unable to parse seed: %s", optarg);
      } else if (val < 0 || val > UINT_MAX) {
        warn("warning: seed does not fit into unsigned int");
      } else {
        seed = val;
      }
      break;
    case 'a':
      extra_hints = atoi(optarg);
      break;
    default:
      usage();
      exit(EXIT_FAILURE);
    }
  }

  printf("seed: %u\n", seed);
  srand(seed);
  sudoku_generate(&puzzle, &solution, extra_hints);
  if (show_solution) {
    sudoku_print(&solution, stdout);
  } else {
    sudoku_print(&puzzle, stdout);
  }

  return 0;
}
