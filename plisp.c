extern void *malloc(size_t);
extern void free(void*);
extern printf;

enum {
  TINT = 1,
  TCELL,
  TSYMBOL,
  TPRIMATIVE,
  TFUNCTION,
  TMACRO,
  TENV,
  TTRUE,
  TNIL,
  TDOT,
  TCPAREN,
};

struct obj_t;
typedef struct obj_t *primative(void *root, struct obj_t **env, struct obj **args);

struct obj_t {
  int type;
  unsigned int size;
  unsigned char gc_r; // used by gc
  
  union {
    int value;      //Int
    struct {        //Cell
      struct obj_t *car;
      struct obj_t *cdr;
    };
    char *name;     //Symbol
    primative *fn;  //Primative

    struct {        //Env frame
      struct obj_t *vars;
      struct obj_t *up;
    };
  };
};


//Constanst
static struct obj_t *True    = &(struct obj_t) { TTRUE };
static struct obj_t *Nil     = &(struct obj_t) { TNIL };
static struct obj_t *Dot     = &(struct obj_t) { TDOT };
static struct obj_t *Cparen  = &(struct obj_t) { TCPAREN };

//list containing all symbols
static struct obj_t *symbols;

void error(char *emsg) {
  printf("error: %s\n", emsg);
}

//---------------------------------------- 
// Memory management
//---------------------------------------- 

#define MAX_MEM 8096

struct obj_list_t {
  struct obj_t *obj;
  struct obj_list_t *next;
};
struct obj_list_t *obj_list = 0;

void obj_list_add(struct obj_t *obj) {
  struct obj_list_t *temp = malloc(sizeof(struct obj_list_t));
  if(!temp) {
    error("allocation failed");
    exit(0);
  }
  temp->obj = obj;
  temp->next = obj_list;
  obj_list = temp;
}

unsigned int mem_used = 0;

static void gc(void *root) {
  //walk root and set flag


  //free all objs that have flag set
  struct obj_list_t *f_entry = 0;
  for(struct obj_list_t **entry = &obj_list; *entry;) {
    if(!*entry->obj->gc_r) {
      free(*entry->obj);
      f_entry = *entry;
    }
    *entry->obj->gc_r = 0;
    entry = &entry->next;
    free(f_entry);
    f_entry = 0;
  }
}

static obj_t *alloc(void *root, int type, size_t size) {
  size += offsetof(obj_t, value);

  if(size + mem_used >= MEM_MAX)
    gc(root);

  if(size + mem_used >= MEM_MAX) {
    error("memory exhausted");
    exit(0);
  }

  struct obj_t *obj = malloc(size);
  if(!obj) {
    error("allocation failed");
    exit(0);
  }
  obj->type = type;
  obj->size = size;
  obj->gc_r = 0;

  mem_used += size;
  obj_list_add(obj);
  
  return obj;
}













