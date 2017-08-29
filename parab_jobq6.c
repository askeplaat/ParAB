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

// this should be atomic over all job queues. needs global lock, over all queues
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

/*
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
  (&jobmutex[home]);
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
  //  printf("My CILK worker number is %d\n", workerNum);
  int safety_counter = SAFETY_COUNTER_INIT;

  while (safety_counter-- > 0 && !global_done && live_node(root)) { 
    job_type *job = NULL;
    /*
      lock(i)
      if live_root && !globaldone then 
        if i==root->board && totaljobs==0 then push select root
        if empty(i) then condwait
        in all cases, pull job, process job
      unlock(i)
since locking is on i there is no global lock, and empty/total_jobs is not atomically shielded, not mutually exclusive.
it can happend that total_jobs is 1 while all queueus are empty(i), or total_jobs is 0 while some queue has a job. the formeer causes infinte wait since all machines fall into the condwait, while the root machine has not added a select root job.
the latter is just a surious extra job to process, which will find out the the root is already closed. nothing serious.
so we must make sure that when all queus are empty, that total_jobs truly is 0 (especially when the root is still live, since hten we need to continue searching, the root must spawn a select).
we could do a stepped search. first look at local q, if empt, then ask others if they are empty

Does global_in_wait allow a lock-free scheme?
    */
    lock(&donemutex);
    if (global_done) {
	break;
    }
    unlock(&donemutex);      

#ifdef LOCAL_Q
    lock(&jobmutex[i]); 
#else
    lock(&global_jobmutex); 
#endif
    //    printf("M:%d locked. jobs: %d\n", i, total_jobs);
    //    print_queues();

    if (live_node(root) && !global_done) {
      //      if (i == root->board && global_in_wait == N_MACHINES-1 && empty_jobs(i)  /*total_jobs <= 0*/)  {
      // if I am the root machine and all other machines are waiting for jobs and I am empty then insert extra root job into the system
      if (i == root->board && global_in_wait == N_MACHINES-1 && no_more_jobs_in_system(i)  /*total_jobs <= 0*/)  {
	assert(empty_jobs(i));
	assert(no_more_jobs_in_system(i));
	/*
	  vraag: waarom? waarom blijft 1 op empty? moet dat geen all_empty zijn in plaats van 
empty?
dus pas de root gaan schedulen als er geen jobs meer in eht hele systeem zijn?
total_jobs == 0 en zo?
	*/
	//     	printf("M:%d. PUSH root select <%d,%d> \n", i, root->a, root->b);
	// no locks. shcedule does locking of push, add_to_queue does no locking of push
	add_to_queue(new_job(root, SELECT)); 
	//addtoqueue forgets to signal. only schedule has the signal. addtoqueue does not. Therefore other machiens that are waiting for waork in a condwait will not be woken up
	pthread_cond_signal(&job_available[i]);
	assert(!no_more_jobs_in_system(i));
	assert(!empty_jobs(i));
      }
      //      assert(!no_more_jobs_in_system(i));

      // ok now use global_in_wait as the basis for everything

      //      if (i != root->board) { 
#define CONDWAIT
#ifdef CONDWAIT
      //      while (empty_jobs(i) && !global_done && live_node(root) && (total_jobs > 0 || i!=root->board)) {
      //root machine mag toch nooit in wait vallen? 
      // root machine queue mag nooit leeg zijn.
      //      assert(i!=root->board || !empty_jobs(i) || !no_more_jobs_in_system(i));
      //      no_more_jobs_in_system klotp niet, die loopt achter. als je naar in_wait kijkt heb je een scherper beeld, ook de gegene die verwerkt worden tel je dan mee
      //      nee klopt niet. root mag wel leeg zijn, als er elders maar jobs zijn
      // als alle anderen in wait hangen, moet root machine een job hebben
      // ofwel niet iedereen hangt in een wait, ofwel root dood
      assert(global_in_wait < N_MACHINES || !live_node(root));
      assert(global_in_wait < N_MACHINES || !empty_jobs(i) || !live_node(root));
      while (i!=root->board && empty_jobs(i) && !global_done && live_node(root) && global_in_wait < N_MACHINES-1) { 
	//	if (i!=root->board) {
	global_in_wait++; // is this a count of threads or a count of nodes? threads
	  //}
	//       	printf("M:%d Waiting (root@%d) jobs:%d. in wait: %d\n", i, root->board, total_jobs, global_in_wait);
	assert(live_node(root));
	//	assert(!no_more_jobs_in_system(i));
	assert(global_in_wait < N_MACHINES);
	//moet de job_available ook globaal?
#ifdef LOCAL_Q
	pthread_cond_wait(&job_available[i], &jobmutex[i]); // root closed must release block 
#else
	pthread_cond_wait(&job_available[i], &global_jobmutex); // root closed must release block 
#endif
	//	pthread_cond_wait(&job_available[i], &jobmutex[i]); // root closed must release block 
	//	if (i!=root->board) {
	  global_in_wait--;
	  //	}
      }
#endif
	//      }   
    
      job = pull_job(i);
    }
    //    printf("M:%d unlock. job: %d type: [%d] <%d,%d> totaljobs: %d. done: %d. liveroot: %d\n", i, job, job?job->type_of_job:0, root->a, root->b, total_jobs, global_done, live_node(root));
