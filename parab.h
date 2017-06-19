
/*
 * parab.h
 *
 * Aske Plaat 2017
*/



/*
 * defines
 */

#define N_MACHINES 20
#define TREE_WIDTH 
#define TREE_DEPTH 7

#define SEQ_DEPTH 2


#define N_JOBS 10000  // 100 jobs in job queue

#define INFTY  99999

#define SELECT 1
#define PLAYOUT 3
#define UPDATE 4
#define BOUND_DOWN 5

#define MAXNODE 1
#define MINNODE 2

#define TRUE 1
#define FALSE 0

#define SAFETY_COUNTER_INIT 10000000



// use Parallel Unorderedness to determine how much parallelism there should be scheduled
#define PUO

/*
 * structs
 */

typedef struct node node_type;
typedef struct node { 
  int board;
  int wa; // window-alpha, passed in from caller.
  int wb;  // wa and wb are only used in liveness calulation. not stored in nodes lb, ub, a, b
  int a; // true alpha computed as max lower bound
  int b; // true beta, computerd as lowest upper bound
  int lb;
  int ub;
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
extern pthread_mutex_t jobmutex;
extern pthread_mutex_t treemutex;
extern int max_q_length[N_MACHINES];
extern int n_par;
extern int global_selects;
extern int global_leaf_eval;
extern int global_downward_aborts;
extern double global_unorderedness_seq_x[TREE_DEPTH];
extern int global_unorderedness_seq_n[TREE_DEPTH];

/*
 * prototypes
 */

void print_q_stats();
int lock(pthread_mutex_t *mutex);
int unlock(pthread_mutex_t *mutex);
int start_mtdf();
int start_alphabeta(int a, int b);
int child_number(int p);
int puo(node_type *node);
void set_best_child(node_type *node);
job_type *new_job(node_type *n, int t);
node_type *new_leaf(node_type *p);
int opposite(int m);
node_type *first_child(node_type *node);
int max_of_beta_kids(node_type *node);
int min_of_alpha_kids(node_type *node);
int max_of_ub_kids(node_type *node);
int min_of_lb_kids(node_type *node);
void compute_bounds(node_type *node);
int leaf_node(node_type *node);
int seq(node_type *node);
int min(int a, int b);
int max(int a, int b);
int update_selects_with_bound(node_type *node);
void sort_queue(job_type *q[], int t);
void print_queue(job_type *q[], int t);
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
int live_node_lbub(node_type *node);
int dead_node_lbub(node_type *node);
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
void do_bound_down(node_type *node);
void downward_update_children(node_type *node);
void store_node(node_type *node); 
void update_bounds_down(node_type *node, int a, int b);
