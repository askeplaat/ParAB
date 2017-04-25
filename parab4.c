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
#define EXPAND 2
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

node_type *new_leaf(node_type *p, int alpha, int beta) {
  if (p && p->depth <= 0) {
    return NULL;
  }
  node_type *n = malloc(sizeof(node_type));
  n->board = rand() % N_MACHINES;
  n->maxormin = p?opposite(p->maxormin):MAXNODE;
#ifdef MAXMININIT
  if (n->maxormin == MAXNODE) {
    n->lb = -INFTY-1;  n->ub =  -INFTY-1; n->g = -INFTY;   n->child_g = -INFTY;
  } else {
    n->lb =  INFTY+1;  n->ub =   INFTY+1; n->g =  INFTY;   n->child_g =  INFTY;
  }
#else
  n->lb = -INFTY-1;
  n->ub =  INFTY+1;
#endif
  /*
 deze komt dus nooit omlaag in max nodes.
 niet goed. we willen juist dat hij bij alle kids gehad, de hoogste van alle kids laat zien. en niet infty
gewoon op -infty zetten voor maxnodes?
  */
 
  n->alpha = alpha; // for SELECT DOWN
  n->beta  = beta; // for SELECT DOWN
  n->child_lb = -INFTY;  // for UPDATE UP
  n->child_ub =  INFTY;  // for UPDATE UP
  n->children =  NULL;
  n->n_children = 0;
  n->n_open_kids = 0;
  n->parent = p;
  n->brother = NULL;
  n->path = 0;
  if (p) {
    n->depth = p->depth - 1;
    //    n->path = p->path*10+p->current_child;
  }
  return n; // return to where? return a pointer to our address space to a different machine's address space?
}

// maxnode: one child is enough to get a lowerbound
// minnode: only of all kids are traversed then a lowerbound exists
int true_lb(node_type *node) {
  if (node->maxormin == MAXNODE || node->n_open_kids <= 0) {
    return node->lb;
  } else {
    // MINNODE and some open children, so a MIN gives a lowerbound of -INFTY
    return -INFTY;
  }
} 

// minnode or maxnode with all kids traversed gives an upperbound
int true_ub(node_type *node) {
  if (node->maxormin == MINNODE || node->n_open_kids <= 0) {
    return node->ub;
  } else {
    // MAXNODE and some open children, so a MAX gives a upperbound of INFTY
    return INFTY;
  }
} 

// maxnode: one child is enough to get a lowerbound
// minnode: only of all kids are traversed then a lowerbound exists
int true_child_lb(node_type *node) {
  if (node->maxormin == MAXNODE || node->n_open_kids <= 0) {
    return node->child_lb;
  } else {
    // MINNODE and some open children, so a MIN gives a lowerbound of -INFTY
    return -INFTY;
  }
} 

// minnode or maxnode with all kids traversed gives an upperbound
int true_child_ub(node_type *node) {
  if (node->maxormin == MINNODE || node->n_open_kids <= 0) {
    return node->child_ub;
  } else {
    // MAXNODE and some open children, so a MAX gives a upperbound of INFTY
    return INFTY;
  }
} 



node_type *first_child(node_type *node) {
  if (node && node->children) {
    return node->children[0];
  } else {
    return NULL;
  }
}

node_type *next_brother(node_type *node) {
  printf("*****NEXT\n");
  if (node) {
    return node->brother;
  } else {
    return NULL;
  }
}

/*
void mk_children(node_type *n, int d) {
  int i;
  n->n_children = TREE_WIDTH;
  n->children =  malloc(sizeof(node_type *)*TREE_WIDTH);
  for (i = 0; i < TREE_WIDTH; i++) {
    n->children[i] = new_leaf(n, n->alpha, n->beta);
       if (d>0) {
         mk_children(n->children[i], d-1);
       }
  }
}
*/

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
  if (node && d >= 0) {
    printf("%d: %d %s <%d,%d>:(%d,%d)\n",
	   node->depth, node->board, ((node->maxormin==MAXNODE)?"+":"-"), node->alpha, node->beta, node->lb, node->ub);
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
  schedule(root, SELECT, -INFTY, INFTY);
  printf("Tree Created. Root: %d\n", root->board);
  start_processes(n_proc);
  print_tree(root, 3);
  return 0;
}

