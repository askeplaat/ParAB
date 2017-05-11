#include <stdio.h>         // for print
#include <stdlib.h>        // for rand
#include <cilk/cilk.h>     // for spawn and sync
#include <cilk/cilk_api.h> // for cilk workers report
#include <assert.h>        // for print
#include <pthread.h>       // for mutex locks
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
  node->a = -INFTY;
  node->b = INFTY;
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
  if (argc != 2) {
    printf("Usage: %s n-par\n", argv[0]);
    exit(1);
  }
  printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
  n_par = atoi(argv[1]);
  if (n_par > TREE_WIDTH) {
    printf("It does not make sense to ask to schedule %d children at once in the job queue when nodes have only %d children to begin with\n", n_par, TREE_WIDTH);
    exit(0);
  }
  printf("Hello from ParAB with %d machine%s and %d children in par in queue\n", N_MACHINES, N_MACHINES==1?"":"s", n_par);
  for (int i = 0; i < N_MACHINES; i++) {
    top[i] = 0;
    max_q_length[i] = 0;
  }

  create_tree(TREE_DEPTH);
  total_jobs = 0;
  schedule(root, SELECT);
  //  printf("Tree Created. Root: %d\n", root->board);

  start_processes(N_MACHINES);

  printf("Done\n");

  //  print_tree(root, min(3, TREE_DEPTH));
  print_q_stats();
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

/***************************
 *** OPEN/LIVE           ***
 ***************************/

