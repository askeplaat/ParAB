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
#ifdef PUO
  return puo(node);
#endif
  // this assumes jobs are distributed evenly over queues
  int machine = node->board;
  int depth = node->depth;  // if too far away from leaves then seq
  return top[machine][SELECT] * 4 > N_JOBS || depth > SEQ_DEPTH;
}


double suo(int d) {
  if (global_unorderedness_seq_n[d]) {
    return global_unorderedness_seq_x[d]/global_unorderedness_seq_n[d];
  } else {
    return FALSE;
  }
}

// true if current child number is larger than sequential unorderedness.
// this prevents parallelism beyond where the sequential algorithm would venture
int puo(node_type *node) {
  return child_number(node->path) > suo(node->depth);
}
/*
int not_empty(int top, int home) {
  lock(&jobmutex[home]);  // empty
  int t = top > 0;
  unlock(&jobmutex[home]); // // empty
  return t;
}

int empty(int top, int home) {
  return !not_empty(top, home);
}
*/
int empty_jobs(int home) {

  int x = top[home][SELECT] < 1 && 
    top[home][UPDATE] < 1 &&
    top[home][BOUND_DOWN] < 1 &&
    top[home][PLAYOUT] < 1;

  return x;
}

// this should be atomic over all job queues. needs global lock, over all queues
/*
int no_more_jobs_in_system(int home) {
  lock(&global_queues_mutex);
  if (!empty_jobs(home)) {
    unlock(&global_queues_mutex);
      return FALSE;
  } else {
    for (int i = 0; i < N_MACHINES; i++) {
      if (!empty_jobs(i)) {
	unlock(&global_queues_mutex);
	return FALSE;
      }
    }
  }
  unlock(&global_queues_mutex);
  return TRUE;
}
*/

/*
int not_empty_and_live_root() {
  int home = root->board;
  lock(&jobmutex[home]);  // empty
  int e =  total_jobs > 0 && live_node(root);
  unlock(&jobmutex[home]);  // empty
  return e;
}

int all_empty_and_live_root() {
  int home = root->board;
  lock(&jobmutex[home]);  // empty
  int e =  total_jobs <= 0 && live_node(root);
  (&jobmutex[home]);  // empty
  return e;
}
*/

void start_processes(int n_proc) {
  int i;
  //  schedule(root, SELECT, -INFTY, INFTY);
  //  for (i = 0; i<n_proc; i++) {

  for (i = 0; i<N_MACHINES; i++) {
    cilk_spawn do_work_queue(i);
  }
  printf("M:%d. Before Cilk sync\n", i);
  cilk_sync;
  printf("Root is solved. jobs: %d. root: <%d:%d> [%d:%d]\n", total_jobs, root->a, root->b, root->lb, root->ub);
}

void do_work_queue(int i) {
  int workerNum = __cilkrts_get_worker_number();
  //  printf("My CILK worker number is %d. global_empty: %d\n", workerNum, global_empty_machines);
  //  printf("M:%d. global_empty: %d, total_jobs: %d\n", i, global_empty_machines, total_jobs);

  //  lock(&treemutex);

  //  printf("M:%d. global_empty: %d, total_jobs: %d\n", i, global_empty_machines, total_jobs);

  //  unlock(&treemutex);

  int safety_counter = SAFETY_COUNTER_INIT;

  while (safety_counter-- > 0 && !global_done && live_node(root)) { 
    job_type *job = NULL;
    /*


while live(root) {
  if me==rootmachine && global_empty() then { push select root }
  else job = pullme) *pull may block*; process(job) *which includes pushes of new jobs*;
}
pull(){
if empty(me) then { global_empty++; return null or condwait}
do {orgtop = top; top--; x=job(top);} while (orgtop-1!=top); return x;
}
empty()=return top==0;
push(x){
if full(me) then { return null;}
do {job(orgtop)=orgjob; orgtop=top; orgjob=job(top); top++; job(top)=x;} while (orgtop+1!=top);

so push & pull have machine-local locks
empty is an atomic word operation. axually, can be inside de push/pull critical section
global_empty also (decrement)

    */

    //    printf("global empty machines: %d\n", global_empty_machines);

    if (i == root->board && global_empty_machines >= N_MACHINES) {
      add_to_queue(new_job(root, SELECT)); 
    }

    job = pull_job(i);
     
    if (job) {
      //         lock_node(job->node);
      //      printf("M:%d tree    ", i);
          lock(&treemutex);
      process_job(job);
            unlock(&treemutex); 
      //          unlock_node(job->node);
    }     
    /*
    lock(&global_jobmutex); // check
    check_consistency_empty();
    unlock(&global_jobmutex); // check
    */
    global_done |= !live_node(root);
    if (global_done) {
      break;
    }
  } // while 

  if (safety_counter <= 0) {
    printf("M:%d ERROR: safety triggered\n", i);
  } else {
    //    printf("M:%d safety counter: %d. root is at machine: %d\n", i, safety_counter, root->board);
  }

  global_done = 1;

  printf("M:%d. Finished. Queue is empty or root is solved. jobs: %d. root: <%d:%d> \n", i, total_jobs, root->a, root->b);
}

