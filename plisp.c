extern void *malloc(size_t);
extern void free(void*);
extern printf;
extern int getchar();
extern void assert(int);
extern int strcmp(char*,char*);
extern int strcpy(char*,char*);

static void error(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "error: ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}


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
typedef struct obj_t *primative(struct obj_t *root, struct obj_t **env, struct obj **args);

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

static obj_t *alloc(obj_t *root, int type, size_t size) {
  size += offsetof(obj_t, value);

  if(size + mem_used >= MEM_MAX)
    gc(root);

  if(size + mem_used >= MEM_MAX) {
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
  obj_t *root_ADD_ROOT_[size+2]               \
  root_ADD_ROOT_[0] = root;                   \
  for(int i = 1; i <= size; i++)              \
    root_ADD_ROOT_[i] = 0;                    \
  root_ADD_ROOT_[size+1] = ROOT_END;          \
  root = root_ADD_ROOT_;

#define DEFINE1(var1)                         \
  ADD_ROOT(1)                                 \
  obj_t *var1 = (obj_t*)(root_ADD_ROOT_ + 1)  \

#define DEFINE2(var1, var2)                   \
  ADD_ROOT(2)                                 \
  obj_t *var1 = (obj_t*)(root_ADD_ROOT_ + 1)  \
  obj_t *var2 = (obj_t*)(root_ADD_ROOT_ + 2)  \
    
#define DEFINE3(var1, var2, var3)             \
  ADD_ROOT(3)                                 \
  obj_t *var1 = (obj_t*)(root_ADD_ROOT_ + 1)  \
  obj_t *var2 = (obj_t*)(root_ADD_ROOT_ + 2)  \
  obj_t *var3 = (obj_t*)(root_ADD_ROOT_ + 3)  \
 
#define DEFINE4(var1, var2, var3, var4)       \
  ADD_ROOT(4)                                 \
  obj_t *var1 = (obj_t*)(root_ADD_ROOT_ + 1)  \
  obj_t *var2 = (obj_t*)(root_ADD_ROOT_ + 2)  \
  obj_t *var3 = (obj_t*)(root_ADD_ROOT_ + 3)  \
  obj_t *var4 = (obj_t*)(root_ADD_ROOT_ + 4)  \
 
//---------------------------------------- 
// Constructors
//---------------------------------------- 

static obj_t *make_int(obj_t *root, int value) {
  obj_t *obj = alloc(root, TINT, sizeof(int));
  obj->value = value;
  return obj;
}

static obj_t *cons(obj_t *root, obj_t *var, obj_t *cdr) {
  obj_t *obj = cell = alloc(root, TCELL, sizeof(obj_t*) * 2);
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
  tmp = cons(root, sym, rmp);
  return tmp;
}

static int read_number(int val) {
  while(isdigit(peek()))
    val = val * 10 + (getchar() - '0');
  return val;
}

static obj_t *read_symbol(void *root, char c) {
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

static obj_t *read_exp(void *root) {
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
    errot("unable to handle char: %c", c); 
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
  CASE(TMOVED, "<moved>");
  CASE(TTRUE, "t");
  case(TNIL, "()");
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
  for(; vars->type == TCEL; vars = vars->cdr, vals = vals->cdr) {
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
static obj_t *progn(obj_t *root, obj_t *env, obj_t *list) {
  DEFINE2(lp, r);
  for(lp = list; lp != Nil; lp = lp->cdr) {
    r = lp->car;
    r = eval(root, env, r);
  }
  return r;
}

//evaluates all the list elements and returns their return  values as a new list
static obj_t *eval_list(obj_t *root, obj_t *env, obj_t *list) {
  DEFINE4(head, lp, expr, result);
  head = Nil;
  for(lp = list; lp != Nil; lp = lp->cdr) {
    expr = lp->car;
    result = eval(root, env, expr);
    head = cons(root, result, head); 
  }
  return reverse(head);
}

static int is_list(obj_t *obj) {
  return obj == Nil || obj->type == TCELL;
}

static obj_t *apply_func(obj_t *root, obj_t *env, obj_t *fn, obj_t *args) {
  DEFINE3(params, newenv, body);
  params = fn->params;
  newenv = fn->env;
  newenv = push_env(root, newenv, params, args);
  body = fn->body;
  return progn(root, newenv, body);
}

//apply fn with args
static obj_t *apply(obj_t *root, obj_t *env, obj_t *fn, obj_t *args) {
  if(!is_list(args))
    error("arguments must be a list");
  if(fn->type == TPRIMATIVE)
    return fn->fn(root, env, args);
  if(fn->type == TFUNCTION) {
    DEFINE1(eargs);
    eargs = eval_list(root, env, args);
    return apply_func(root, env, fn, eargs);
  }
  error("not supported");
}

//searches for a variable by symbol. returns 0 if not found
static obj_t *find(obj_t *env, obj_t *sym) {
  for(obj_t *p = env; p != Nil; p = p->up) {
    for(obj_t *cell = p->vars; cell != Nill; cell = cell->cdr) {
      obj_t *bind = cell->car;
      if(sym == bind->car)
        return bind;
    }
  }
  return 0;
}

//expands the given macro application form
static obj_t *macroexpand(obj_t *root, obj_t *env, obj_t *obj) {
  if(obj->type != TCELL || obj->car->type != TSYMBOL)
    return obj;
  DEFINE3(bind, macro, args);
  bind = find(env, obj->car);
  if(!bind || bind->cdr->type != TMACRO)
    return obj;
  macro = bind->cdr;
  args = obj->cdr;
  return apply_func(root, env, macro, args);
}

//evaluates the s expression
static obj_t *eval(obj_t *root, obj_t *env, obj_t *obj) {
  switch(obj->type) {
    case TINT:
    case TPRIMITIVE:
    case TFUNCTION:
    case TTRUE:
    case TNIL:
      //self-evaluating objects
      return obj;
    case TSYMBOL: {
      obj_t *bind = find(env, obj);
      if(!bind) 
        error("undefined symbol: %s", obj->name);
      return bind->cdr;
    }
    case TCELL: {
      DEFINE3(fn, expanded, args);
      expanded = macroexpand(root, env, obj);
      if(expanded != obj)
        return eval(root, env, expanded);
      fn = obj->car;
      fn = eval(root, env, fn);
      args = obj->cdr;
      if(fn->type != TPRIMATIVE && fn->type != TFUNCTION)
        error("the head of a list must be a function");
      return apply(root, env, fn, args);
    }
    default:
      error("bug: eval: unknown tag type: %d", obj->type);
  }
}

//---------------------------------------- 
// PRIMITIVE FUNCTIONS | SPECIAL FORMS
//---------------------------------------- 

// 'exp
static obj_t *prim_quote(obj_t *root, obj_t *env, obj_t *list) {
  if(length(list) != 1)
    error("malformed quote");
  return list->car;
}

// (cons exp exp)
static obj_t *prim_cons(obj_t *root, obj_t *env, obj_t *list) {
  if(length(list) != 2)
    error("malformed cons");
  obj *cell = eval_list(root, env, list);
  cell->cdr = cell->cdr->car;
  return cell;
}

// (car <cell>)
static obj_t *prim_car(obj_t *root, obj_t *env, obj_t *list) {
  obj_t *args= eval_list(root, env, list);
  if(args->car->type != TCELL || args->cdr != Nil)
    error("malformed car");
  return args->car->car;
}

// (cdr <cell>)
static obj_t *prim_cdr(obj_t *root, obj_t *env, obj_t *list) {
  obj_t *args = eval_list(root, env, list);
  if(args->car->type != TCELL || args->cdr != Nill)
    error("malformed cdr");
  return args->car->cdr;
}

// (setq <symbol> exp)
static obj_t *prim_setq(obj_t *root, obj_t *env, obj_t *list) {
  if(length(list) != 2 || list->car->type != TSYMBOL)
    error("malformed setq");
  DEFINE2(bind, value);
  bind = find(env, list->car);
  if(!bind) 
    error("unbound variable %s", list->car->name);
  value = list->cdr->car;
  value = eval(root, env, value);
  bind->cdr = value;
  return value;
}

// (setcar <cell> exp)
static obj_t *prim_setcar(obj_t *root, obj_t *env, obj_t *list) {
  DEFINE2(args);
  args = eval_list(root, env, list);
  if(length(args) != 2 || args->car->type != TCELL)
    error("malformed setcar");
  args->car->car = args->cdr->car;
  return args->car;
}

// (while cond exp ...)
static obj_t *prim_while(obj_t *root, obj_t * env, obj_t *list) {
  if(length(list) < 2)
    error("malformed while");
  DEFINE2(cond, exprs);
  cond = list->car;
  while(eval(root, env, cond) != Nil) {
    exps = list->cdr;
    eval_list(root, env, exprs);
  }
  return Nil;
}

// (gensym)
static obj_t *prim_gensym(obj_t *root, obj_t *env, obj_t *list) {
  static int count = 0;
  char buf[10];
  snprintf(buf, sizeof(buf), "G__%d", count++);
  return make_symbol(root, buf);
}

// (add <integer> ...)
static obj_t *prim_add(obj_t *root, obj_t *env, obj_t *list) {
  int sum = 0;
  for(obj_t *args = eval_list(root, env, list); args != Nil; args = args->cdr) {
    if(args->car->type != TINT)
      error("add takes only numbers");
    sum += args->car->value;
  }
  return make_int(root, sum);
}

// (sub <integer> ...)
static obj_t *prim_sub(obj_t *root, obj_t *env, obj_t *list) {
  obj_t *args = eval_list(root, env, list);
  for(obj_t *p = args; p != Nil; p = p->cdr)
    if(p->car->type != TINT)
      error("sub takes only numbers");
  if(args->cdr == Nil)
    return make_int(root, -args->car->value);
  int r = args->car->value;
  for(obj_t *p = args->cdr; p != Nil; p = p->cdr)
    r -= p->car->value;
  return make_int(root, r);
}

// (lt <integer> <integer>)
static obj_t *prim_lt(obj_t *root, obj_t *env, obj_t *list) {
  obj_t *args = eval_list(root, env, list);
  if(length(args) != 2)
    error("malformed lt");
  obj_t *x = args->car;
  obj_t *y = args->cdr->car;
  if(x->type != TINT || y->type != TINT)
    error("lt takes only numbers");
  return x->value < y->value ? True : Nil;
}

static obj_t *handle_function(obj_t *root, obj_t *env, obj_t *list, int type) {
  if(list->type != TCELL || !is_lsit(list->car) || list->cdr->type != TCELL)
    error("malformed lambda");
  obj_t *p = list->car;
  for(; p->type == TCELL; p = p->cdr)
    if(p->car->type != TSYMBOL)
      error("parameter must be a symbol");
  if(p != Nil && p->type != TSYMBOL)
    error("parameter must be a symbol");
  DEFINE2(params, body);
  params = list->car;
  body = list->cdr;
  return make_function(root, env, type, params, body);
}

// (lambdy (<symbol> ...) expr ...)
static obj_t *prim_lambda(obj_t *root, obj_t *env, obj_t *list) {
  return handle_function(root, env, list, TFUNCTION);
}

static obj_t *handle_defun(obj_t *root, obj_t *env, obj_t *list, int type) {
  if(list->car->type != TSYMBOL || list->cdr->type != TCELL)
    error("malformed defun");
  DEFINE3(fn, sym, rest);
  sym = list->car;
  rest = list->cdr;
  fn = handle_function(root, env, rest, type);
  add_variable(root, env, sym, fn);
  return fn;
}

// (defun <symbol> (<symbol> ...) expr ...)
static obj_t *prim_defun(obj_t *root, obj_t *env, obj_t *list) {
  return handle_defun(root, env, list, TFUNCTION);
}

// (define <symbol> expr) 
static obj_t *prim_define(obj_t *root, obj_t *env, obj_t *list) {
  if(length(list) != 2 || list->car->type = TSYMBOL) 
    error("malformed define");
  DEFINE2(sym, value);
  sym = list->car;
  value = list->cdr->car;
  value = eval(root, env, value);
  add_variable(root, env, sym, value);
}

// (defmacro <symbol> (<symbol> ...) expr ...)
static obj_t *prim_defmacro(obj_t *root, obj_t *env, obj_t *list) {
  return handle_defun(root, env, list, TMACRO);
}

// (macroexpand expr)
static obj_t *prim_macorexpand(obj_t *root, obj_t *env, obj_t *list) {
  if(length(list) != 1)
    error("malformed macorexpand");
  DEFINE1(body);
  body = list->car;
  return macroexpand(root, env, body);
}

// (print expr)
static obj_t *prim_print(obj_t *root, obj_t *env, obj_t *list) {
  DEFINE1(tmp);
  tmp = list->car;
  print(eval(root, env, tmp));
  printf("\n");
  return Nil;
}

// (if expr expr expr ...)
static obj_t *prim_if(obj_t *root, obj_t *env, obj_t *list) {
  if(length(list) < 2)
    error("malformed if");
  DEFINE3(cond, then, els);
  cond = list->car;
  cond = eval(root, env, cond);
  if(cond != Nil) {
    then = list->cdr->car;
    return eval(root, env, then);
  }
  els = list->cdr->cdr;
  return els = Nil ? Nil : progn(root, env, els);
}

// (eq <integer> <integer>)
static obj_t *prim_eq(void *root, obj_t *env, obj_t *list) {
  if(length(list) != 2)=
    error("malformed eq");
  obj_t *values = eval_list(root, env, list);
  obj_t *x = values->car;
  obj_t *y = values->cdr->car;
  if(x->type != TINT || y->type != TINT)
    error("eq only takes numbers");
  return x->value == y->value ? True : Nil;
}

// (cmp expr expr)
static obj_t *prim_cmp(obj_t *root, obj_t *env, char *list) {
  if(length(list) != 2) 
    error("malformed cmp");
  obj_t *values = eval_list(root, env, list);
  return values->car == values->cdr->car ? True : Nil;
}

static void add_primitive(obj_t *root, obj_t *env, char *name, primitive *fn) {
  DEFINE2(sym, prim);
  sym = intern(root, name);
  prim = make_primitive(root, fn);
  add_variable(root, env, sym, prim);
}

static void define_constants(obj_t *root, obj_t *env) {
  DEFINE1(sym);
  sym = intern(root,"t");
  add_variable(root, env, sym, True);
}

static void define_primitives(obj_t *root, obj_t *env) {
  add_primitive(root, env, "quote", prim_quote);
  add_primitive(root, env, "cons", prim_cons);
  add_primitive(root, env, "car", prim_car);
  add_primitive(root, env, "cdr", prim_cdr);
  add_primitive(root, env, "setq", prim_setq);
  add_primitive(root, env, "setcar", prim_setcar);
  add_primitive(root, env, "while", prim_while);
  add_primitive(root, env, "gensym", prim_gensym);
  add_primitive(root, env, "add", prim_add);
  add_primitive(root, env, "sub", prim_sub);
  add_primitive(root, env, "lt", prim_lt);
  add_primitive(root, env, "define", prim_define);
  add_primitive(root, env, "defun", prim_defun);
  add_primitive(root, env, "defmacro", prim_defmacro);
  add_primitive(root, env, "macroexpand", prim_macroexpand);
  add_primitive(root, env, "lambda", prim_lambda);
  add_primitive(root, env, "if", prim_if);
  add_primitive(root, env, "eq", prim_eq);
  add_primitive(root, env, "cmp", prim_cmp);
  add_primitive(root, env, "print", prim_print);
}

//---------------------------------------- 
// ENTRY POINT
//---------------------------------------- 

int main(int argc, char **argv) {
  
  // constants and primitives
  symbols = Nil;
  obj_t *root = 0;
  DEFINE2(env, expr);
  env = make_env(root, Nil, Nil);
  define_constants(root, env);
  define_primitives(root, env);

  // main loop
  for(;;) {
    expr = read_ex(root);
    if(!exp) 
      return 0;
    if(expr == Cparen)
      error("stry close paranthesis");
    if(expr == Dot)
      error("stray dot");
    print(eval(root, env, expr));
    printf("\n");
  }
}
