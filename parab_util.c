#include <stdio.h>         // for print
#include <stdlib.h>        // for rand
#include <cilk/cilk.h>     // for spawn and sync
#include <cilk/cilk_api.h> // for cilk workers report
#include <assert.h>        // for print
#include <pthread.h>       // for mutex locks
#include <string.h>        // for strcmp in main()
#include "parab.h"         // for prototypes and data structures




/************************
 ** JOB              ***
 ***********************/

job_type *new_job(node_type *n, int t) {
  job_type *job = malloc(sizeof(job_type));
  job->node = n;
  job->type_of_job = t;
  return job;
}

int lock(pthread_mutex_t *mutex) {
  pthread_mutex_lock(mutex);
}
int unlock(pthread_mutex_t *mutex) {
  pthread_mutex_unlock(mutex);
}

/************************
 *** NODE             ***
 ************************/

/*
 * allocate in memory space of home machine
 */

#define MAXMININIT

node_type *new_leaf(node_type *p) {
  if (p && p->depth <= 0) {
    return NULL;
  }
  node_type *node = malloc(sizeof(node_type));
  node->board = rand() % N_MACHINES;
  node->maxormin = p?opposite(p->maxormin):MAXNODE;
  node->wa = -INFTY;
  node->wb = INFTY;
  if (p) {
    node->wa = p->wa;
    node->wb = p->wb;
  }
  node->a = -INFTY;
  node->b = INFTY;
  node->lb = -INFTY;
  node->ub = INFTY;
  node->children =  NULL;
  node->n_children = 0;
  node->parent = p;
  node->best_child = NULL;
  node->path = 0;
  if (p) {
    node->depth = p->depth - 1;
  }
  return node; // return to where? return a pointer to our address space to a different machine's address space?
}

int max(int a, int b) {
  return a>b?a:b;
}
int min(int a, int b) {
  return a<b?a:b;
}


int max_of_beta_kids(node_type *node) {
  int b = -INFTY;
  int ch = 0;
  for (ch = 0; ch < node->n_children && node->children[ch]; ch++) {
    node_type *child = node->children[ch];
    if (child) {
      b = max(b, child->b);
    }
  }
  if (ch < node->n_children) {
    b = -INFTY;
  }
  // if there are unexpanded kids in a max node, then beta is infty
  // the upper bound of a max node with open children is infinity, it can
  // still be any value
  return (b == -INFTY)?INFTY:b;
}

int min_of_alpha_kids(node_type *node) {
  int a = INFTY;
  int ch = 0;
  for (ch = 0; ch < node->n_children && node->children[ch]; ch++) {
    node_type *child = node->children[ch];
    if (child) {
      a = min(a, child->a);
    } 
  }
  if (ch < node->n_children) {
    a = INFTY;
  }
  // if there are unexpanded kids in a min node, then alpha is -infty
  return (a == INFTY)?-INFTY:a;
}

int max_of_ub_kids(node_type *node) {
  int ub = -INFTY;
  int ch = 0;
  for (ch = 0; ch < node->n_children && node->children[ch]; ch++) {
    node_type *child = node->children[ch];
    if (child) {
      ub = max(ub, child->ub);
    }
  }
  if (ch < node->n_children) {
    ub = -INFTY;
  }
  // if there are unexpanded kids in a max node, then beta is infty
  // the upper bound of a max node with open children is infinity, it can
  // still be any value
  return (ub == -INFTY)?INFTY:ub;
}

int min_of_lb_kids(node_type *node) {
  int lb = INFTY;
  int ch = 0;
  for (ch = 0; ch < node->n_children && node->children[ch]; ch++) {
    node_type *child = node->children[ch];
    if (child) {
      lb = min(lb, child->lb);
    } 
  }
  if (ch < node->n_children) {
    lb = INFTY;
  }
  // if there are unexpanded kids in a min node, then alpha is -infty
  return (lb == INFTY)?-INFTY:lb;
}

void set_best_child(node_type *node) {
  if (node && node->parent) {
    if (node->parent->best_child) {
      //      printf("SET BEST CHILD from %d ", node->parent->best_child->path);    
    } else {
      //      printf("SET BEST CHILD from -- ");    
    }
    if (node->parent->maxormin == MAXNODE && 
	(!node->parent->best_child || 
	 node->a > node->parent->best_child->a)) {
      /*if my value is better than your current best child then update best child
	update beste child shpuld be coniditiaonal only if it is better. 
	this updates it to the last best child. In parallel timing may be off, and this may be wrong
      */
      node->parent->best_child = node;	  
    }
    if (node->parent->maxormin == MINNODE && 
	(!node->parent->best_child || 
	 node->b < node->parent->best_child->b)) {
      node->parent->best_child = node;	  
    }
    if (node->parent->best_child) {
      //      printf("to %d\n", node->parent->best_child->path);
    } else {
      //      printf("to --\n");
    }
  }
}

int child_number(int p) {
  return p - (10*(p/10));
}

void print_unorderedness() {
  for (int i = 0; i < TREE_DEPTH; i++) {
    if (global_unorderedness_seq_n[i]) {
      printf("seq u.o. (%d): %3.2lf\n", i, 
	     global_unorderedness_seq_x[i]/global_unorderedness_seq_n[i]);
    } else {
      printf("seq u.o. (%d) zero\n", i);
    }
  }
}

