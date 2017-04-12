/*
 * parab.h
 *
 * Aske Plaat 2017
*/

typedef struct node node_type;
typedef struct node { 
  int board;
  int lb; 
  int ub;
  node_type **children;
  int n_children;
  node_type *parent;
  int  maxormin;
} nt;

struct job {
  node_type *node;
  int type_of_job;
};
typedef struct job job_type;


job_type *new_job(node_type *n, int t);
node_type *new_leaf(node_type *p);
int opposite(int m);
int main(int argc, char *argv[]);
void create_tree(int d);
void mk_children(node_type *n, int d);
int open_node(node_type *node);
int closed_node(node_type *node);
int live_node(node_type *node);
int dead_node(node_type *node);
void start_processes(int n_proc);
void do_work_queue(int i);
int not_empty(int top);
void add_to_queue(job_type *job);
void process_job(job_type *job);
void schedule(node_type *node, int job_type);
node_type *do_select(node_type *node, int a, int b);
void do_expand(node_type *node);
void do_playout(node_type *node);
int evaluate(node_type *node);
node_type *do_update(node_type *node);
void store_node(node_type *node);
void update_bounds_down(node_type *node, int a, int b);
