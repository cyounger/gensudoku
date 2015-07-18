#include <stdlib.h>
#include <assert.h>
#include "util.h"
#include "solver.h"

struct node {
  node *left, *right, *up, *down;
  node *column;
  size_t count; // count and rownum could be in a union
  size_t rownum;
};

struct solver {
  node *root;
  node *nodes;
  int *solution;
  size_t solution_count;
  size_t solution_size;
  dlx_mode mode;
  size_t nrows;
  size_t ncols;
};

static bool search(solver *s, int k);
static void cover(node *column);
static void uncover(node *column);

// Create a new dancing links (DLX) solver. In order to allocate
// memory, this needs to know some information about the exact cover
// matrix: the number of cells that are on and the dimensions of the
// matrix.
solver *solver_create(size_t inuse, size_t ncols, size_t nrows)
{
  solver *s = calloc(1, sizeof(solver));
  if (s == NULL) {
    fatal("failed to allocate memory for solver");
  }

  s->ncols = ncols;
  s->nrows = nrows;

  size_t needed = inuse + ncols + 1;
  if ((s->nodes = calloc(needed, sizeof(node))) == NULL) {
    fatal("failed to allocate memory for solver nodes");
  }

  return s;
}

void solver_destroy(solver *s)
{
  assert(s);
  free(s->nodes);
  free(s);
}

void solver_init_graph(solver *s, bool *cells, bool strict)
{
  assert(s);
  assert(cells);

  node *nodes = s->nodes;

  // Link together column headers 1 through ncols-2
  int nodes_used = s->ncols;
  for (int col = 1; col < s->ncols-1; col++) {
    nodes[col].left = &nodes[col-1];
    nodes[col].right = &nodes[col+1];
  }

  // Link root in and create circular list for column headers
  s->root = &nodes[nodes_used++];
  s->root->right = &nodes[0];
  nodes[0].left = s->root;
  nodes[0].right = &nodes[1];
  nodes[s->ncols-1].left = &nodes[s->ncols-2];
  nodes[s->ncols-1].right = s->root;
  s->root->left = &nodes[s->ncols-1];

  // Build a circular list for each column. Store the nodes used so
  // that the rows can also be made into a circular list on another
  // pass.
  node **used = calloc(s->ncols*s->nrows, sizeof(node *));
  if (used == NULL) {
    fatal("failed to allocate memory for solver used nodes");
  }
  for (int col = 0; col < s->ncols; col++) {
    // Set current to the column header
    node *current = &nodes[col];
    for (int row = 0; row < s->nrows; row++) {
      if (cells[row*s->ncols+col]) {
        node *n = &nodes[nodes_used++];
        current->down = n;
        n->up = current;
        n->column = &nodes[col];
        // Increment the column header's total count
        nodes[col].count++;
        // Set the row number for this node
        n->rownum = row;
        current = n;
        // There's no way to determine what n->left and n->right should
        // be from here; store a pointer to n so this can be done later.
        used[row*s->ncols+col] = n;
      }
    }
    // Make circular by pointing to the column header
    current->down = &nodes[col];
    nodes[col].up = current;
  }

  // Build the circular lists for each row
  for (int row = 0; row < s->nrows; row++) {
    node *current = NULL;
    node *first = NULL;
    for (int col = 0; col < s->ncols; col++) {
      node *n = used[row*s->ncols+col];
      if (n != NULL) {
        if (first == NULL) {
          first = n;
          current = first;
        } else {
          current->right = n;
          n->left = current;
          current = n;
        }
      }
    }
    // Make the row list circular if there's 1 or more element
    if (first != NULL) {
      current->right = first;
      first->left = current;
    }
  }

  if (!strict) {
    // If a column header's count is 0, i.e., there are no
    // intersecting rows, and DLX won't find a solution. Remove the
    // column.
    for (int col = 0; col < s->ncols; col++) {
      if (nodes[col].count == 0) {
        nodes[col].left->right = nodes[col].right;
        nodes[col].right->left = nodes[col].left;
      }
    }
  }

  free(used);
}

