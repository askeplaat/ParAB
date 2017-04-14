#include <stdio.h>
#include <stdlib.h>
#include <cilk/cilk.h>
#include "parab.h"

#define N_JOBS 100  // 100 jobs in job queue
#define N_MACHINES 16
#define TREE_WIDTH 2
#define TREE_DEPTH 3
#define INFTY  99999

#define SELECT 1
#define EXPAND 2
#define PLAYOUT 3
#define UPDATE 4

#define MAXNODE 1
#define MINNODE 2

/* 
 * parab.c
 *
 * parallel alpha beta based on TDS and MCTS ideas, roll out alphabeta, see Bojun Huang AAAI 2015
 * Pruning Game Tree by Rollouts
 * http://www.aaai.org/ocs/index.php/AAAI/AAAI15/paper/view/9828/9354
 *
 * Aske Plaat 2017
 * 
 */

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

node_type *new_leaf(node_type *p, int alpha, int beta) {
  node_type *n = malloc(sizeof(node_type));
  n->board = rand() % N_MACHINES;
  n->lb = -INFTY;
  n->ub =  INFTY;
  n->alpha = alpha; // for SELECT DOWN
  n->beta  = beta; // for SELECT DOWN
  n->child_lb = -INFTY;  // for UPDATE UP
  n->child_ub =  INFTY;  // for UPDATE UP
  n->children =  NULL;
  n->n_children = 0;
  n->parent = p;
  n->maxormin = p?opposite(p->maxormin):MAXNODE;
  if (p) {
    n->depth = p->depth - 1;
  }
  return n; // return to where? return a pointer to our address space to a different machine's address space?
}

int opposite(int m) {
  return (m==MAXNODE) ? MINNODE : MAXNODE;
}

int max(int a, int b) {
  return a>b?a:b;
}
int min(int a, int b) {
  return a<b?a:b;
}


void print_tree(node_type *node, int d) {
  if (node && d > 0) {
    printf("%d: %d %s <%d,%d>:(%d,%d)\n",
	   node->depth, node->board, node->maxormin?"+":"-", node->alpha, node->beta, node->lb, node->ub);
    for (int ch = 0; ch < node->n_children;ch++) {
      print_tree(node->children[ch], d-1);
    }
  }
}


node_type *root;

/***************************
 *** MAIN                 **
 ***************************/

int main(int argc, char *argv[]) { 
  int n_proc = atoi(argv[1]);
  printf("Hello from ParAB with %d machines\n", n_proc);
  create_tree(TREE_DEPTH);
  schedule(root, SELECT, -INFTY, INFTY);
  printf("Tree Created\n");
  start_processes(n_proc);
  return 0;
}

void create_tree(int d) {
  root = new_leaf(NULL, -INFTY, INFTY);
  //  mk_children(root, d-1);
}

/*
void mk_children(node_type *n, int d) {
  int i;
  n->n_children = TREE_WIDTH;
  for (i = 0; i < TREE_WIDTH; i++) {
    n->children[i] = new_leaf(n );
    if (d>0) {
      mk_children(n->children[i], d-1);
    }
  }
}
*/

/***************************
 *** OPEN/LIVE           ***
 ***************************/

int leaf_node(node_type *node) {
  return node && node->depth <= 0;
}
int open_node(node_type *node) {
  return node && node->lb == -INFTY && node->ub == INFTY; // not expanded, untouched. is live
}
int closed_node(node_type *node) {
  return node && !open_node(node);  // touched, may be dead or live (AB cutoff or not)
}
int live_node(node_type *node) {
  return node && node->lb < node->ub; // alpha beta window is live. may be open or closed
}
int dead_node(node_type *node) {  
  return node && !live_node(node);    // ab cutoff. is closed
}

/*******************************
 **** JOB Q                  ***
 ******************************/

job_type *queue[N_MACHINES][N_JOBS];
int top[N_MACHINES];


