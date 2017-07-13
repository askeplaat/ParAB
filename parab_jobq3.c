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

int not_empty(int top, int home) {
  lock(&jobmutex[home]);
  int t = top > 0;
  unlock(&jobmutex[home]);
  return t;
}

int empty(int top, int home) {
  return !not_empty(top, home);
}

int empty_jobs(int home) {
  return top[home][SELECT] < 1 && 
    top[home][UPDATE] < 1 &&
    top[home][BOUND_DOWN] < 1 &&
    top[home][PLAYOUT] < 1;
}

int not_empty_and_live_root() {
  int home = root->board;
  lock(&jobmutex[home]);
  int e =  total_jobs > 0 && live_node(root);
  unlock(&jobmutex[home]);
  return e;
}

int all_empty_and_live_root() {
  int home = root->board;
  lock(&jobmutex[home]);
  int e =  total_jobs <= 0 && live_node(root);
  unlock(&jobmutex[home]);
  return e;
}

void schedule(node_type *node, int t) {
  if (node) {
    // send to remote machine
    add_to_queue(new_job(node, t));
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
  //  printf("Root is solved. jobs: %d. root: <%d:%d> [%d:%d]\n", total_jobs, root->a, root->b, root->lb, root->ub);
}

void do_work_queue(int i) {
  //  printf("Hi from machine %d  ", i);
  //  printf("top[%d]: %d  ", i, top[i]);

  int workerNum = __cilkrts_get_worker_number();
  //  printf("My CILK worker number is %d\n", workerNum);
  int safety_counter = SAFETY_COUNTER_INIT;

  /*
  // wait for jobs to arrive
  //  while (empty(top[i][SELECT], i) && live_node(root)) {
    // nothing
  //}
  //  printf("M%d starting job queue\n", i);
  //  while (not_empty(top[i])) {
  //  while (live_node(root) && total_jobs > 0) {

  while (live_node(root) && safety_counter-- > 0) {
    //  while (not_empty_and_live_root()) {

    lock(&(jobmutex[i]));
    while (empty_jobs(i) && live_node(root)) {
      printf("M:%d Waiting %d:%d:%d:%d. root: <%d:%d>@%d. total_jobs: %d\n", i, 
	     top[i][SELECT], top[i][PLAYOUT], 
	     top[i][UPDATE], top[i][BOUND_DOWN], 
	     root->a, root->b, root->board, total_jobs);
      pthread_cond_wait(&job_available[i], &jobmutex[i]);
    }
    job = pull_job(i);
    unlock(&(jobmutex[i]));

    if (job) {
      printf("M:%d Got %d [%d]\n", i, job->node->path, job->type_of_job);
           lock(&treemutex);
      process_job(job);
            unlock(&treemutex);
    }

    //    pthread_mutex_lock(&treemutex);
    //    pthread_mutex_lock(&jobmutex);
    */
    /*
hmm. in this way the spawning of selects for the root is blocked if the condwait is waiting on a job for this machine.
    if (all_empty_and_live_root()) { hmm. deze zou alleen door de machine met de root gedaan moeten worden. nu doen alle machines deze test. dan kunnen er wel heel veel select.roots tegelijk gescheduled worden. niet nodig.

      het is dus mogelijk dat nu nog root live is, je hier in komt, de root closed wordt, de andere machine stopt, en je dan een schedu;ed. moet deze test+schedule ook niet atomair?
uittekenen van mogelijke interleavings.
																	  condities: live_root, empty_jobs
																	  algoritme: zolang live_root, schedule selects.
																	  of eigenlijk: zolang live_root && empty_jobs, dan schedule selects. En dat is ingewikkeld in deze code, want hier wordt op twee plekken gechecked voor live_root && empty jobs. Los van elkaar, niet atomair.

Ik moet deze while loop herontwerpen
    */
  while (safety_counter-- > 0 && !global_done) { 
    job_type *job = NULL;
      
    // kan dit zonder locks?
    if (i == root->board && empty_jobs(i) && !global_done)  {
      //does it schedule one or does it keep on pushing jobs in the q?
      //      printf("root schedule: ");
      schedule(root, SELECT); // local schedule, q already locked
      if (empty_jobs(i)) {
	printf("ERROR: job queue still empty after scheduling root\n");
      }
    }
    
    lock(&jobmutex[i]);
    while (empty_jobs(i) && !global_done) {
      if (i != root->board)  {
	//	printf("M:%d Waiting\n", i);
	pthread_cond_wait(&job_available[i], &jobmutex[i]); // root closed must release block 
      }
    } 
    job = pull_job(i);
    unlock(&(jobmutex[i]));

    if (job) {
      lock_node(job->node);
      process_job(job); 
      unlock_node(job->node);
      if (i == root->board && !live_node(root)) {
	lock(&donemutex);
	global_done = TRUE;

	// root is solved. release all condition variables
	for (int i = 0; i < N_MACHINES; i++) {
	  pthread_cond_broadcast(&job_available[i]);
	}

	unlock(&donemutex);
	break;
      } 
    }
    lock(&donemutex);
    if (global_done) {
      unlock(&donemutex);
      break;
    }
    unlock(&donemutex);
    
  } // while true
      /*
      printf("* live root: %d, ab:<%d:%d>, lbub:<%d:%d>, wawb:<%d:%d>\n", 
	     live_node(root), 
	     root->a, root->b,
	     root->lb, root->ub, 
	     root->wa, root->wb);
      */

  if (safety_counter <= 0) {
    printf("M:%d ERROR: safety triggered\n", i);
  } else {
    //    printf("M:%d safety counter: %d. root is at machine: %d\n", i, safety_counter, root->board);
  }

  // root is solved. release all condition variables
  for (int i = 0; i < N_MACHINES; i++) {
    pthread_cond_broadcast(&job_available[i]);
  }

  //  printf("M%d Queue is empty or root is solved. jobs: %d. root: <%d:%d> \n", i, total_jobs, root->a, root->b);
}

// which q? one per processor
void add_to_queue(job_type *job) {
  int home_machine = job->node->board;
  int jobt = job->type_of_job;
  if (home_machine >= N_MACHINES) {
    printf("ERROR: home_machine %d too big\n", home_machine);
    exit(1);
  }

  lock(&(jobmutex[home_machine]));
  if (top[home_machine][jobt] >= N_JOBS) {
    printf("M%d Top:%d ERROR: queue [%d] full\n", home_machine, top[home_machine][jobt], jobt);
    exit(1);
  }
  
  push_job(home_machine, job);

  if (empty_jobs(home_machine)) {
    //    printf("Signalling %d for %d [%d]\n", home_machine, job->node->path, job->type_of_job);
    pthread_cond_signal(&job_available[home_machine]);
  }
  unlock(&(jobmutex[home_machine]));
}

void push_job(int home_machine, job_type *job) {
  total_jobs++;
  int jobt = job->type_of_job;
  //int jobt = SELECT;
  queue[home_machine][++(top[home_machine][jobt])][jobt] = job;
  max_q_length[home_machine][jobt] = 
    max(max_q_length[home_machine][jobt], top[home_machine][jobt]);
#undef PRINT_PUSHES
#ifdef PRINT_PUSHES
  if (seq(job->node)) {
    //        printf("ERROR: pushing job while in seq mode ");
  }
  printf("    M:%d P:%d %s TOP[%d]:%d PUSH  [%d] <%d:%d> \n", 
	 job->node->board, job->node->path, 
	 job->node->maxormin==MAXNODE?"+":"-", 
	 job->node->board, top[job->node->board][jobt], job->type_of_job,
	 job->node->a, job->node->b);
#endif
  //  sort_queue(queue[home_machine], top[home_machine]);
  //  print_queue(queue[home_machine], top[home_machine]);
}

job_type *pull_job(int home_machine) {
  //  printf("M%d Pull   ", home_machine);
  int jobt = BOUND_DOWN;
  // first try bound_down, then try update, then try select
  while (jobt > 0) {
    if (top[home_machine][jobt] > 0) {
      total_jobs--;
      job_type *job = queue[home_machine][top[home_machine][jobt]--][jobt];
#undef PRINT_PULLS
#ifdef PRINT_PULLS
      printf("    M%d P:%d %s TOP[%d]:%d PULL  [%d] <%d:%d> \n", 
	     job->node->board, job->node->path, 
	     job->node->maxormin==MAXNODE?"+":"-", 
	     job->node->board, top[job->node->board][jobt], job->type_of_job,
	     job->node->a, job->node->b);
#endif
      return job;
    }
    jobt --;
  }
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

