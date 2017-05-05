#include <stdio.h>         // for print
#include <stdlib.h>        // for rand
#include <assert.h>        // for print
#include <pthread.h>
#include "parab.h"         // for prototypes and data structures


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
 * parab6.c 27 april 2017 werkt op 1 thread corrected alphabeta
 * parab7.c 27 april 2017 parallelle versie, cilk threads
 * 
 * parab4.c
 * introduces next_brother pointer in node
 * needed since we generate one at a time, then process (search), and 
 * then generate the next brother, which thjen is searched with the new bound.
 * note that this is a very sequential way of looking at alphabeta
 * 
 * parab5.c going back to separate lb and ub
 * 
 * parab6.c using alpha and beta as upward lb and ub and as downward alpha and beta
 * the logic is there, and it is much cleaner this way. Code is about 50% shorter* although I am now cheating on node accesses to parent and child,
 * that in a distributed memory setting need to be fixed, they are remote references
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


/***************************
 ** VARIABLES             **
 **************************/

node_type *root = NULL;
job_type *queue[N_MACHINES][N_JOBS];
int top[N_MACHINES];
int total_jobs = 0;
pthread_mutex_t mymutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t treemutex = PTHREAD_MUTEX_INITIALIZER;
int max_q_length[N_MACHINES];
int n_par = 1;



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
  
  //  printf("M%d P%d: %s SELECT d:%d  ---   <%d:%d>   ", 
  //	 node->board, node->path, node->maxormin==MAXNODE?"+":"-",
  //	 node->depth,
  //	 node->a, node->b);

  if (leaf_node(node) && live_node(node)) { // depth == 0; frontier, do playout/eval
    //    printf("M%d PLAYOUT\n", node->board);
    schedule(node, PLAYOUT);
  } else if (live_node(node)) {
    //    printf("M%d LIVE: FLC\n", node->board);
    if (seq(node->board)) {
      // schedule one child. not much parallelism
      schedule(first_live_child(node, 1), SELECT); // first live child finds a live child or does and expand creating a new child
    } else {
      // schedule many children in parallel
      for (int p = 0; p < n_par; p++) {
	//        printf("M:%d P:%d par child:%d/%d\n", node->board, node->path, p, n_par);
	node_type *child = first_live_child(node, p+1); 
	if (child) {
	  schedule(child, SELECT); // first live child finds a live child or does and expand creating a new child
	}
      }
    }
  } else if (dead_node(node) && root != node) { // cutoff: alpha==beta
    //    printf("M%d DEAD: bound computation causes cutoff\n", node->board,
    //	   node->depth, node->path);
    // if only this could work across machines with remote references
    if (node->parent) {
      set_best_child(node);
      //      node->parent->best_child = node;
      schedule(node->parent, UPDATE);
    }
  } else {
    printf("M%d ERROR: not leaf, not dead, not live: %d\n", 
	   node->board, node->path);
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
node_type * first_live_child(node_type *node, int p) {

  //  printf("M%d P%d: %s FLC d:%d    ", 
  //	 node->board,
  //	 node->path,
  //	 node->maxormin==MAXNODE?"+":"-",
  //	 node->depth);

  int ch = 0;
  int found = 0;
  node_type *older_brother = NULL;
  node->n_children  = TREE_WIDTH;

  /* 
   * add one child
   * first find thenext available spot
   */
  if (node->children == NULL) {
    node->children    = malloc(sizeof(node_type *)*(node->n_children));
    for (ch = 0; ch < node->n_children; ch++) {
      node->children[ch] = NULL;
    }
  }

  // find the p-th live-child
  // find first p empty
  for (ch = 0; 
       ch < node->n_children && 
	 ((node->children[ch] && 
	   dead_node(node->children[ch])) ||
	  ++found < p); ch++) {
    // this child exists. try next
    older_brother = node->children[ch];
  }

  if (ch >= node->n_children) { 
    /*
      printf("M%d ERROR: all children already expanded: %d\n", 
      node->board, node->path);
      print_tree(root, TREE_DEPTH);
      print_q_stats();
      exit(0);
    */
    return NULL;
  }
  node_type *child = node->children[ch];

  // found live existing child
  if (child && live_node(child)) {
    //    printf("M%d FLC found existing live child %d\n", node->board, child->path);
    return child;
  }

  // did not find a child. do expand
  if (ch < node->n_children) { 
    node->children[ch] = new_leaf(node);
    if (node->children[ch]) {
      node->children[ch]->path = 10 * node->path;
      node->children[ch]->path += ch + 1;
      //      printf("M%d P%d FLC EXPAND created ch:%d -d:%d ch-p:%d\n", 
      //	     node->board, node->path,  
      //	     ch, node->children[ch]->depth, node->children[ch]->path);
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
  printf("M%d P%d: PLAYOUT d:%d    A:%d\n", 
	 node->board, node->path, node->depth, node->a);
  // can we do this? access a pointer of a node located at another machine?
  //  schedule(node->parent, UPDATE, node->lb, node->ub);
  if (node->parent) {
    set_best_child(node);
    //    node->parent->best_child = node;
    schedule(node->parent, UPDATE);
  }
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
  //  printf("%d UPDATE\n", node->path);

  if (node && node->best_child) {
    int continue_updating = 0;
    
    if (node->maxormin == MAXNODE) {
      node->a = max(node->a, node->best_child->a);
      node->b = max_of_beta_kids(node); //  infty if unexpanded kids
      // if we have expanded a full max node, then a beta has been found, which should be propagated upwards to my min parenr
      continue_updating = (node->b != INFTY);

    }
    if (node->maxormin == MINNODE) {
      node->a = min_of_alpha_kids(node);
      node->b = min(node->b, node->best_child->b);
      continue_updating = (node->a != -INFTY); // if a full min node has been expanded, then an alpha has been bound, and we should propagate it to the max parent
    }
    /*
    if (node && node->parent) {
      printf("M%d P%d %s UPDATE d:%d  --  %d:<%d:%d> n_ch:%d\n", 
	     node->board, node->path, node->maxormin==MAXNODE?"+":"-", 
	     node->depth, node->parent->path,
	     node->a, node->b, node->n_children);
    }
    */
    if (continue_updating) {
      if (node->parent) {
	//	if (node->parent->best_child) {
	set_best_child(node);
	  //	} 
	schedule(node->parent, UPDATE);
      }
    } else {
      // keep going, no longer autmatic select of root. select of this node
      schedule(node, SELECT);
    }
  }
}

// end