void schedule(node_type *node, int t) {
  if (node) {
    // send to remote machine
    int home_machine = node->board;

    int was_empty = empty_jobs(home_machine);  
    add_to_queue(new_job(node, t));
    /*
    if (was_empty) {
      //      printf("Signalling machine %d for path %d type:[%d]. in wait: %d\n", home_machine, node->path, t, global_in_wait);
      pthread_cond_signal(&job_available[home_machine]);
      //   pthread_cond_signal(&global_job_available[home_machine]); //how do we know that we signal the correct machine?
    }
    */
  } 
}

// which q? one per processor
void add_to_queue(job_type *job) {
  int home_machine = job->node->board;
  int jobt = job->type_of_job;
  if (home_machine >= N_MACHINES) {
    printf("ERROR: home_machine %d too big\n", home_machine);
    exit(1);
  }
 
  if (top[home_machine][jobt] >= N_JOBS) {
    printf("M%d Top:%d ERROR: queue [%d] full\n", home_machine, top[home_machine][jobt], jobt);
    exit(1);
  }

  push_job(home_machine, job);
}


/* 
** PUSH JOB
*/

void push_job(int home_machine, job_type *job) {
  //  printf("M:%d PUSH   ", home_machine);
#ifdef LOCAL_LOCK
  lock(&jobmutex[home_machine]); // push
#else
  lock(&global_jobmutex); // push
#endif
  total_jobs++;
  if (empty_jobs(home_machine) && global_empty_machines > 0) {
    // I was empty, not anymore
    //    printf("M:%d is empty. decr global-empty %d\n", home_machine, global_empty_machines);
    global_empty_machines--;
  }
  int jobt = job->type_of_job;
  //int jobt = SELECT;
  queue[home_machine][++(top[home_machine][jobt])][jobt] = job;
  max_q_length[home_machine][jobt] = 
    max(max_q_length[home_machine][jobt], top[home_machine][jobt]);
#ifdef LOCAL_LOCK
  unlock(&jobmutex[home_machine]); // push
#else
  unlock(&global_jobmutex); // push
#endif

#undef PRINT_PUSHES
#ifdef PRINT_PUSHES
  if (seq(job->node)) {
    //        printf("ERROR: pushing job while in seq mode ");
  }
  assert(home_machine == job->node->board);

  printf("    M:%d P:%d %s TOP[%d]:%d PUSH  [%d] <%d:%d> jobs: %d\n", 
	 job->node->board, job->node->path, 
 	 job->node->maxormin==MAXNODE?"+":"-", 
	 job->node->board, top[job->node->board][jobt], job->type_of_job,
	 job->node->a, job->node->b, total_jobs);
#endif
  //  sort_queue(queue[home_machine], top[home_machine]);
  //  print_queue(queue[home_machine], top[home_machine]);
}

void check_consistency_empty() {
  int e = 0;
  for (int i = 0; i < N_MACHINES; i++) {
    e += empty_jobs(i);
  }
  if (e != global_empty_machines) {
    printf("ERROR: inconsistency empty jobs %d %d\n", global_empty_machines, e);
    exit(0);
  }
}

void check_job_consistency() {
  int j = 0;
  for (int i = 0; i < N_MACHINES; i++) {
    j += top[i][SELECT] + top[i][UPDATE] + top[i][BOUND_DOWN] + top[i][PLAYOUT];
  }
  if (total_jobs != j) {
    printf("ERROR Inconsistency total_jobs =/= j %d %d\n", total_jobs, j);
    exit(0);
  }
}

/*
** PULL JOB
*/

job_type *pull_job(int home_machine) {
  //  printf("M:%d Pull   ", home_machine);
#ifdef LOCAL_LOCK
  lock(&jobmutex[home_machine]);  // pull
#else
  lock(&global_jobmutex);  // pull
#endif
  int jobt = BOUND_DOWN;
  // first try bound_down, then try update, then try select
  while (jobt > 0) {
    if (top[home_machine][jobt] > 0) {
      total_jobs--;
      /*
      if (no_more_jobs_in_system(home_machine)) {

	pthread_cond_signal(&job_available[root->board]);
	//pthread_cond_signal(&global_job_available);
	// send signal naar root home machnien;
      }
      */
      //      assert(total_jobs >= 0);
      job_type *job = queue[home_machine][top[home_machine][jobt]--][jobt];
      if (empty_jobs(home_machine)) {
	//	printf("M:%d will be empty. incr global_empty %d\n", home_machine, global_empty_machines);
	global_empty_machines++;
      }
      //      check_job_consistency();
#undef PRINT_PULLS
#ifdef PRINT_PULLS
      printf("    M:%d P:%d %s TOP[%d]:%d PULL  [%d] <%d:%d> jobs: %d\n", 
	     job->node->board, job->node->path, 
	     job->node->maxormin==MAXNODE?"+":"-", 
	     job->node->board, top[job->node->board][jobt], job->type_of_job,
	     job->node->a, job->node->b, total_jobs);
#endif
#ifdef LOCAL_LOCK
      unlock(&jobmutex[home_machine]);  // pull
#else
      unlock(&global_jobmutex);  // pull
#endif
      return job;
    }
    jobt --;
  }
#ifdef LOCAL_LOCK
  unlock(&jobmutex[home_machine]); // pull
#else
  unlock(&global_jobmutex); // pull
#endif
  global_no_jobs[home_machine]++;
  return NULL;
}





/*
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
*/

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
  for (int i = 0; i < top[home_machine][SELECT]; i++) {
    job_type *job = queue[home_machine][i][SELECT];
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
  //  printf("Process job\n");
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