// Search for a solution to the exact cover problem specified in the
// cell matrix passed in by solver_init_graph.
//
// In dlx_random mode, find a random solution and store it in
// solution. Return true if the puzzle can be solved.
//
// In dlx_unique mode, check for more than one solution. Return true
// only if there is exactly one solution.
//
// The caller should provide the solution array. It will be filled
// with the row indices of the DLX matrix. If the solution set is
// smaller than the size provided, the remanining elements will be set
// to -1.
//
bool solver_run(solver *s, dlx_mode search_mode, int *solution, size_t size)
{
  assert(s);
  assert(solution);

  // solver_init_graph should be called before this function
  assert(s->nodes);

  s->mode = search_mode;
  s->solution = solution;
  s->solution_size = size;
  s->solution_count = 0;

  for (int i = 0; i < size; i++) {
    s->solution[i] = -1;
  }

  bool found = search(s, 0);
  if (s->mode == DLX_RANDOM) {
    return found;
  } else {
    return (s->solution_count == 1);
  }

  return false;
}

bool search(solver *s, int k)
{
  assert(s);
  assert(s->root);
  assert(k >= 0 && k < s->solution_size);

  if (s->root->right == s->root) {
    // If there's no more columns (constraints) left, we've found a
    // solution. This implicitly assumes that the same solution won't
    // be found twice.
    s->solution_count++;
    return (s->mode == DLX_RANDOM || (s->mode == DLX_UNIQUE && s->solution_count > 1));
  }

  // Choose a column. It's presence indicates that the solution set
  // does not yet satisfy the constraint corresponding to this
  // column. Pick the column (constraint) that has the least number of
  // rows satisfying it, to minimize the branching factor of this
  // algorithm.
  node *column = s->root->right;
  int min = column->count;
  node *c = column->right;
  while (c != s->root) {
    if (c->count < min) {
      column = c;
      min = c->count;
    }
    c = c->right;
  }

  // Cover the column. This unlinks the column from the graph, as well
  // as all rows that intersect this column. The rows aren't needed
  // because one of the rows will be part of the solution set, and
  // since only one row should satisfy the constraint, the others are
  // unnecessary.
  cover(column);

  // Store the rows in an array so that they can be ordered randomly.
  int count = column->count;
  if (count > 0) {
    // This array is typically small but since this function is called
    // recursively, it needs to be allocated here. Rather than many
    // small allocations on the heap, use a VLA.
    node *rows[count];
    int i = 0;
    node *row = column->down;
    while (row != column) {
      rows[i++] = row;
      row = row->down;
    }
    
    if (s->mode == DLX_RANDOM) {
      // Shuffle rows in random mode
      for (i = count - 1; i >= 1; i--) {
        int j = rand() % (i+1);
        row = rows[i];
        rows[i] = rows[j];
        rows[j] = row;
      }
    }

    for (i = 0; i < count; i++) {
      row = rows[i];
      s->solution[k] = row->rownum;
      
      // Remove all others rows that satisfy any of the constraints that
      // are satisifed by this row. This is done to ensure that in a
      // deeper recurse of the algorithm, no row is put in the solution
      // set that satisfies a constraint that is already satisifed here.
      c = row->right;
      while (c != row) {
        cover(c->column);
        c = c->right;
      }

      // Recursively search for a solution, with one less constraint.
      if (search(s, k+1)) {
        return true;
      }

      // Putting this row in the solution didn't work, backtrack by
      // uncovering the columns that were previously covered. Do this in
      // reverse order.
      c = row->left;
      while (c != row) {
        uncover(c->column);
        c = c->left;
      }
    }
  }

  // Since a solution could not be found with any of the rows, this
  // constraint could not be satisfied. Backtrack.
  uncover(column);
  return false;
}

void cover(node *column)
{
  // Change the column header list to point around this column
  column->left->right = column->right;
  column->right->left = column->left;
  
  // Then for each row that this column intersects, remove the row
  // from all the other columns that intersect it by changing the
  // links to point around the row.
  node *row = column->down;
  while (row != column) {
    node *n = row->right;
    while (n != row) {
      n->up->down = n->down;
      n->down->up = n->up;
      n->column->count--;
      n = n->right;
    }
    row = row->down;
  }
}

void uncover(node *column)
{
  // For each row that this column intersects, restore the row into
  // the other columns' lists. This has to be done in the oppposite
  // order from the cover operation.
  node *row = column->up;
  while (row != column) {
    node *n = row->left;
    while (n != row) {
      n->up->down = n;
      n->down->up = n;
      n->column->count++;
      n = n->left;
    }
    row = row->up;
  }

  // Restore the column into the column header list
  column->left->right = column;
  column->right->left = column;
}
