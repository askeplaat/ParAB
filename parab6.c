#include <stdio.h>
#include <stdlib.h>
#include <cilk/cilk.h>
#include <assert.h>
#include "parab.h"

#define N_JOBS 20  // 100 jobs in job queue
#define N_MACHINES 1
#define TREE_WIDTH 2
#define TREE_DEPTH 2
#define INFTY  99999

#define SELECT 1
#define PLAYOUT 3
#define UPDATE 4

#define MAXNODE 1
#define MINNODE 2

/* 
 * parab.c
 *
 * parallel alpha beta based on TDS and MCTS ideas, 
 * roll out alphabeta, see Bojun Huang AAAI 2015
 * Pruning Game Tree by Rollouts
 * http://www.aaai.org/ocs/index.php/AAAI/AAAI15/paper/view/9828/9354
 *
 * Aske Plaat 2017
 * 
 * parab4.c
 * introduces next_brother pointer in node
 * needed since we generate one at a time, then process (search), and 
 * then generate the next brother, which thjen is searched with the new bound.
 * note that this is a very sequential way of looking at alphabeta
 * 
 * parab5.c going back to separate lb and ub
 *
 * SELECT: node.a = parent.a; node.b = parent.b; update a=max(a, lb); b=min(b, ub)
 * UPDATE: MAX: node.lb=max(node.lb, child.lb); MAX && CLOSED: node.ub=max(all-children.ub); MIN: node.ub=min(node.ub, child.ub); MIN && CLOSED: node.lb=min(all-children.lb); if some values changed, UPDATE node.parent. 
 * LIVE: a<b
 * DEAD: not LIVE (alphabeta cutoff)
 * TOUCHED: some children of node are expanded AND (lb > -INF || ub < INF)
 * CLOSED: all children of node are expanded AND (lb > -INF || ub < INF)
 * OPEN: zero children are expanded and have meaningful bounds
 * node == not LEAF && OPEN: expand one child, mark it OPEN
 * node == not LEAF && TOUCHED: expand one child, mark it OPEN
 * node == not LEAF && CLOSED && LIVE: SELECT left-most child 
 * node == LEAF && LIVE && OPEN: evaluate, making this node CLOSED
 * node == DEAD: select first LIVE && OPEN/TOUCHED/CLOSED brother of this node
 * node == CLOSED: UPDATE node.parent
 * klopt dit? alle gevallen gehad? gaat de select na de update goed, neemt die de
 * ub/lb en a/b currect over?
 * 
 * Doet OPEN/TOUCHED/CLOSED er toe? Only LEAF/INNER en LIVE/DEAD?
 * SELECT: compute ab/b and select left-most live child. if not exist then EXPAND. if leaf then evalute and UPDATE
 * UPDATE: update parents lb/ub until no change, then SELECT (root/or node does not matter)
 * if node==LIVE/OPEN   then SELECT(node) -> push leftmost LIVE/OPEN child
 * if node==DEAD/CLOSED then UPDATE(node) -> push parent
 * EVALUATE transforms OPEN to CLOSED
 * UPDATE transforms CLOSED to OPEN (if no changes to bounds)
 * is CLOSED: DEAD en OPEN: LIVE?
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

#define MAXMININIT

node_type *new_leaf(node_type *p) {
  if (p && p->depth <= 0) {
    return NULL;
  }
  node_type *n = malloc(sizeof(node_type));
  n->board = rand() % N_MACHINES;
  n->maxormin = p?opposite(p->maxormin):MAXNODE;
  n->a = -INFTY;
  n->b = INFTY;
  n->children =  NULL;
  n->n_children = 0;
  n->parent = p;
  n->path = 0;
  if (p) {
    n->depth = p->depth - 1;
  }
  return n; // return to where? return a pointer to our address space to a different machine's address space?
}

int max(int a, int b) {
  return a>b?a:b;
}
int min(int a, int b) {
  return a<b?a:b;
}


int max_of_beta_kids(node_type *node) {
  int b = -INFTY;
  for (int ch = 0; ch < TREE_WIDTH && node->children[ch]; ch++) {
    node_type *child = node->children[ch];
    if (child) {
      b = max(b, child->b);
    }
  }
  // if there are unexpanded kids in a max node, then beta is infty
  return (b == -INFTY)?INFTY:b;
}

int min_of_alpha_kids(node_type *node) {
  int a = INFTY;
  for (int ch = 0; ch < TREE_WIDTH && node->children[ch]; ch++) {
    node_type *child = node->children[ch];
    if (child) {
      a = min(a, child->a);
    }
  }
  // if there are unexpanded kids in a min node, then alpha is -infty
  return (a == INFTY)?-INFTY:a;
}


node_type *first_child(node_type *node) {
  if (node && node->children) {
    return node->children[0];
  } else {
    return NULL;
  }
}



int opposite(int m) {
  return (m==MAXNODE) ? MINNODE : MAXNODE;
}


void print_tree(node_type *node, int d) {
  if (node && d >= 0) {
    printf("%d: %d %s <%d,%d>\n",
	   node->depth, node->board, ((node->maxormin==MAXNODE)?"+":"-"), node->a, node->b);
    for (int ch = 0; ch < node->n_children; ch++) {
      print_tree(node->children[ch], d-1);
    }
  }
}


node_type *root;
job_type *queue[N_MACHINES][N_JOBS];
int top[N_MACHINES];



/***************************
 *** MAIN                 **
 ***************************/

