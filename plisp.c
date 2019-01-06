#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>

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
typedef struct obj_t *primative(struct obj_t *root, struct obj_t **env, struct obj_t **args);

typedef struct obj_t {
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

    struct {        //Function
      struct obj_t *params;
      struct obj_t *body;
      struct obj_t *env;
    };

    struct {        //Env frame
      struct obj_t *vars;
      struct obj_t *up;
    };
  };
} obj_t;


//Constants
static obj_t *True    = &(obj_t) { TTRUE };
static obj_t *Nil     = &(obj_t) { TNIL };
static obj_t *Dot     = &(obj_t) { TDOT };
static obj_t *Cparen  = &(obj_t) { TCPAREN };

//list containing all symbols
static obj_t *symbols;

void error(char *emsg) {
  printf("error: %s\n", emsg);
}

//---------------------------------------- 
// Memory management
//---------------------------------------- 

#define MAX_MEM 8096

typedef struct obj_list_t {
  obj_t *obj;
  struct obj_list_t *next;
} obj_list_t;
obj_list_t *obj_list = 0;

void obj_list_add(obj_t *obj) {
  obj_list_t *temp = malloc(sizeof(obj_list_t));
  if(!temp) {
    error("allocation failed");
    exit(0);
  }
  temp->obj = obj;
  temp->next = obj_list;
  obj_list = temp;
}

unsigned int mem_used = 0;

static void gc(obj_t *root) {
  //walk root and set flag


  //free all objs that have flag set
  obj_list_t *f_entry = 0;
  for(obj_list_t **entry = &obj_list; *entry;) {
    if(!(*entry)->obj->gc_r) {
      free((*entry)->obj);
      f_entry = *entry;
    }
    (*entry)->obj->gc_r = 0;
    entry = &((*entry)->next);
    free(f_entry);
    f_entry = 0;
  }
}

