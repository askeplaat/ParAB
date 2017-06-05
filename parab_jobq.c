#include <stdio.h>         // for print
#include <stdlib.h>        // for rand
#include <cilk/cilk.h>     // for spawn and sync
#include <cilk/cilk_api.h> // for cilk workers report
#include <assert.h>        // for print
#include <pthread.h>       // for mutex locks
#include <string.h>        // for strcmp in main()
#include "parab.h"         // for prototypes and data structures


/*******************************
 **** JOB Q                  ***
 ******************************/

// throttling parallelism
// how are the job queues doing? if empty, then allow parallelism, 
// if getting full, then turn sequential
// other policies are possible: left first, deepest first (now it is
// in effect a breadth first policy. not very efficient)
// par should be near leaves. not near root
int seq(node_type *node) {
  // this assumes jobs are distributed evenly over queues
  int machine = node->board;
  int depth = node->depth;  // if too far away from leaves then seq
  return top[machine] * 4 > N_JOBS || depth > SEQ_DEPTH;
}


int not_empty(int top) {
  lock(&jobmutex);
  int t = top > 0;
  unlock(&jobmutex);
  return t;
}
int empty(int top) {
  return !not_empty(top);
}

int not_empty_and_live_root() {
  lock(&jobmutex);
  int e =  total_jobs > 0 && live_node(root);
  unlock(&jobmutex);
  return e;
}

int all_empty_and_live_root() {
  lock(&jobmutex);
  int e =  total_jobs <= 0 && live_node(root);
  unlock(&jobmutex);
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

  for (i = 0; i<N_MACHINES; i++) {
    cilk_spawn do_work_queue(i);
  }
  cilk_sync;
  printf("Root is solved. jobs: %d. root: <%d:%d> [%d:%d]\n", total_jobs, root->a, root->b, root->lb, root->ub);
}

void do_work_queue(int i) {
  //  printf("Hi from machine %d  ", i);
  //  printf("top[%d]: %d  ", i, top[i]);

  int workerNum = __cilkrts_get_worker_number();
  //  printf("My CILK worker number is %d\n", workerNum);

  // wait for jobs to arrive
  while (empty(top[i])) {
    // nothing
  }
  //  printf("M%d starting job queue\n", i);
  //  while (not_empty(top[i])) {
  //  while (live_node(root) && total_jobs > 0) {
  while (live_node(root)) {
    //  while (not_empty_and_live_root()) {
    job_type *job = NULL;

    lock(&jobmutex);
    job = pull_job(i);
    unlock(&jobmutex);

    if (job) {
      //      pthread_mutex_lock(&treemutex);
      process_job(job);
      //      pthread_mutex_unlock(&treemutex);
    }

    //    pthread_mutex_lock(&treemutex);
    //    pthread_mutex_lock(&jobmutex);
    if (all_empty_and_live_root()) {
      /*
      printf("* live root: %d, ab:<%d:%d>, lbub:<%d:%d>, wawb:<%d:%d>\n", 
	     live_node(root), 
	     root->a, root->b,
	     root->lb, root->ub, 
	     root->wa, root->wb);
      */
      schedule(root, SELECT);
    }
    //    pthread_mutex_unlock(&jobmutex);
    //    pthread_mutex_unlock(&treemutex);

  }
  //  printf("M%d Queue is empty or root is solved. jobs: %d. root: <%d:%d> \n", i, total_jobs, root->a, root->b);
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
  
  lock(&jobmutex);
  push_job(home_machine, job);
  unlock(&jobmutex);
}

void push_job(int home_machine, job_type *job) {
  total_jobs++;
  queue[home_machine][++(top[home_machine])] = job;
  max_q_length[home_machine] = max(max_q_length[home_machine], top[home_machine]);
#undef PRINT_PUSHES
#ifdef PRINT_PUSHES
  if (seq(job->node)) {
    //        printf("ERROR: pushing job while in seq mode ");
  }
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
  return;
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

// there is a new bound. update the selects in the job queue 
int update_selects_with_bound(node_type *node) {
  return TRUE; // since in the shared mem version all updates to bounds
  // are already done in downwardupdartechildren: as soon as you update
  // the bounds in the child nodes, since the job queue 
  // has pointers to the nodes, all entries in the 
  // job queue are updated automatically

  int home_machine = node->board;
  int continue_update = 0;
  // find all the entries for this node in the job queue
  for (int i = 0; i < top[home_machine]; i++) {
    job_type *job = queue[home_machine][i];
    if (job->node == node && job->type_of_job == SELECT) {
      //      since node == node I do not really have to update the bounds, they are already updated....
      continue_update |= job->node->a < node->a;
      job->node->a = max(job->node->a, node->a);
      continue_update |= job->node->b > node->b;
      job->node->b = min(job->node->b, node->b);
    }
  }
  return (continue_update);
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
  if (job && job->node && live_node(job->node)) {
    switch (job->type_of_job) {
    case SELECT:      do_select(job->node);  break;
    case PLAYOUT:     do_playout(job->node); break;
    case UPDATE:      do_update(job->node);  break;
    case BOUND_DOWN:  do_bound_down(job->node);  break;
    otherwise: printf("ERROR: invalid job  type in q\n"); exit(0); break;
    }
  }
}