void start_processes(int n_proc) {
  int i;
  schedule(root, SELECT);
  for (i = 0; i<n_proc; i++) {
    top[i] = 0;
    cilk_spawn do_work_queue(i);
  }
  cilk_sync;
}

void do_work_queue(int i) {
  printf("Hi from machine %d\n", i);

  while (not_empty(top[i])) {
    process_job(pull_job(i));
  }
}

int not_empty(int top) {
  return (top) > 0;
}

// which q? one per processor
void add_to_queue(job_type *job, int alpha, int beta) {
  int home_machine = job->node->board;
  if (top[home_machine] >= N_JOBS) {
    printf("ERROR: queue full\n");
  }

  /* 
   * two types of jobs have bound propagation actions
   * that move down or up in the tree
   * we must use tricks to pass the bound values
   * to the operations, since we can only access a node's values
   * at the home machine, we cannot access other nodes at other
   * machines, we only can use local accesses
   * This is TDS: work follows data
   */
  if (job->type_of_job == SELECT) {
    job->node->alpha = alpha;
    job->node->beta  = beta;
  }
  if (job->type_of_job == EXPAND) {
    job->node->alpha = alpha;
    job->node->beta  = beta;
  }
  if (job->type_of_job == UPDATE) {
    job->node->child_lb = alpha;
    job->node->child_ub  = beta;
  }
  
  push_job(home_machine, job);
}


void push_job(int home_machine, job_type *job) {
  queue[home_machine][++top[home_machine]] = job;
}

job_type *pull_job(int home_machine) {
  job_type *job =  queue[home_machine][top[home_machine]--];
  return job;
}

void process_job(job_type *job) {
  switch (job->type_of_job) {
  case SELECT:  do_select(job->node);  break;
  case EXPAND:  do_expand(job->node);  break;
  case PLAYOUT: do_playout(job->node); break;
  case UPDATE:  do_update(job->node);  break;
  }
}

void schedule(node_type *node, int t, int alpha, int beta) {
  if (node) {
    // copy a,b
    job_type *job = new_job(node, t);

    // send to remote machine
    add_to_queue(job, alpha, beta);
  } else {
    printf("NODE is NULL\n");
  }
}


/******************************
 *** SELECT                 ***
 ******************************/

// traverse to the left most deepest open node or leaf node
// but only one node at a time, since each node has its own home machine
void do_select(node_type *node) {
  if (node == root && dead_node(node)) {
    printf("root is solved\n");
  }

  /* 
   * alpha beta update, top down 
   */
  
  if (node->maxormin == MAXNODE) {
    node->alpha = max(node->alpha, node->lb);
  }
  if (node->maxormin == MINNODE) {
    node->beta = min(node->beta, node->ub);
  }
  // alphabeta cutoff
  if (node->alpha >= node->beta) {
    return;
  }

  if (leaf_node(node)) {
    schedule(node, PLAYOUT, node->alpha, node->beta);
  }

  if (dead_node(node)) {
    schedule(next_brother(node), SELECT, node->alpha, node->beta);
  }

  if (live_node(node)) {
    schedule(first_child(node), SELECT, node->alpha, node->beta);
  }

  if (open_node(node)) {
    schedule(node, EXPAND, node->alpha, node->beta);
  }

  /*
			  node:
			  OPEN (internal leaf): EXPAND (add kids to internal tree)
			  CLOSED/LIVE (internal/inner): SELECT (traverse down) (maar kan ook update zijn)
			  CLOSED/LEAF: PLAYOUT; UPDATE bounds of parent (maar kan ook select zijn)
			  dus een job moet een richting hebben, of een next action
			  CLOSED/DEAD: select next brother or update up
						  
  ab, are updated going down. are tightened by lbub of nodes. a=max(a,lb); b=min(b,ub)
lbub are updated going up, are the max and min of their children (max of their kids if max node, and vv)
* select: upd ab; if node.leaf schedule(Playout node); if node.dead schedule(select nextbrother node); if node.live schedule(select firstchild node); if node.open schedule expand node
* playout: node.eval; schedule update node
* expand: generate open kids & schedule on queues as select (to do upd ab, w/ lbub inf) & then expand
* update: upd lbub; if changed then schedule update parent else schedule select node

expand creates the parallelism. the subsequent selects traverse through the different work queues

						  */
}


