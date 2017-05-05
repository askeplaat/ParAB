/*
 * parab.h
 *
 * Aske Plaat 2017
*/



/*
 * defines
 */

#define N_JOBS 20  // 100 jobs in job queue
#define N_MACHINES 2
#define TREE_WIDTH 3
#define TREE_DEPTH 4
#define INFTY  99999

#define SELECT 1
#define PLAYOUT 3
#define UPDATE 4

#define MAXNODE 1
#define MINNODE 2



/*
 * structs
 */

typedef struct node node_type;
typedef struct node { 
  int board;
  int a;
  int b;
  node_type **children;
  int n_children;
  node_type *parent;
  node_type *best_child;
  int  maxormin;
  int depth;
  int path;
} nt;

struct job {
  node_type *node;
  int type_of_job;
};
typedef struct job job_type;



/*
 * variables
 */

extern node_type *root;
extern job_type *queue[N_MACHINES][N_JOBS];
extern int top[N_MACHINES];
extern int total_jobs;
extern pthread_mutex_t mymutex;
extern pthread_mutex_t treemutex;
extern int max_q_length[N_MACHINES];
extern int n_par;



/*
 * prototypes
 */

void print_q_stats();
void set_best_child(node_type *node);
job_type *new_job(node_type *n, int t);
node_type *new_leaf(node_type *p);
int opposite(int m);
node_type *first_child(node_type *node);
int max_of_beta_kids(node_type *node);
int min_of_alpha_kids(node_type *node);
void compute_bounds(node_type *node);
int leaf_node(node_type *node);
int seq(int machine);
int min(int a, int b);
int max(int a, int b);
node_type *next_brother(node_type *node);
int main(int argc, char *argv[]);
void print_tree(node_type *node, int d);
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
int not_empty_and_live_root();
int empty(int top);
int not_empty(int top);
void add_to_queue(job_type *job);
void process_job(job_type *job);
void schedule(node_type *node, int t);
void do_select(node_type *node);
node_type * first_live_child(node_type *node, int p);
void do_playout(node_type *node);
int evaluate(node_type *node);
void do_update(node_type *node);
void store_node(node_type *node);
void update_bounds_down(node_type *node, int a, int b);
