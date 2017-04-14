#include <stdio.h>
#include <stdlib.h>
#include <cilk/cilk.h>
#include "parab.h"

#define N_JOBS 100  // 100 jobs in job queue
#define N_MACHINES 16
#define TREE_WIDTH 6
#define TREE_DEPTH 4
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


node_type *new_leaf(node_type *p) {
  node_type *n = malloc(sizeof(node_type));
  n->board = rand() % N_MACHINES;
  n->lb = -INFTY;
  n->ub =  INFTY;
  n->children = NULL;
  n->n_children = 0;
  n->parent = p;
  n->maxormin = opposite(p->maxormin);
  if (p) {
    n->depth = p->depth - 1;
  }
  return n;
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

node_type *root;

/***************************
 *** MAIN                 **
 ***************************/

int main(int argc, char *argv[]) { 
  int n_proc = atoi(argv[1]);
  printf("Hello from ParAB with %d machines\n", n_proc);
  create_tree(TREE_DEPTH);
  printf("Tree Created\n");
  start_processes(n_proc);
  return 0;
}

void create_tree(int d) {
  root = new_leaf(NULL);
  root->maxormin = MAXNODE;
  root->depth = d;
  //  mk_children(root, d-1);
}

void mk_children(node_type *n, int d) {
  int i;
  n->n_children = TREE_WIDTH;
  for (i = 0; i < TREE_WIDTH; i++) {
    n->children[i] = new_leaf(n);
    if (d>0) {
      mk_children(n->children[i], d-1);
    }
  }
}
			  
/***************************
 *** OPEN/LIVE           ***
 ***************************/

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
    process_job(queue[i][top[i]--]);
  }
}

int not_empty(int top) {
  return (top) > 0;
}

// which q? one per processor
void add_to_queue(job_type *job) {
  int home_machine = job->node->board;
  if (top[home_machine] >= N_JOBS) {
    printf("ERROR: queue full\n");
  }
  queue[home_machine][++top[home_machine]] = job;
}

void process_job(job_type *job) {
  node_type *result_node;
  node_type *highest_node;

  if (leaf_node(job)) {
    do_playout(job->node);
    highest_changed_node = do_update_upwards(job->node);
    propagate_bounds_downward(highest_changed_node);
    schedule(highest_changed_node, SELECT);
  } else {
    n = do_select(job->node);
    if (open_node(n)) {
      do_expand(n);
    } else if (leaf_node(n)) {
      schedule(n, PLAYOUT);
    } else {
      printf("ERROR: must be leaf node\n");
    }
  }

    SELECT is strange. There are many threads, they are the work quueues.
all queuus are the node expanders & tree-traversers.


So what are we?
descending frmo the root doing SELECT and then EXPAND or PLAYOUT?
or do we just take the job that is at the top of the job queue and process that?
So a leaf gets a playout and an inner gets an expand and an update
but where is the select? if we just take the job node?
or is the select the priority in the priority queue?
the leftmost deepest open live node
or: expand the live next brother of the leftmost deepest closed live node (die niet geexpandeerd wordt door een ander)
(kan niet in TDS, daar doet elke processor altijd een andere node).
dan is select dus inderdaad impliciet in de priority van de priority queue
En is er geen expliciete select meer in het lgoritme.
Moet je wel de alpha en beta downward updates goed doen!
De select begint overigens niet vanaf de root, maar vanaf de highest changed node of the previous update pass
Of er moet toch een propagate down komen, aan het einde van de update, en dan de select helemaal overslaan.
of aan het einde van de propagate down is de select
Er si wel verschil tussen SELECT en PROPAGATE_DOWN.
SELECT doet een path, en propagate down doet een tree.
Dus SELECT is onvoldoende om all ebounds in een keer consistent te krijgen.
Is wel voldoende als SELECT telkens niet meer dan een node selecteert. 
Dan is select dus van af de higehst changed node, parallel, hoe krijgen we daar parallelisme in?
Het parallelisme zit niet in het aflopen van het SELECT pad, maar in de job queus,
het aprpalleisme zit in de priority queues!
er moeten dus meerdere soort-van-selects plaatsvinden.
Elke queue zijn eigen select, de left most deepest live node
Maar dan hebben we dus wel een propagate down nodig die de hele tree
van bounds consistent maakt. Meer dan een select die een enkel path doet.
Wat wel weer mooi is, is dat je in de priority queues, die gesorterd zijn, alle nodes beneden een
zekere highest node kunt nemen om de bounds van te verwerken.

select is sequentieel. Allemaal parallelle selects gaan naar dezelfde node toe. moet naar verschillende
				 
				 

  switch (job->type_of_job) {
  case SELECT: result_node = do_select(job->node, job->node->lb, job->node->ub); schedule(result_node, EXPAND); break;
  case EXPAND: do_expand(job->node); schedule(job->node, PLAYOUT); break;
  case PLAYOUT: do_playout(job->node); schedule(job->node, UPDATE); break;
  case UPDATE: highest_node = do_update(job->node); schedule(highest_node, SELECT); break;
  }
}

void schedule(node_type *node, int t) {
  if (node) {
    job_type *job = new_job(node, t);
    add_to_queue(job);
  } else {
    printf("NODE is NULL\n");
  }
}


/******************************
 *** SELECT                 ***
 ******************************/