void create_tree(int d) {
  root = new_leaf(NULL, -INFTY, INFTY);
  root->depth = d;
  //  mk_children(root, d-1);
}



/***************************
 *** OPEN/LIVE           ***
 ***************************/

int leaf_node(node_type *node) {
  return node && node->depth <= 0;
}
int unexpanded_node(node_type *node) {
  //  return node && (node->lb <= -INFTY && node->ub >= INFTY); // not expanded, untouched. is live
  return node && node->n_children == 0; // not expanded, untouched. is live
}
int expanded_node(node_type *node) {
  return node && !unexpanded_node(node);  // touched, may be dead or live (AB cutoff or not)
}
int live_node(node_type *node) {
  //  return node && node->lb < node->ub; // alpha beta???? window is live. may be open or closed
  return node && node->alpha < node->beta; // alpha beta???? window is live. may be open or closed
}
int dead_node(node_type *node) {  
  return node && !live_node(node);    // ab cutoff. is closed
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
      schedule(root, SELECT, root->alpha, root->beta);
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


void schedule(node_type *node, int t, int alpha, int beta) {
  if (node) {
    // copy a,b
    job_type *job = new_job(node, t);

    // send to remote machine
    add_to_queue(job, alpha, beta);
  } else {
    printf("schedule: NODE to schedule in job queue is NULL. Type: %d\n", t);
  }
}

// which q? one per processor
void add_to_queue(job_type *job, int alpha, int beta) {
  int home_machine = job->node->board;
  if (home_machine >= N_MACHINES) {
    printf("ERROR: home_machine %d too big\n", home_machine);
    exit(1);
  }
  if (top[home_machine] >= N_JOBS) {
    printf("ERROR: queue full\n");
    exit(1);
  }

  /* 
   * two types of jobs have bound propagation actions
   * that move do-sewn or up in the tree
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
    //    job->node->child_lb = alpha;
    //    job->node->child_ub  = beta;
    job->node->child_g = alpha;
    job->node->child_g  = beta;
  }
  
  push_job(home_machine, job);
}


void push_job(int home_machine, job_type *job) {
  queue[home_machine][++(top[home_machine])] = job;
  printf("    %d: PUSH  [%d] <%d:%d> G:%d chg:%d\n", 
	 job->node->path, job->type_of_job,
	 job->node->alpha, job->node->beta, job->node->g, job->node->child_g);
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


/******************************
 *** SELECT                 ***
 ******************************/

// traverse to the left most deepest open node or leaf node
// but only one node at a time, since each node has its own home machine
void do_select(node_type *node) {

  if (node == root && dead_node(node)) {
    printf("root is solved: %d:%d\n", root->alpha, root->beta);
    return;
  }

  /* 
   * alpha beta update, top down 
   */
  /*
  int  a = node->maxormin==MAXNODE?max(node->g, node->alpha):node->alpha;
  int  b = node->maxormin==MINNODE?min(node->g, node->beta):node->beta;
  if (a >= b) {
    printf("%d %s closing window [%d:%d]:%d <%d:%d> openkids:%d\n", node->path, 
	   node->maxormin==MAXNODE?"+":"-",
	   a, b, node->g, node->alpha, node->beta, node->n_open_kids);
  }
  */  
hmm. moet select de g wissen?
waar vindt de injectie van g in de bounds-sequence plaats?
doet update dat niet?
  g is voor naar boven toe, niet voor naar beneden toe. 
waarom zit die g hier uberhaupt in de alpab beta bound select?
Het is voor de overstap van up/down. In het down gaan moet de up-bound worden doorgegeven aan de down-bound.
En als hij dan gebruikt is moet hij gewist worden.
g values moeten niet te ver naar boven worden doorgegeven. 
update gaat te ver door nara boven. hij schiet door.
na een child expand is er nog geen info genoeg voor een alpha bound aan de root. Dat is er nu wel

  
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  if (node->maxormin == MAXNODE) {
    //    node->alpha = max(node->alpha, true_lb(node));
    node->alpha = max(node->alpha, node->g);
    if (node->alpha > -INFTY && node->n_open_kids <= 0) {
      node->beta = node->alpha;
      printf("%d closing window <%d:%d> openkids:%d\n", node->path, node->alpha, node->beta, node->n_open_kids);
    }
  }

  if (node->maxormin == MINNODE) {
    //    node->beta = min(node->beta, true_ub(node));
    node->beta = min(node->beta, node->g);
    if (node->beta < INFTY && node->n_open_kids <= 0) {
      node->alpha = node->beta;
      printf("%d closing window <%d:%d> openkids:%d\n", node->path, node->alpha, node->beta, node->n_open_kids);    }
  }
  
  /*
  printf("SELECT %s d:%d p:%d O:%d Cl:%d Li:%d De:%d Le:%d path:%d nopen:%d  ---   <%d:%d> G:%d\n", 
	 node->maxormin==MAXNODE?"+":"-",
	 node->depth, node->path,
	 open_node(node), closed_node(node), 
	 live_node(node), dead_node(node), leaf_node(node), 
	 node->path, node->n_open_kids, 
	 node->alpha, node->beta,  node->g);
  */
  printf("%d: %s SELECT d:%d nopen:%d  ---   <%d:%d> G:%d    ", 
	 node->path, node->maxormin==MAXNODE?"+":"-",
	 node->depth, node->n_open_kids, 
	 node->alpha, node->beta,  node->g);
  /*
  // alphabeta cutoff
  if (node->alpha >= node->beta) {
    printf("******CUTOFF****** %d %d <%d:%d>\n", node->depth, node->path, node->alpha, node->beta);
    return;
  }
  */
  if (leaf_node(node)) { // dpeth == 0; frontier, do playout/eval
    printf("PLAYOUT\n");
    schedule(node, PLAYOUT, node->alpha, node->beta);
  } else if (dead_node(node) && root != node) { // cutoff: alpha==beta
    printf("DEAD: NEXT %d %d\n", node->depth, node->path);
    do_expand(node->parent);
    schedule(next_brother(node), SELECT, node->parent->alpha, node->parent->beta);
    //schedule(node->parent, EXPAND, node->parent->alpha, node->parent->beta);
  } else if (expanded_node(node) && live_node(node)) {
    printf("EXPANDED/LIVE: FIRST\n");
    /*
dit is fout
dit moet niet de eerste kind zijn die je afloopt
maar het beste kind
  is de non-beste niet dood? (a/b)
wordt de updat wel hoog genoeg doorgegeven?
  het lijkt of hij niet hoog genoeg doorkomt, en de bounds en dus de dode window to laag blijven steken
    */
    schedule(first_child(node), SELECT, node->alpha, node->beta);
  } else if (unexpanded_node(node)) {
    printf("UNEXPANDED node: expand\n");
    // node is open, that means untouched, no kids, must grow kids.
    // OPEN is "leaf" in the internal tree, frontier node
    // lb == -INFTY, ub == INFTY, n_children == 0, children[] == NULL
    if (node->n_children != 0 || node->children) {
      printf("ERROR: open node has children\n%d %d\n", 
	     node->n_children, node->n_open_kids);
      print_tree(root, 2);
      //      exit(0);
    } else {
      schedule(node, EXPAND, node->alpha, node->beta);
    }
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
void do_expand(node_type *node) {

  printf("%d: %s EXPAND d:%d    ", 
	 node->path,
	 node->maxormin==MAXNODE?"+":"-",
	 node->depth);

  int ch = 0;
  node_type *older_brother = NULL;
  node->n_children  = TREE_WIDTH;
  node->n_open_kids = TREE_WIDTH;

  if (node->parent) {
    node->parent->n_open_kids--;
    if (node->n_open_kids < 0) {
      printf("ERROR: n_open_kids below zero\n");
      print_tree(root, 2);
      //      exit(0);
    }
  }
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
  for (ch = 0; node->children[ch]; ch++) {
    // this child exists. try next
    older_brother = node->children[ch];
  }

  if (ch==0&&older_brother!=NULL) {
    printf("older brother is non-NULL for first child d:%d, p:%d\n",
	   node->depth, node->path);
  }

  if (node->children[ch]) {
    printf("ERROR: trying to create child in existing spot\n");
  }
  if (ch < TREE_WIDTH) {
    node->children[ch] = new_leaf(node, node->alpha, node->beta);
    if (ch > 0) {
      node->children[ch-1]->brother = node->children[ch];
    }
    if (node->children[ch]) {
      node->children[ch]->path = 10 * node->path;
      node->children[ch]->path += ch + 1;
      printf("EXPAND created ch:%d -d:%d ch-p:%d\n", ch, node->children[ch]->depth, node->children[ch]->path);
#undef SCHEDULE_CHILD
#ifdef SCHEDULE_CHILD
      // We assume after expand immediate PLAYOUT follows
      schedule(node->children[ch], SELECT, node->alpha, node->beta);
#endif
#ifdef SCHEDULE_ROOT_AND_NODE_WINDOW
      schedule(root, SELECT, node->alpha, node->beta);
#endif
#ifdef SCHEDULE_ROOT
      schedule(root, SELECT, root->alpha, root->beta);
#endif
    } else {
      /*
      printf("ERROR: new_leaf is NULL. d:%d p:%d\n", node->depth, node->path);
      print_tree(root, 3);
      exit(0);
      */
    }
  }  else {
    printf("EXPAND created nothing\n");
  }

         /*
      it should not push all children immediately. It should first push only the first cahild, then have that pulled and processed, and then when that subtree has been searhced, and the search results *(bound) is knownw, and can be used, then the second and thirs and lataer child should be pushed to the job queue, wiht the new search bound. 
This can be achieved with a priority queue, where nodes are bacsically taken off of the queue head based on their lexicogaphical ordering, not when they were pushed in temporal ordering/created. allways do the deepest leftmost first. 
children should only be created and pushed to the queue when their bound is reayd, in alpha beta they should wait for the older brother to be completed.
Or, they may be created and pushed on the quuee, but when they are then selected, they should searh cand see if a bound is rady in the ancestral path
a find-bound-among-parents- function before you go about and select/expand yourslef.
theis can be achieved by the select, if the select would traverse all the way from the root to the active node, instead of just taking it from the job queue. In other words, expand should only expand the first child, not all younger brothers, with their immature bounds, too early bounds. Not create younger children, let them be created by the select form the root, just like MCTS does. 
So Only Create First Child!!! not all the younger.
The current expand is a recursive-alpha-beta-like expand, you put all children at once in the job queue!!!

Should First  push only the first child. 
Then that first child. when it is done, should push its brother (with the new bound)
(Does this have parallelism?)
      */
}


/******************************
 *** PLAYOUT                ***
 ******************************/

// just std ab evaluation. no mcts playout
void do_playout(node_type *node) {
  node->g = node->lb = node->ub = evaluate(node);
  printf("%d: PLAYOUT d:%d    G:%d\n", node->path, node->depth, node->g);
  // can we do this? access a pointer of a node located at another machine?
  //  schedule(node->parent, UPDATE, node->lb, node->ub);
  schedule(node->parent, UPDATE, node->g, node->g);
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
  /*
  printf("UPDATE %s %d:%d --  %d nopen:%d G:%d chg: %d\n", 
	 node->maxormin==MAXNODE?"+":"-", 
	 node->depth, node->board, 
	 node->path, node->n_open_kids, node->g, node->child_g);
  */
  if (node && live_node(node)) {
    int continue_updating = 0;
    /*
    hmm. no, only at playout is not enough, inner nodes must also be changed. It really is when all kids of a node have been touched, when a node is fully explored.
So we need to count the number of open children!
      It is in expand! whenever a node is expanded, the parent count decrements.
Huh???>>?????
      open means having no kids. so the PARENT of the node that is expanded, is decremented. What about leaves?
*/
    /*
    node->n_open_kids--;
    only direct kids, not every upward pass.
      so that means only do it once each pass, only the first time.
That is doable to implement
			      only after playout, not when scheduled by update
    */
    

    if (node->maxormin == MAXNODE) {
      if (node->child_g > node->g) {
	continue_updating = 1;
      } else {
	printf("STOP UPDATING: d:%d p:%d c:%d g:%d\n", node->depth, node->path, node->child_g, node->g);
      }
      node->g = max(node->g, node->child_g);
      /*
      //      if (node->ub < true_child_ub(node)) {
      if (node->ub > true_child_ub(node)) {
	node->ub = true_child_ub(node);
	continue_updating = 1;
      }      
      if (node->lb < true_child_lb(node)) {
	node->lb = true_child_lb(node);
	continue_updating = 1;
      }
      */
    }
    /*
Dit klopt dus niet.
  Na een enkele pass omhoog vanaf de leaves naar de root is de root dood, ub en lb zijn gelijk. het window is dicht. dat klopt niet. de ub van ene max node is na een kind altijd nog plus oneindig
  de ab doen? dan is a de waarde van het kind, en is b plus oneindig
    */
    if (node->maxormin == MINNODE) {
      if (node->child_g < node->g) {
	continue_updating = 1;
      } else {
	printf("STOP UPDATING: d:%d p:%d c:%d g:%d\n", node->depth, node->path, node->child_g, node->g);
      }
      node->g = min(node->g, node->child_g);
      /*
      if (node->ub > true_child_ub(node)) {
	node->ub = true_child_ub(node);
	continue_updating = 1;
      }      
      //      if (node->lb > true_child_lb(node)) {
      if (node->lb < true_child_lb(node)) {
	node->lb = true_child_lb(node);
	continue_updating = 1;
      }
      */
    }
  printf("%d %s UPDATE d:%d  --  %d nopen:%d <%d:%d> G:%d chg:%d\n", 
	 node->path, node->maxormin==MAXNODE?"+":"-", 
	 node->depth, node->board, 
	 node->n_open_kids, node->alpha, node->beta, node->g, node->child_g);

    if (continue_updating && node->parent) {
      //Dit kan dus niet, dez emoet op de remote machine gescheduled worden.
      //      schedule(node->parent, UPDATE, node->lb, node->ub);
      schedule(node->parent, UPDATE, node->g, node->g);
      //Alles moet in stapjes per node. node voor node, alles gescheduled up de home machine
      //      moet er geen select komen na een update?
      //r = do_update(node->parent);
    } else {
      if (node->maxormin==MAXNODE) {
	if (node->n_open_kids <= 0) {
	  schedule(node, SELECT, node->g, node->g);   
	} else {
	  schedule(node, SELECT, node->g, node->beta);   
	}
      } else {
	if (node->n_open_kids <= 0) {
	  schedule(node, SELECT, node->g, node->g);   
	} else {
	  schedule(node, SELECT, node->alpha, node->g);
	}
      }
      //      schedule(node, SELECT, true_lb(node), true_ub(node));
    }
  }
  return;
}

/*
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
	node_type *child = node->children[ch];
	update_bounds_down(child, a, b);
	propagate_bounds_downward(child);
      }
    }
  }
}
  
*/    
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