#ifdef LOCAL_Q
    unlock(&jobmutex[i]);
#else
    unlock(&global_jobmutex);
#endif
    
    if (job) {
      //      printf("M:%d lock tree\n", i);
      //            lock_node(job->node);
      lock(&treemutex);
      process_job(job);
      unlock(&treemutex); 
	//    unlock_node(job->node);
      //      printf("M:%d unlock tree\n", i);
    }     
    
    //    unlock(&jobmutex[i]);

    lock(&donemutex);
    global_done |= !live_node(root);
    //    global_done |= total_jobs <= 0;
    if (global_done) {
      unlock(&donemutex);      
      break;
    }
    unlock(&donemutex);      

  } // while 

  if (safety_counter <= 0) {
    printf("M:%d ERROR: safety triggered\n", i);
  } else {
    //    printf("M:%d safety counter: %d. root is at machine: %d\n", i, safety_counter, root->board);
  }

  //  lock(&donemutex);
  global_done = 1;
  //  unlock(&donemutex);

  // root is solved. release all condition variables
  printf("M:%d. Finished. Broadcast all threads release cond wait. done: %d. liveroot: %d\n", i, global_done, live_node(root));
  for (int i = 0; i < N_MACHINES; i++) {
    pthread_cond_broadcast(&job_available[i]);
    //    pthread_cond_broadcast(&global_job_available);
  }

  printf("M:%d. Finished. Queue is empty or root is solved. jobs: %d. root: <%d:%d> \n", i, total_jobs, root->a, root->b);
}

void schedule(node_type *node, int t) {
  if (node) {
    // send to remote machine
    int home_machine = node->board;
    //    printf("LOCK machine %d (addtoq) p:%d\n", home_machine, node->path);

#ifdef LOCAL_Q
    lock(&(jobmutex[home_machine]));
#else
    lock(&(global_jobmutex));
#endif
    int was_empty = empty_jobs(home_machine);  
    add_to_queue(new_job(node, t));
#ifdef LOCAL_Q
    unlock(&(jobmutex[home_machine]));  //  printf("M:%d pushed %d [%d.%d.%d.%d]\n", home_machine, job->node->path,  top[home_machine][SELECT],  top[home_machine][UPDATE],  top[home_machine][PLAYOUT],  top[home_machine][BOUND_DOWN]);
#else
    unlock(&(global_jobmutex));  //  printf("M:%d pushed %d [%d.%d.%d.%d]\n", home_machine, job->node->path,  top[home_machine][SELECT],  top[home_machine][UPDATE],  top[home_machine][PLAYOUT],  top[home_machine][BOUND_DOWN]);
#endif

    if (was_empty) {
      //      printf("Signalling machine %d for path %d type:[%d]. in wait: %d\n", home_machine, node->path, t, global_in_wait);
      pthread_cond_signal(&job_available[home_machine]);
      //   pthread_cond_signal(&global_job_available[home_machine]); //how do we know that we signal the correct machine?
    }
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
  //  lock(&total_jobs_mutex);
  total_jobs++;
  /*
hmm. this is not enough. making increment atomic is too little. as if they were not atomic.
I must make sure totaljobs reflects the true total jobs number in the system, 
the total of the jobs in all job queues.
make total_jobs() a function? counting all machine queues on demand?
Maar zelfs die moet dan alle tops van alle machines locken, zodat ze niet veranderen, 
en het gebruik hiervan moet atomair zijn met empty(i), want dat moet consistent zijn.
  */
  //  unlock(&total_jobs_mutex);
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
  int jobt = BOUND_DOWN;
  // first try bound_down, then try update, then try select
  while (jobt > 0) {
    if (top[home_machine][jobt] > 0) {
      //      lock(&total_jobs_mutex);
      total_jobs--;
      //      unlock(&total_jobs_mutex);
      //      if (total_jobs <= 0) {
      if (no_more_jobs_in_system(home_machine)) {
	//	printf("Signalling root machnine since totaljobs is zero\n");
	pthread_cond_signal(&job_available[root->board]);
	//pthread_cond_signal(&global_job_available);
	// send signal naar root home machnien;
      }
      //      assert(total_jobs >= 0);
      job_type *job = queue[home_machine][top[home_machine][jobt]--][jobt];
      //      check_job_consistency();
#undef PRINT_PULLS
#ifdef PRINT_PULLS
      printf("    M:%d P:%d %s TOP[%d]:%d PULL  [%d] <%d:%d> jobs: %d\n", 
	     job->node->board, job->node->path, 
	     job->node->maxormin==MAXNODE?"+":"-", 
	     job->node->board, top[job->node->board][jobt], job->type_of_job,
	     job->node->a, job->node->b, total_jobs);
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