int leaf_node(node_type *node) {
  return node && node->depth <= 0;
}
int live_node(node_type *node) {
  //  return node && node->lb < node->ub; // alpha beta???? window is live. may be open or closed
  return node && node->a < node->b; // alpha beta???? window is live. may be open or closed
}
int dead_node(node_type *node) {  
  return node && !live_node(node);    // ab cutoff. is closed
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


/*******************************
 **** JOB Q                  ***
 ******************************/

// throttling parallelism
// how are the job queues doing? if empty, then allow parallelism, 
// if getting full, then turn sequential
// other policies are possible: left first, deepest first (now it is
// in effect a breadth first policy. not very efficient)
int seq(int machine) {
  // this assumes jobs are distributed evenly over queues
  return top[machine] * 4 > N_JOBS;
}


int not_empty(int top) {
  pthread_mutex_lock(&jobmutex);
  int t = top > 0;
  pthread_mutex_unlock(&jobmutex);
  return t;
}
int empty(int top) {
  return !not_empty(top);
}

int not_empty_and_live_root() {
  pthread_mutex_lock(&jobmutex);
  int e =  total_jobs > 0 && live_node(root);
  pthread_mutex_unlock(&jobmutex);
  return e;
}

int all_empty_and_live_root() {
  pthread_mutex_lock(&jobmutex);
  int e =  total_jobs <= 0 && live_node(root);
  pthread_mutex_unlock(&jobmutex);
  return e;
}

void schedule(node_type *node, int t) {
  if (node/* && !seq(node->board)*/) {
    /*
here we are throttling correctness.
  seq for speculative opar (extrsa selects) is fine, but losing updates is not fine. it forgets bounds that should propagate to the root
    */
    job_type *job = new_job(node, t);
    // send to remote machine
    add_to_queue(job);
  } else {
    //    printf("schedule: NODE to schedule in job queue is NULL. Type: %d\n", 
    //	   t);
  }
}

void start_processes(int n_proc) {
  int i;
  //  schedule(root, SELECT, -INFTY, INFTY);
  //  for (i = 0; i<n_proc; i++) {

  int numWorkers = __cilkrts_get_nworkers();
  printf("CILK has %d worker threads\n", numWorkers);

  for (i = 0; i<N_MACHINES; i++) {
    cilk_spawn do_work_queue(i);
  }
  cilk_sync;
}

void do_work_queue(int i) {
  printf("Hi from machine %d  ", i);
  printf("top[%d]: %d  ", i, top[i]);

  int workerNum = __cilkrts_get_worker_number();
  printf("My CILK worker number is %d\n", workerNum);

  // wait for jobs to arrive
  while (empty(top[i])) {
    // nothing
  }
  printf("M%d starting job queue\n", i);
  //  while (not_empty(top[i])) {
  //  while (live_node(root) && total_jobs > 0) {
  while (live_node(root)) {
    //  while (not_empty_and_live_root()) {
    job_type *job = NULL;

    pthread_mutex_lock(&jobmutex);
    job = pull_job(i);
    pthread_mutex_unlock(&jobmutex);

    if (job) {
      //      pthread_mutex_lock(&treemutex);
      process_job(job);
      //      pthread_mutex_unlock(&treemutex);
    }

    //    pthread_mutex_lock(&treemutex);
    //    pthread_mutex_lock(&jobmutex);
    if (all_empty_and_live_root()) {
      //      printf("* ");
      schedule(root, SELECT);
    }
    //    pthread_mutex_unlock(&jobmutex);
    //    pthread_mutex_unlock(&treemutex);

  }
  printf("M%d Queue is empty or root is solved. jobs: %d. root: <%d:%d> \n", i, total_jobs, root->a, root->b);
}

// which q? one per processor
void add_to_queue(job_type *job) {
  int home_machine = job->node->board;
  if (home_machine >= N_MACHINES) {
    printf("ERROR: home_machine %d too big\n", home_machine);
    exit(1);
  }
  if (top[home_machine] >= N_JOBS) {
    printf("M%d Top:%d ERROR: queue full\n", home_machine, top[home_machine]);
    exit(1);
  }
  
  pthread_mutex_lock(&jobmutex);
  push_job(home_machine, job);
  pthread_mutex_unlock(&jobmutex);
}

void push_job(int home_machine, job_type *job) {
  total_jobs++;
  queue[home_machine][++(top[home_machine])] = job;
  max_q_length[home_machine] = max(max_q_length[home_machine], top[home_machine]);
#undef PRINT_PUSHES
#ifdef PRINT_PUSHES
  printf("    M%d P:%d %s TOP[%d]:%d PUSH  [%d] <%d:%d> \n", 
	 job->node->board, job->node->path, 
	 job->node->maxormin==MAXNODE?"+":"-", 
	 job->node->board, top[job->node->board], job->type_of_job,
	 job->node->a, job->node->b);
#endif
  sort_queue(queue[home_machine], top[home_machine]);
  //  print_queue(queue[home_machine], top[home_machine]);
}

job_type *pull_job(int home_machine) {
  //  printf("M%d Pull   ", home_machine);
  if (top[home_machine] <= 0) {
    //    printf("M%d PULL ERROR\n", home_machine);
    return NULL;
  }
  total_jobs--;
  job_type *job = queue[home_machine][top[home_machine]--];
#undef PRINT_PULLS
#ifdef PRINT_PULLS
  printf("    M%d P:%d %s TOP[%d]:%d PULL  [%d] <%d:%d> \n", 
	 job->node->board, job->node->path, 
	 job->node->maxormin==MAXNODE?"+":"-", 
	 job->node->board, top[job->node->board], job->type_of_job,
	 job->node->a, job->node->b);
#endif
  return job;
}

// swap the pointers to jobs in the job array
void swap_jobs(job_type *q[], int t1, int t2) {
  job_type *tmp = q[t1];
  q[t1] = q[t2];
  q[t2] = tmp;
}

// this is not a full sort
// this is a single pass that, performed on a sorted list, will 
// keep it sorted.
void sort_queue(job_type *q[], int t) {
  // last inserted job is at the top
  // percolate update to the top. that is, percolate SELECTS down
  if (!q[t]) {
    return;
  }
  int top = t;
  if (q[t]->type_of_job == SELECT) {  
    //  keep going down until the other node is a SELECT
    while (top-- > 0 && q[top] && q[top]->type_of_job != SELECT) {
      swap_jobs(q, top+1, top);
    }
  }
  // now top is either an UPDATE or an EXPAND or a SELECT next to other SELECTS
  // now sort on depth. nodes with a low value for depth are closest 
  // to the leaves, so I want the lowest depth values to be nearest the top
  while (top-- > 0 && q[top] && q[top]->node->depth < q[top+1]->node->depth) {
    swap_jobs(q, top+1, top);
  }
}

void print_queue(job_type *q[], int t){
  for (int i = 0; i <= t; i++) {
    if (q[i] && q[i]->node) {
      printf("Q[%d]: T:[%d] P:%d <%d:%d> depth:%d\n", 
	     i, q[i]->type_of_job, q[i]->node->path, 
	     q[i]->node->a, q[i]->node->b, q[i]->node->depth);
    }
  }
}

void process_job(job_type *job) {
  if (job) {
    switch (job->type_of_job) {
    case SELECT:  do_select(job->node);  break;
      //  case EXPAND:  do_expand(job->node);  break;
    case PLAYOUT: do_playout(job->node); break;
    case UPDATE:  do_update(job->node);  break;
    }
  }
}