int opposite(int m) {
  return (m==MAXNODE) ? MINNODE : MAXNODE;
}

void print_tree(node_type *node, int d) {
  if (node && d >= 0) {
    printf("%d: %d %s <%d,%d>\n",
	   node->depth, node->path, ((node->maxormin==MAXNODE)?"+":"-"), node->a, node->b);
    for (int ch = 0; ch < node->n_children; ch++) {
      print_tree(node->children[ch], d-1);
    }
  }
}


/***************************
 *** MAIN                 **
 ***************************/

int main(int argc, char *argv[]) { 
  int g = -INFTY;
  if (argc != 3) {
    printf("Usage: %s {w,n,m} n-par\n", argv[0]);
    exit(1);
  }
  printf("\n\n\n\n\n");
  char *alg_choice = argv[1];
  n_par = atoi(argv[2]);
  if (n_par > TREE_WIDTH) {
    printf("It does not make sense to ask to schedule %d children at once in the job queue when nodes have only %d children to begin with\n", n_par, TREE_WIDTH);
    exit(0);
  }
  printf("Hello from ParAB with %d machine%s and %d children in par in queue\n", N_MACHINES, N_MACHINES==1?"":"s", n_par);
  for (int i = 0; i < N_MACHINES; i++) {
    top[i] = 0;
    max_q_length[i] = 0;
  }
  total_jobs = 0;

  for (int i = 0; i < TREE_DEPTH; i++) {
    global_unorderedness_seq_x[i] = TREE_WIDTH/2;
    global_unorderedness_seq_n[i] = 1;
  }

  int numWorkers = __cilkrts_get_nworkers();
  printf("CILK has %d worker threads\n", numWorkers);

  /* 
   * process algorithms
   */

  if (strcmp(alg_choice, "w") == 0) {
    printf("Wide-window Alphabeta\n");
    g = start_alphabeta(-INFTY, INFTY);
  } else if (strcmp(alg_choice, "n") == 0) {
    printf("Null-window Alphabeta\n");
    int b = 77;
    g = start_alphabeta(b-1, b);
    if (g < b) { printf("NWS fail low (ub)\n"); } else { printf("NWS fail high (lb)\n"); }
  } else if (strcmp(alg_choice, "m") == 0) {
    printf("MTD(f)\n");
    g = start_mtdf();
  } else {
    printf("ERROR: invalid algorithm choice %s\n", alg_choice);
  }

  printf("Done. value: %d\n", g);

  //  print_tree(root, min(3, TREE_DEPTH));
  print_q_stats();
  printf("Selects: %d\n", global_selects);
  printf("Leaf Evals: %d\n", global_leaf_eval);
  printf("Downward parallel aborted searches: %d\n", global_downward_aborts);
  print_unorderedness();
  return 0;
}

void create_tree(int d) {
  root = new_leaf(NULL);
  root->depth = d;
  //  mk_children(root, d-1);
}

void print_q_stats() {
  for (int i=0; i < N_MACHINES; i++) {
    printf("Max Q length [%d]\n", max_q_length[i], i);
  }
}



// simple, new leaf is initialized with a wide window
int start_alphabeta(int a, int b) {
  if (!root) {
    create_tree(TREE_DEPTH);// aha! each alphabeta always creates new tree!
  }
  root->wa = a; // store bounds in passed-down window alpha/beta
  root->wb = b;
  schedule(root, SELECT);
  start_processes(N_MACHINES);
  return root->ub >= b ? root->lb : root->ub;
  // dit moet een return value zijn buiten het window. fail soft ab
}

int start_mtdf() {
  int lb = -INFTY;
  int ub = INFTY;
  int g = 0;
  int b = INFTY;

  do {
    if (g == lb) { b = g+1; } else { b = g; }
    //    printf("MTD(%d)\n", b);
    g = start_alphabeta(b-1, b);
    if (g < b)   { ub = g;  } else { lb = g; }
  } while (lb < ub);

  return g;
}





/***************************
 *** OPEN/LIVE           ***
 ***************************/

int leaf_node(node_type *node) {
  return node && node->depth <= 0;
}
int live_node(node_type *node) {
  //  return node && node->lb < node->ub; // alpha beta???? window is live. may be open or closed
  return node && max(node->wa, node->a) < min(node->wb, node->b); // alpha beta???? window is live. may be open or closed
}
int live_node_lbub(node_type *node) {
  //  return node && node->lb < node->ub; // alpha beta???? window is live. may be open or closed
  printf("ERROR: using lb/ub\n");
  return node && node->lb < node->ub; // alpha beta???? window is live. may be open or closed
}
int dead_node(node_type *node) {  
  return node && !live_node(node);    // ab cutoff. is closed
}
int dead_node_lbub(node_type *node) {  
  printf("ERROR: using lb/ub\n");
  return node && !live_node_lbub(node);    // ab cutoff. is closed
}
void compute_bounds(node_type *node) {
  if (node && node->parent) {
    int old_a = node->a;
    int old_b = node->b;
    node->a = max(node->a, node->parent->a);
    node->b = min(node->b, node->parent->b);
    //    printf("%d COMPUTEBOUNDS <%d:%d> -> <%d:%d>\n", 
    //	   node->path, old_a, old_b, node->a, node->b);
  }
}