int main(int argc, char *argv[]) { 
  if (argc != 2) {
    printf("Usage: ./parab n-proc\n");
    exit(1);
  }
  int n_proc = atoi(argv[1]);
  printf("Hello from ParAB with %d machine%s\n", n_proc, n_proc==1?"":"s");
  for (int i = 0; i < N_MACHINES; i++) {
    top[i] = 0;
  }
  create_tree(TREE_DEPTH);
  schedule(root, SELECT);
  printf("Tree Created. Root: %d\n", root->board);
  start_processes(n_proc);
  print_tree(root, 3);
  return 0;
}

void create_tree(int d) {
  root = new_leaf(NULL);
  root->depth = d;
  //  mk_children(root, d-1);
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
  node->a = max(node->a, node->parent->a);
  node->b = min(node->b, node->parent->b);
}


/*******************************
 **** JOB Q                  ***
 ******************************/

void start_processes(int n_proc) {
  int i;
  //  schedule(root, SELECT, -INFTY, INFTY);
  //  for (i = 0; i<n_proc; i++) {
  for (i = 0; i<N_MACHINES; i++) {
    cilk_spawn do_work_queue(i);
  }
  cilk_sync;
}

void do_work_queue(int i) {
  //  printf("Hi from machine %d\n", i);
  //  printf("top[%d]: %d\n", i, top[i]);

  while (not_empty(top[i])) {
    process_job(pull_job(i));
    if (empty(top[i]) && live_node(root)) {
      schedule(root, SELECT);
    }
  }
  printf("Queue is empty or root is solved\n");
}

int not_empty(int top) {
  return (top) > 0;
}
int empty(int top) {
  return !not_empty(top);
}


void schedule(node_type *node, int t) {
  if (node) {
    // copy a,b
    job_type *job = new_job(node, t);

    // send to remote machine
    add_to_queue(job);
  } else {
    printf("schedule: NODE to schedule in job queue is NULL. Type: %d\n", t);
  }
}

// which q? one per processor
void add_to_queue(job_type *job) {
  int home_machine = job->node->board;
  if (home_machine >= N_MACHINES) {
    printf("ERROR: home_machine %d too big\n", home_machine);
    exit(1);
  }
  if (top[home_machine] >= N_JOBS) {
    printf("ERROR: queue full\n");
    exit(1);
  }
  
  push_job(home_machine, job);
}


void push_job(int home_machine, job_type *job) {
  queue[home_machine][++(top[home_machine])] = job;
  printf("    %d: PUSH  [%d] <%d:%d> \n", 
	 job->node->path, job->type_of_job,
	 job->node->a, job->node->b);
}

job_type *pull_job(int home_machine) {
  job_type *job =  queue[home_machine][top[home_machine]--];
  return job;
}

void process_job(job_type *job) {
  switch (job->type_of_job) {
  case SELECT:  do_select(job->node);  break;
    //  case EXPAND:  do_expand(job->node);  break;
  case PLAYOUT: do_playout(job->node); break;
  case UPDATE:  do_update(job->node);  break;
  }
}


/******************************
 *** SELECT                 ***
 ******************************/

// traverse to the left most deepest open node or leaf node
// but only one node at a time, since each node has its own home machine
// precondition: my bounds 
void do_select(node_type *node) {

  if (node == root && dead_node(node)) {
    printf("root is solved: %d:%d\n", root->a, root->b);
    return;
  }

  /* 
   * alpha beta update, top down 
   */
  
  compute_bounds(node);
  
  printf("%d: %s SELECT d:%d  ---   <%d:%d>   ", 
	 node->path, node->maxormin==MAXNODE?"+":"-",
	 node->depth,
	 node->a, node->b);

  if (leaf_node(node) && live_node(node)) { // depth == 0; frontier, do playout/eval
    printf("PLAYOUT\n");
    schedule(node, PLAYOUT);
  } else if (dead_node(node) && root != node) { // cutoff: alpha==beta
    printf("DEAD: UPDATE ERROR: impossible: dead nodes should not be selected\n", 
	   node->depth, node->path);
    schedule(node, UPDATE);
  } else if (live_node(node)) {
    printf("LIVE: FIRST\n");
    schedule(first_live_child(node), SELECT); // first live child finds a live child or does and expand creating a new child
  } else {
    printf("ERROR: not leaf, not dead, not live: %d\n", node->path);
    print_tree(root, 2);
    exit(0);
  }
}