/********************************
 *** EXPAND                   ***
 ********************************/

void do_expand(node_type *node) {
  int ch = 0;
  node->n_children = TREE_WIDTH;
  for (ch = 0; ch < node->n_children; ch++) {
    node->children[i] = new_leaf(node, node->alpha, node->beta);

Hmm. Dit is apart. Nieuwe nodes worden op hun home machine gemaakt.
In de TT; en ook als job in de job queue.
dus new_leaf is een RPC?
En wat is de betekenis van de pointer die new_leaf opleverd als de nieuwe leaf
	    op een andere machine zit? In SHM is dat ok, maar later in Distr Mem
	    Is de pointer betekenisloos of misleidend.

	   OK. Laten we voor SHM en threads het maar even zo laten dan.

    if (d>0) {
      mk_children(n->children[i], d-1);
    }
    schedule(node->children[i], SELECT, );
  }
}


/******************************
 *** PLAYOUT                ***
 ******************************/

// just std ab evaluation. no mcts playout
void do_playout(node_type *node) {
  node->lb = node->ub = evaluate(node);
  schedule(node, UPDATE, node->lb, node->ub);
}

int evaluate(node_type *node) {
  return rand();
}

/*****************************
 *** UPDATE                ***
 *****************************/

// backup through the tree
void do_update(node_type *node) {
  if (live_node(node)) {
    int continue_updating = 1;
    continue_updating = 0;

    if (node->maxormin == MAXNODE) {
      if (node->ub < node->child_ub) {
	node->ub = node->child_ub;
	continue_updating = 1;
      }      
      if (node->lb < node->child_lb) {
	node->lb = node->child_lb;
	continue_updating = 1;
      }
    }
    if (node->maxormin == MINNODE) {
      if (node->ub > node->child_ub) {
	node->ub = node->child_ub;
	continue_updating = 1;
      }      
      if (node->lb > node->child_lb) {
	node->lb = node->child_lb;
	continue_updating = 1;
      }
    }
    if (continue_updating) {
      //Dit kan dus niet, dez emoet op de remote machine gescheduled worden.
      schedule(node->parent, UPDATE, node->lb, node->ub);
      //Alles moet in stapjes per node. node voor node, alles gescheduled up de home machine
      //r = do_update(node->parent);
    }
  }
  return;
}

void store_node(node_type *node) {
}

void update_bounds_down(node_type *node, int a, int b) {
  int continue_updating = 0;
  if (live_node(node)) {
    if (node->maxormin == MAXNODE) {
      // lb: take the max of lb, a
      // if alpha is tighter (greater) then update
      if (node->lb < a) {
	node->lb = a;
	continue_updating = 1;

klopt dit?
in downward update moeten toch alleen alpha en beta geupdate worden?
lb ub updates vinden toch alleen upward plaats?
      }
    }
    if (node->maxormin == MINNODE) {
      // update if b is tighter (less)
      if (node->ub > b) {
	node->ub = b;
	continue_updating = 1;
      }
    }
  }
  return;
}


// traverse down tree and propagate bounds down
// input: two bounds, a and b
// output: fixed/reconciled lb/ub of child nodes
void propagate_bounds_downward(node_type *node) {
  if (live_node(node) && !open_node) { // only traverse touched nodes. Do not start touching 
    // new nodes, expanding them inadvertently
    int ch = 0;
    int continue_updating = 1;
    int a = node->lb;
    int b = node->ub;
    while (continue_updating) {
      continue_updating = 0;
      for (ch = 0; ch < node->n_children; ch++) {
	node_type child = node->children[ch];
	continue_updating |= update_bounds_down(child, a, b);
	propagate_bounds_downward(child);
      }
    }
  }
}
      