// traverse to the left most deepest open node or leaf node
// but only one node at a time, since each node has its own home machine
node_type *do_select(node_type *node, int a, int b) {
  node_type *child;
  node_type *r;
  if (node == root && dead_node(node)) {
    printf("root is solved\n");
  }
  if (node->maxormin == MAXNODE) {
    a = max(a, node->lb);
  }
  if (node->maxormin == MINNODE) {
    b = min(b, node->ub);
  }
  // alphabeta cutoff
  if (a >= b) {
    return NULL;
  }
  // select an open, unexplored, node
  if (open_node(node)) {
    // update bounds downward, reconcile ub/lb and a/b: in maxnode max lb/a, in minnode min ub/b
    update_bounds_down(node, a, b); 
    waarom moeten de bounds van een open node bijgewerkt worden?
      wordt hij dcthgemaakt? met weke bounds dan? toch eerst expanderen, en tot een leaf doorgaan
en dan pas als we een leaf hebben een boudns updat ein de update upward krijgen?
    return node;
  } else {
    int ch = 0;
    child = node->children[ch++];
    // if current node is closed, find a live child and traverse it
    // descend the tree, a path of live nodes to find an open node
    while (child && dead_node(child)) {
      child = node->children[ch++];
      if (ch > node->n_children) {
	child = NULL;
      }

      Dus hier gaan we recursief de diepte in.
	Willen we dit? klopt dit?
	Nee toch?
	Want een volgende node zit op een andere home machine.
	Dus we moeten een Schedule SELECT doen, en niet een Aanroep SELECT
	Schedule SELECT zet hem op een gewenste processor neer.
	Aanroep blokkeert de thread, en kan niet werken, want
	we hebben deze child niet zelf........
	eigenlijk kan ik die bounds accesses node->lb, node->ub niet doen
	daar moet een retrieve tussen zitten. uit de hashtable
	en je moet deze nodeaccess dus schedulen op de home processor
	elk selectje moet lokaal, per node een aparte select

      schedule(child, SELECT);
      so what can we do about alpha and beta?
			  !!!!!!!!!!!!!!!!
moeten a en b ook in de child node worden meegegeven?

1. uitzoeken waar ab update moet, en waar lbub update moet
2. uitzoeken wat achter elkaar moet
select moet node voor node en opnieuw schedule			  
3. uitzoeken hoe select een node kan uitzoeken, en er toch par is,			  
hoe select door de job queues heen gaat, wat er gebeurd als
meerdere job queues proberen select te doen, of er dan par is

			  node:
			  OPEN (internal leaf): EXPAND (add kids to internal tree)
			  CLOSED/LIVE (internal/inner): SELECT (traverse down) (maar kan ook update zijn)
			  CLOSED/LEAF: PLAYOUT; UPDATE bounds of parent (maar kan ook select zijn)
			  dus een job moet een richting hebben, of een next action
			  CLOSED/DEAD: select next brother or update up
						  /*

  ab, are updated going down. are tightened by lbub of nodes. a=max(a,lb); b=min(b,ub)
lbub are updated going up, are the max and min of their children (max of their kids if max node, and vv)
* select: upd ab; if node.leaf schedule(Playout node); if node.dead schedule(select nextbrother node); if node.live schedule(select firstchild node); if node.open schedule expand node
* playout: node.eval; schedule update node
* expand: generate open kids & schedule on queues as select (to do upd ab, w/ lbub inf) & then expand
* update: upd lbub; schedule update parent

expand creates the parallelism. the subsequent selects traverse through the different work queues

						  */

						  
      r = do_select(child, a, b);
      // if r==NULL continue to next brother
      if (r) {
	break;
      }
    }
    // fill in alpha beta bounds
    //    r->lb = a;
    //    r->ub = b;
    return r;
  }
  //  bounds propagation;???
}


/********************************
 *** EXPAND                   ***
 ********************************/

void do_expand(node_type *node) {
  int ch = 0;
  node_type *child;
  node->n_children = TREE_WIDTH;
  for (ch = 0; ch < node->n_children; ch++) {
    node->children[i] = new_leaf(n);
    if (d>0) {
      mk_children(n->children[i], d-1);
    }
    add_to_queue(node->children[i]);
    if (open(node->childre[])) {
      schedule(SELECT)
  }

  EXPAND moet hier ter plekke nieuwe nodes maken, en doorsturen naar de
    home-machine.
    Met een store-entry zo gauw er een nieuwe node gemaakt is.

Dit klopt dus niet.
Want er wordt al een hele boom aangemaakt in create tree.
En nu worden hier dus kinderen in de node gehangen.
dat is dubbelop

En wat ook niet klopt is de jobqueue. Ik ben vergeten dat jobs er uit gehaald moeten
worden als ze verwerkt worden.
take a job moet een job er af halen.
    // do not forget left-first Dewey coding
    store_entry(child); // store tt entry at their home node
 make_job, scheduke
  }
}


/******************************
 *** PLAYOUT                ***
 ******************************/

// just std ab evaluation. no mcts playout
void do_playout(node_type *node) {
  node->lb = node->ub = evaluate(node);
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
    if (node->parent->maxormin == MAXNODE) {
      if (node->parent->ub < node->ub) {
	node->parent->ub = node->ub;
	continue_updating = 1;
      }      
      if (node->parent->lb < node->lb) {
	node->parent->lb = node->lb;
	continue_updating = 1;
      }
      // store in TT at home node
      store_node(node->parent);
    }
    if (node->parent->maxormin == MINNODE) {
      if (node->parent->ub > node->ub) {
	node->parent->ub = node->ub;
	continue_updating = 1;
      }      
      if (node->parent->lb > node->lb) {
	node->parent->lb = node->lb;
	continue_updating = 1;
      }
      // store in TT at home node
      store_node(node->parent);
    }
    node_type *r = NULL; // return highest node that is updated. if stopped by bounds, stop and return node
    if (continue_updating) {
      //Dit kan dus niet, dez emoet op de remote machine gescheduled worden.
      schedule(node->parent, UPDATE);
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
      
