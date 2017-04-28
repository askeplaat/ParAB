/*
 * parab.h
 *
 * Aske Plaat 2017
*/

typedef struct node node_type;
typedef struct node { 
  int board;
  int a;
  int b;
  node_type **children;
  int n_children;
  node_type *parent;
  int  maxormin;
  int depth;
  int path;
} nt;

struct job {
  node_type *node;
  int type_of_job;
};
typedef struct job job_type;


job_type *new_job(node_type *n, int t);
node_type *new_leaf(node_type *p);
int opposite(int m);
node_type *first_child(node_type *node);
node_type *next_brother(node_type *node);
int main(int argc, char *argv[]);
void create_tree(int d);
void push_job(int home_machine, job_type *job);
job_type *pull_job(int home_machine);
void mk_children(node_type *n, int d);
int unexpanded_node(node_type *node);
int expanded_node(node_type *node);
int live_node(node_type *node);
int dead_node(node_type *node);
void start_processes(int n_proc);
void do_work_queue(int i);
int all_empty_and_live_root();
int empty(int top);
int not_empty(int top);
void add_to_queue(job_type *job);
void process_job(job_type *job);
void schedule(node_type *node, int t);
void do_select(node_type *node);
node_type * first_live_child(node_type *node);
void do_playout(node_type *node);
int evaluate(node_type *node);
void do_update(node_type *node);
void store_node(node_type *node);
void update_bounds_down(node_type *node, int a, int b);