/********************************
 *** EXPAND                   ***
 ********************************/

    /*
Hmm. Dit is apart. Nieuwe nodes worden op hun home machine gemaakt.
In de TT; en ook als job in de job queue.
dus new_leaf is een RPC?
En wat is de betekenis van de pointer die new_leaf opleverd als de nieuwe leaf
	    op een andere machine zit? In SHM is dat ok, maar later in Distr Mem
	    Is de pointer betekenisloos of misleidend.

	   OK. Laten we voor SHM en threads het maar even zo laten dan.
    */
// Add new children, schedule them for selection
// Can this work? it references nodes (children) at other home machines
// must find out if remote pointers is doen by new_leaf or by schedule
node_type * first_live_child(node_type *node) {

  printf("%d: %s EXPAND d:%d    ", 
	 node->path,
	 node->maxormin==MAXNODE?"+":"-",
	 node->depth);

  int ch = 0;
  node_type *older_brother = NULL;
  node->n_children  = TREE_WIDTH;

  /* 
   * add one child
   * first find thenext available spot
   */
  if (node->children == NULL) {
    node->children    = malloc(sizeof(node_type *)*TREE_WIDTH);
    for (ch = 0; ch < node->n_children; ch++) {
      node->children[ch] = NULL;
    }
  }

  // find first empty
  for (ch = 0; node->children[ch] && dead_node(node->children[ch]); ch++) {
    // this child exists. try next
    older_brother = node->children[ch];
  }
  if (ch >= TREE_WIDTH) {
    printf("ERROR: all children alrady expanded and dead: %d\n", node->path);
    return NULL;
  }
  node_type *child = node->children[ch];

  // found live existing child
  if (child && live_node(child)) {
    return child;
  }

  // did not find a child. do expand
  if (ch < TREE_WIDTH) {
    node->children[ch] = new_leaf(node);
    if (node->children[ch]) {
      node->children[ch]->path = 10 * node->path;
      node->children[ch]->path += ch + 1;
      printf("%d  EXPAND created ch:%d -d:%d ch-p:%d\n", 
	     node->path,  
	     ch, node->children[ch]->depth, node->children[ch]->path);
      return node->children[ch];
    } 
  }  else {
    printf("EXPAND created nothing\n");
  }
}


/******************************
 *** PLAYOUT                ***
 ******************************/

// just std ab evaluation. no mcts playout
void do_playout(node_type *node) {
  node->a = node->b = evaluate(node);
  printf("%d: PLAYOUT d:%d    A:%d\n", node->path, node->depth, node->a);
  // can we do this? access a pointer of a node located at another machine?
  //  schedule(node->parent, UPDATE, node->lb, node->ub);
  schedule(node, UPDATE);
}

int evaluate(node_type *node) {
  //  return node->path;
  return rand() % (INFTY/8) - (INFTY/16);
}

/*****************************
 *** UPDATE                ***
 *****************************/

// backup through the tree
void do_update(node_type *node) {

  if (node && live_node(node)) {
    int continue_updating = 0;
    
    if (node->maxormin == MAXNODE) {
      node->parent->a = max(node->parent->a, node->a);
      node->parent->b = max_of_beta_kids(node->parent); //  infty if unexpanded kids
      // if we have expanded a full max node, then a beta has been found, which should be propagated upwards to my min parenr
      continue_updating = (node->parent->b != INFTY);

    }
    if (node->maxormin == MINNODE) {
      node->parent->a = min_of_alpha_kids(node->parent);
      node->parent->b = min(node->parent->b, node->b);
      continue_updating |= (node->parent->a != -INFTY); // if a full min node has been expanded, then an alpha has been bound, and we should propagate it to the max parent
    }
    printf("%d %s UPDATE d:%d  --  <%d:%d>\n", 
	   node->path, node->maxormin==MAXNODE?"+":"-", 
	   node->depth, node->board, 
	    node->a, node->b);

    if (continue_updating && node->parent) {
      schedule(node->parent, UPDATE);
    } 
  }
}

// end