static obj_t *alloc(obj_t *root, int type, size_t size) {
  size += offsetof(obj_t, value);

  if(size + mem_used >= MAX_MEM)
    gc(root);

  if(size + mem_used >= MAX_MEM) {
    error("memory exhausted");
    exit(0);
  }

  obj_t *obj = malloc(size);
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

#define ROOT_END  ((void*)-1)

#define ADD_ROOT(size)                        \
  obj_t *root_ADD_ROOT_[size+2];              \
  root_ADD_ROOT_[0] = root;                   \
  for(int i = 1; i <= size; i++)              \
    root_ADD_ROOT_[i] = 0;                    \
  root_ADD_ROOT_[size+1] = ROOT_END;          \
  root = root_ADD_ROOT_;

#define DEFINE1(var1)                         \
  ADD_ROOT(1);                                \
  obj_t *var1 = (obj_t*)(root_ADD_ROOT_ + 1); \

#define DEFINE2(var1, var2)                   \
  ADD_ROOT(2);                                \
  obj_t *var1 = (obj_t*)(root_ADD_ROOT_ + 1); \
  obj_t *var2 = (obj_t*)(root_ADD_ROOT_ + 2); \
    
#define DEFINE3(var1, var2, var3)             \
  ADD_ROOT(3);                                \
  obj_t *var1 = (obj_t*)(root_ADD_ROOT_ + 1); \
  obj_t *var2 = (obj_t*)(root_ADD_ROOT_ + 2); \
  obj_t *var3 = (obj_t*)(root_ADD_ROOT_ + 3); \
 
#define DEFINE4(var1, var2, var3, var4)       \
  ADD_ROOT(4);                                \
  obj_t *var1 = (obj_t*)(root_ADD_ROOT_ + 1); \
  obj_t *var2 = (obj_t*)(root_ADD_ROOT_ + 2); \
  obj_t *var3 = (obj_t*)(root_ADD_ROOT_ + 3); \
  obj_t *var4 = (obj_t*)(root_ADD_ROOT_ + 4); \
 
//---------------------------------------- 
// Constructors
//---------------------------------------- 

static obj_t *make_int(obj_t *root, int value) {
  obj_t *obj = alloc(root, TINT, sizeof(int));
  obj->value = value;
  return obj;
}

static obj_t *cons(obj_t *root, obj_t *car, obj_t *cdr) {
  obj_t *obj = alloc(root, TCELL, sizeof(obj_t*) * 2);
  obj->car = car;
  obj->cdr = cdr;
  return obj;
}

static obj_t *make_primative(obj_t *root, primative *fn) {
  obj_t *obj = alloc(root, TPRIMATIVE, sizeof(primative*));
  obj->fn = fn;
  return obj;
}

static obj_t *make_function(obj_t *root, obj_t *env, int type, obj_t *params, obj_t *body) {
  assert(type == TFUNCTION || type == TMACRO);  
  obj_t *obj = alloc(root, type, sizeof(obj_t*) * 3);
  obj->params = params;
  obj->body = body;
  obj->env = env;
  return obj;
}

static obj_t *make_env(obj_t *root, obj_t *vars, obj_t *up) {
  obj_t *obj = alloc(root, TENV, sizeof(obj_t*) * 2);
  obj->vars = vars;
  obj->up = up;
  return obj;
}

// ((x . y) . a)
static obj_t *acons(obj_t *root, obj_t *x, obj_t *y, obj_t *a) {
  DEFINE1(cell);
  cell = cons(root, x, y);
  return cons(root, cell, a);
}

//---------------------------------------- 
// PARSER
//---------------------------------------- 

#define SYMBOL_MAX_LEN 200
const char symbol_chars[] = "~!@#$&^&*-_=+:/?<>";

static obj_t *read_exp(obj_t *root);

static int peek(void) {
  int c = getchar();
  ungetc(c, stdin);
  return c;
}

static obj_t *reverse(obj_t *p) {
  obj_t *ret = Nil;
  while(p != Nil) {
    obj_t *head = p;
    p = p->cdr;
    head->cdr = ret;
    ret = head;
  }
  return ret;
}

static void skip_line(void) {
  for(;;) {
    int c = getchar();
    if(c == EOF ||c == '\n')
      return;
    if(c == '\r') {
      if(peek() == '\n')
        getchar();
      return;
    }
  }
}

// '(' has already been read
static obj_t *read_list(obj_t *root) {
  DEFINE3(obj, head, last);
  head = Nil;
  for(;;) {
    obj = read_exp(root);
    if(!obj)
      error("unclosed paranthesis");
    if(obj == Cparen)
      return reverse(head);
    if(obj == Dot) {
      last = read_exp(root);
      if(read_exp(root) != Cparen)
        error("close paranthesis expected after dot");
      obj_t *ret = reverse(head);
      head->cdr = last;
      return last;
    }
    head = cons(root, obj, head);
  }
}


// returns symbol if name is already present
static obj_t *intern(obj_t *root, char *name) {
  for(obj_t *p = symbols; p != Nil; p = p->cdr)
    if(!strcmp(name, p->car->name)) 
      return p->car;
  DEFINE1(sym);
  sym = make_symbol(root, name);
  symbols = cons(root, sym, &symbols); 
  return sym;
}

static obj_t *read_quote(obj_t *root) {
  DEFINE2(sym, tmp);
  sym = intern(root, "quote");
  tmp = read_exp(root);
  tmp = cons(root, tmp, &Nil);
  tmp = cons(root, sym, tmp);
  return tmp;
}

static int read_number(int val) {
  while(isdigit(peek()))
    val = val * 10 + (getchar() - '0');
  return val;
}

static obj_t *read_symbol(obj_t *root, char c) {
  char buf[SYMBOL_MAX_LEN + 1];
  buf[0] = c;
  int len = 1;
  while(isalnum(peek()) || strchr(symbol_chars, peek())) {
    if(SYMBOL_MAX_LEN <= len)
      error("symbol name too long");
    buf[len++] = getchar();
  }
  buf[len] = 0;
  return intern(root, buf);
}

static obj_t *read_exp(obj_t *root) {
  for(;;) {
    int c = getchar();
    if(c == ' ' || c == '\n' || c == '\r' || c == '\t') 
      continue;
    if(c == EOF)
      return 0;
    if(c == ';') {
      skip_line();
      continue;
    }
    if(c == '(')
      return read_list(root);
    if(c == ')')
      return Cparen;
    if(c == '.')
      return Dot;
    if(c == '\'')
      return read_quote(root);
    if(isdigit(c))
      return make_int(root, read_number(c - '0'));
    if(c == '-' && isdigit(peek()))
      return make_int(root, -read_number(0));
    if(isalpha(c) || strchr(symbol_chars, c))
      return read_symbol(root, c);
    error("unable to handle char"); //TODO: print c
  }
}

static void print(obj_t *obj) {
  switch(obj->type) {
    case TCELL:
      print("(");
      for(;;) {
        print(obj->car);
        if(obj->cdr == Nil)
          break;
        if(obj->cdr->type != TCELL) {
          printf(" . ");
          print(obj->cdr);
          break;
        }
        printf(" ");
        return;
      }
      printf(")");
      return;

#define CASE(type, ...)       \
  case type:                  \
      printf(__VA_ARGS__);    \
      return
  CASE(TINT, "%d", obj->value);
  CASE(TSYMBOL, "%s", obj->name);
  CASE(TPRIMATIVE, "<primative>");
  CASE(TFUNCTION, "<function>");
  CASE(TMACRO, "<macro>");
  CASE(TTRUE, "t");
  CASE(TNIL, "()");
#undef CASE
  
  default:
    error("compiler error: unknown tag type");
  }
}

static int length(obj_t *list) {
  int len = 0;
  for(; list->type == TCELL; list = list->cdr)
    len++;
  return list == Nil ? len : -1;
}

//---------------------------------------- 
// EVALUATOR
//---------------------------------------- 

static obj_t *eval(obj_t *root, obj_t *env, obj_t *obj);

static void add_variable(obj_t *root, obj_t *env, obj_t *sym, obj_t *val) {
  DEFINE2(vars, *tmp);
  vars = env->vars;
  tmp = acons(root, sym, val, vars);
  env->vars = tmp;
}

static obj_t *push_env(obj_t *root, obj_t *env, obj_t *vars, obj_t *vals) {
  DEFINE3(map, sym, val);
  map = Nil;
  for(; vars->type == TCELL; vars = vars->cdr, vals = vals->cdr) {
    if(vals->type != TCELL) 
      error("cannot apply function: number of argument does not match");
    sym = vars->car;
    val = vals->car;
    map = acons(root, sym, val, map);
  }
  if(vars != Nil)
    map = acons(root, vars, vals, map);
  return make_env(root, map, env);
}

//evaluates the list elements from the head and returns the last value














