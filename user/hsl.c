#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <hexos.h>

#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_HOME  0x84
#define KEY_END   0x85
#define KEY_DEL   0x86

/* ===== Tokenizer ===== */

enum {
    T_EOF = 0, T_NUM, T_STR, T_IDENT,
    T_ASSIGN, T_PLUS, T_MINUS, T_STAR, T_SLASH,
    T_LPAREN, T_RPAREN, T_SEMI,
    T_DOT, T_LBRACK, T_RBRACK,
    T_PIPE, T_LBRACE, T_RBRACE,
    T_LT, T_GT, T_LE, T_GE, T_EQ, T_NE,
};

typedef struct {
    int  type;
    long num;
    char str[128];
} token_t;

#define MAX_TOKENS 256

static token_t g_tokens[MAX_TOKENS];
static int     g_ntok;
static int     g_tok_idx;

static void lex_push(int type, long num, const char* str) {
    if (g_ntok >= MAX_TOKENS - 1) return;
    token_t* t = &g_tokens[g_ntok++];
    t->type = type;
    t->num  = num;
    if (str) {
        strncpy(t->str, str, 127);
        t->str[127] = 0;
    } else {
        t->str[0] = 0;
    }
}

static void tokenize(const char* src) {
    g_ntok = 0;
    g_tok_idx = 0;
    int i = 0;

    while (src[i] && g_ntok < MAX_TOKENS - 1) {
        char c = src[i];

        if (c == ' ' || c == '\t') { i++; continue; }

        if (c == '=') {
            if (src[i + 1] == '=') { lex_push(T_EQ, 0, 0); i += 2; }
            else                   { lex_push(T_ASSIGN, 0, 0); i++; }
            continue;
        }
        if (c == '!') {
            if (src[i + 1] == '=') { lex_push(T_NE, 0, 0); i += 2; }
            else                   { i++; }
            continue;
        }
        if (c == '<') {
            if (src[i + 1] == '=') { lex_push(T_LE, 0, 0); i += 2; }
            else                   { lex_push(T_LT, 0, 0); i++; }
            continue;
        }
        if (c == '>') {
            if (src[i + 1] == '=') { lex_push(T_GE, 0, 0); i += 2; }
            else                   { lex_push(T_GT, 0, 0); i++; }
            continue;
        }

        if (c == '+') { lex_push(T_PLUS,   0, 0); i++; continue; }
        if (c == '-') { lex_push(T_MINUS,  0, 0); i++; continue; }
        if (c == '*') { lex_push(T_STAR,   0, 0); i++; continue; }
        if (c == '/') { lex_push(T_SLASH,  0, 0); i++; continue; }
        if (c == '(') { lex_push(T_LPAREN, 0, 0); i++; continue; }
        if (c == ')') { lex_push(T_RPAREN, 0, 0); i++; continue; }
        if (c == ';') { lex_push(T_SEMI,   0, 0); i++; continue; }
        if (c == '.') { lex_push(T_DOT,    0, 0); i++; continue; }
        if (c == '[') { lex_push(T_LBRACK, 0, 0); i++; continue; }
        if (c == ']') { lex_push(T_RBRACK, 0, 0); i++; continue; }
        if (c == '|') { lex_push(T_PIPE,   0, 0); i++; continue; }
        if (c == '{') { lex_push(T_LBRACE, 0, 0); i++; continue; }
        if (c == '}') { lex_push(T_RBRACE, 0, 0); i++; continue; }

        if (c == '"') {
            i++;
            char buf[128];
            int  n = 0;
            while (src[i] && src[i] != '"') {
                if (n < 127) buf[n++] = src[i];
                i++;
            }
            buf[n] = 0;
            if (src[i] == '"') i++;
            lex_push(T_STR, 0, buf);
            continue;
        }

        if (c >= '0' && c <= '9') {
            long v = 0;
            while (src[i] >= '0' && src[i] <= '9') {
                v = v * 10 + (src[i] - '0');
                i++;
            }
            lex_push(T_NUM, v, 0);
            continue;
        }

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            char buf[128];
            int  n = 0;
            while ((src[i] >= 'a' && src[i] <= 'z') ||
                   (src[i] >= 'A' && src[i] <= 'Z') ||
                   (src[i] >= '0' && src[i] <= '9') ||
                    src[i] == '_') {
                if (n < 127) buf[n++] = src[i];
                i++;
            }
            buf[n] = 0;
            lex_push(T_IDENT, 0, buf);
            continue;
        }

        break;
    }

    lex_push(T_EOF, 0, 0);
}

static token_t* cur(void)  { return &g_tokens[g_tok_idx]; }

static token_t* peek(int n) {
    int idx = g_tok_idx + n;
    if (idx >= g_ntok) idx = g_ntok - 1;
    return &g_tokens[idx];
}

static void advance(void) {
    if (g_tok_idx < g_ntok - 1) g_tok_idx++;
}

/* ===== AST ===== */

enum {
    N_NUM, N_STR, N_IDENT, N_ASSIGN, N_BINOP, N_CALL,
    N_MEMBER, N_INDEX, N_SELF, N_LAMBDA, N_PIPE
};

enum {
    OP_ADD = 1, OP_SUB, OP_MUL, OP_DIV,
    OP_LT, OP_GT, OP_LE, OP_GE, OP_EQ, OP_NE,
};

typedef struct node {
    int    type;
    long   num;
    char   str[128];
    int    op;
    struct node* a;
    struct node* b;
} node_t;

static node_t* node_new(int type) {
    node_t* n = (node_t*)malloc(sizeof(node_t));
    if (!n) return 0;
    memset(n, 0, sizeof(node_t));
    n->type = type;
    return n;
}

static node_t* parse_pipeline(void);

static int is_builtin_cmd(const char* s) {
    return strcmp(s, "ls") == 0 ||
           strcmp(s, "count") == 0 ||
           strcmp(s, "where") == 0 ||
           strcmp(s, "map") == 0 ||
           strcmp(s, "sort") == 0;
}

static node_t* parse_primary(void) {
    token_t* t = cur();
    node_t*  n;

    if (t->type == T_NUM) {
        n = node_new(N_NUM);
        if (!n) return 0;
        n->num = t->num;
        advance();
        return n;
    }
    if (t->type == T_STR) {
        n = node_new(N_STR);
        if (!n) return 0;
        strncpy(n->str, t->str, 127);
        advance();
        return n;
    }
    if (t->type == T_DOT) {
        if (peek(1)->type != T_IDENT) advance();
        return node_new(N_SELF);
    }
    if (t->type == T_LBRACE) {
        advance();
        node_t* body = parse_pipeline();
        if (cur()->type == T_RBRACE) advance();
        n = node_new(N_LAMBDA);
        if (!n) return 0;
        n->a = body;
        return n;
    }
    if (t->type == T_IDENT) {
        char name[128];
        strncpy(name, t->str, 127);
        name[127] = 0;
        advance();

        if (is_builtin_cmd(name)) {
            node_t* call = node_new(N_CALL);
            if (!call) return 0;
            strncpy(call->str, name, 127);

            if (strcmp(name, "count") == 0 &&
                (cur()->type == T_EOF || cur()->type == T_SEMI ||
                 cur()->type == T_PIPE || cur()->type == T_RBRACE)) {
                return call;
            }

            call->a = parse_primary();
            return call;
        }

        n = node_new(N_IDENT);
        if (!n) return 0;
        strncpy(n->str, name, 127);
        return n;
    }
    if (t->type == T_LPAREN) {
        advance();
        n = parse_pipeline();
        if (cur()->type == T_RPAREN) advance();
        return n;
    }
    return 0;
}

static node_t* parse_postfix(void) {
    node_t* n = parse_primary();
    while (n) {
        if (cur()->type == T_DOT) {
            advance();
            if (cur()->type != T_IDENT) break;
            node_t* m = node_new(N_MEMBER);
            if (!m) return n;
            m->a = n;
            strncpy(m->str, cur()->str, 127);
            advance();
            n = m;
        } else if (cur()->type == T_LBRACK) {
            advance();
            node_t* idx = parse_pipeline();
            if (cur()->type == T_RBRACK) advance();
            node_t* i = node_new(N_INDEX);
            if (!i) return n;
            i->a = n;
            i->b = idx;
            n = i;
        } else {
            break;
        }
    }
    return n;
}

static node_t* parse_term(void) {
    node_t* n = parse_postfix();
    while (n && (cur()->type == T_STAR || cur()->type == T_SLASH)) {
        int op = (cur()->type == T_STAR) ? OP_MUL : OP_DIV;
        advance();
        node_t* r = parse_postfix();
        node_t* b = node_new(N_BINOP);
        if (!b) return n;
        b->op = op;
        b->a  = n;
        b->b  = r;
        n = b;
    }
    return n;
}

static node_t* parse_expr(void) {
    node_t* n = parse_term();
    while (n && (cur()->type == T_PLUS || cur()->type == T_MINUS)) {
        int op = (cur()->type == T_PLUS) ? OP_ADD : OP_SUB;
        advance();
        node_t* r = parse_term();
        node_t* b = node_new(N_BINOP);
        if (!b) return n;
        b->op = op;
        b->a  = n;
        b->b  = r;
        n = b;
    }
    return n;
}

static node_t* parse_compare(void) {
    node_t* n = parse_expr();
    while (n && cur()->type >= T_LT && cur()->type <= T_NE) {
        int op;
        switch (cur()->type) {
            case T_LT: op = OP_LT; break;
            case T_GT: op = OP_GT; break;
            case T_LE: op = OP_LE; break;
            case T_GE: op = OP_GE; break;
            case T_EQ: op = OP_EQ; break;
            case T_NE: op = OP_NE; break;
            default:   op = OP_EQ; break;
        }
        advance();
        node_t* r = parse_expr();
        node_t* b = node_new(N_BINOP);
        if (!b) return n;
        b->op = op;
        b->a  = n;
        b->b  = r;
        n = b;
    }
    return n;
}

static node_t* parse_pipe_target(void) {
    if (cur()->type != T_IDENT) return 0;

    char name[128];
    strncpy(name, cur()->str, 127);
    name[127] = 0;
    advance();

    node_t* call = node_new(N_CALL);
    if (!call) return 0;
    strncpy(call->str, name, 127);

    if (strcmp(name, "sort") == 0) {
        if (cur()->type == T_IDENT && strcmp(cur()->str, "by") == 0) {
            advance();
            node_t* body = parse_expr();
            node_t* lam  = node_new(N_LAMBDA);
            if (!lam) return call;
            lam->a = body;
            call->a = lam;
        }
        return call;
    }

    if (strcmp(name, "count") == 0) {
        if (cur()->type == T_EOF || cur()->type == T_SEMI ||
            cur()->type == T_PIPE || cur()->type == T_RBRACE) {
            return call;
        }
        call->a = parse_primary();
        return call;
    }

    call->a = parse_primary();
    return call;
}

static node_t* parse_pipeline(void) {
    node_t* n = parse_compare();
    while (n && cur()->type == T_PIPE) {
        advance();
        node_t* r = parse_pipe_target();
        node_t* p = node_new(N_PIPE);
        if (!p) return n;
        p->a = n;
        p->b = r;
        n = p;
    }
    return n;
}

static node_t* parse_stmt(void) {
    if (cur()->type == T_IDENT && strcmp(cur()->str, "echo") == 0) {
        advance();
        node_t* arg = parse_pipeline();
        node_t* call = node_new(N_CALL);
        if (!call) return 0;
        strncpy(call->str, "echo", 127);
        call->a = arg;
        return call;
    }

    if (cur()->type == T_IDENT && peek(1)->type == T_ASSIGN) {
        char name[128];
        strncpy(name, cur()->str, 127);
        name[127] = 0;
        advance();
        advance();
        node_t* v = parse_pipeline();
        node_t* a = node_new(N_ASSIGN);
        if (!a) return 0;
        strncpy(a->str, name, 127);
        a->a = v;
        return a;
    }

    return parse_pipeline();
}

/* ===== Values ===== */

enum { V_NIL, V_NUM, V_STR, V_LIST, V_OBJ, V_LAMBDA };

typedef struct obj_field {
    char name[32];
    int  type;
    long num;
    char str[128];
    struct obj_field* next;
} obj_field_t;

typedef struct value {
    int  type;
    long num;
    char str[128];

    int  list_len;
    struct value* list;

    obj_field_t* fields;

    struct node* ast;
} value_t;

static value_t val_num(long v) {
    value_t r; memset(&r, 0, sizeof(r));
    r.type = V_NUM; r.num = v; return r;
}
static value_t val_str(const char* s) {
    value_t r; memset(&r, 0, sizeof(r));
    r.type = V_STR;
    strncpy(r.str, s, 127);
    r.str[127] = 0;
    return r;
}
static value_t val_nil(void) {
    value_t r; memset(&r, 0, sizeof(r));
    r.type = V_NIL; return r;
}
static value_t val_list(int n) {
    value_t r; memset(&r, 0, sizeof(r));
    r.type = V_LIST;
    r.list_len = n;
    r.list = (value_t*)malloc(sizeof(value_t) * (n ? n : 1));
    return r;
}
static value_t val_obj(void) {
    value_t r; memset(&r, 0, sizeof(r));
    r.type = V_OBJ; return r;
}

static void obj_add(value_t* v, const char* name, value_t val) {
    obj_field_t* f = (obj_field_t*)malloc(sizeof(obj_field_t));
    if (!f) return;
    memset(f, 0, sizeof(*f));
    strncpy(f->name, name, 31);
    f->type = val.type;
    f->num  = val.num;
    strncpy(f->str, val.str, 127);
    f->next = v->fields;
    v->fields = f;
}

/* ===== Environment ===== */

typedef struct env_entry {
    char name[64];
    value_t val;
    struct env_entry* next;
} env_entry_t;

typedef struct {
    env_entry_t* head;
    value_t      self;
    int          has_self;
} env_t;

static value_t* env_find(env_t* e, const char* name) {
    for (env_entry_t* p = e->head; p; p = p->next)
        if (strcmp(p->name, name) == 0) return &p->val;
    return 0;
}

static void env_set(env_t* e, const char* name, value_t v) {
    for (env_entry_t* p = e->head; p; p = p->next) {
        if (strcmp(p->name, name) == 0) { p->val = v; return; }
    }
    env_entry_t* n = (env_entry_t*)malloc(sizeof(env_entry_t));
    if (!n) return;
    strncpy(n->name, name, 63);
    n->name[63] = 0;
    n->val  = v;
    n->next = e->head;
    e->head = n;
}

/* ===== Printing ===== */

static void print_num_field(long n) {
    char b[32]; int k = 31;
    b[k] = 0;
    if (n == 0) b[--k] = '0';
    else {
        int neg = n < 0;
        if (neg) n = -n;
        while (n) { b[--k] = '0' + (n % 10); n /= 10; }
        if (neg) b[--k] = '-';
    }
    write(1, &b[k], (size_t)(31 - k));
}

static void print_value(value_t v);

static void print_obj_field(obj_field_t* f) {
    write(1, f->name, strlen(f->name));
    write(1, ": ", 2);
    if (f->type == V_NUM) {
        print_num_field(f->num);
    } else if (f->type == V_STR) {
        write(1, "\"", 1);
        write(1, f->str, strlen(f->str));
        write(1, "\"", 1);
    } else {
        write(1, "?", 1);
    }
}

static void print_obj_inline(value_t v) {
    write(1, "{", 1);
    int first = 1;
    for (obj_field_t* f = v.fields; f; f = f->next) {
        if (!first) write(1, ", ", 2);
        first = 0;
        print_obj_field(f);
    }
    write(1, "}", 1);
}

static void print_value(value_t v) {
    if (v.type == V_NUM) {
        printf("%ld\n", v.num);
    } else if (v.type == V_STR) {
        write(1, v.str, strlen(v.str));
        write(1, "\n", 1);
    } else if (v.type == V_LIST) {
        write(1, "[", 1);
        for (int i = 0; i < v.list_len; i++) {
            if (i) write(1, ", ", 2);
            if (v.list[i].type == V_OBJ) {
                print_obj_inline(v.list[i]);
            } else if (v.list[i].type == V_STR) {
                write(1, "\"", 1);
                write(1, v.list[i].str, strlen(v.list[i].str));
                write(1, "\"", 1);
            } else if (v.list[i].type == V_NUM) {
                print_num_field(v.list[i].num);
            } else {
                write(1, "nil", 3);
            }
        }
        write(1, "]\n", 2);
    } else if (v.type == V_OBJ) {
        print_obj_inline(v);
        write(1, "\n", 1);
    } else if (v.type == V_LAMBDA) {
        write(1, "<lambda>\n", 9);
    } else {
        puts("nil");
    }
}

/* ===== Builtins ===== */

static value_t builtin_ls(const char* path) {
    const char* target = (path && *path) ? path : ".";
    char buf[4096];
    int n = hex_list_dir(target, buf, sizeof(buf));
    if (n < 0) {
        puts("ls: cannot list");
        return val_list(0);
    }

    int count = 0;
    int i = 0;
    while (i < n) {
        i++;
        int namelen = (unsigned char)buf[i++];
        i += namelen;
        count++;
    }

    value_t list = val_list(count);
    if (!list.list) return val_list(0);

    i = 0;
    int idx = 0;
    while (i < n && idx < count) {
        char type = buf[i++];
        int namelen = (unsigned char)buf[i++];
        char name[128];
        int  cp = namelen < 127 ? namelen : 127;
        for (int k = 0; k < cp; k++) name[k] = buf[i + k];
        name[cp] = 0;
        i += namelen;

        value_t obj = val_obj();
        obj_add(&obj, "type", val_str(type == 2 ? "dir" : "file"));
        obj_add(&obj, "name", val_str(name));

        char pbuf[256];
        int  plen = 0;
        if (target[0] == '/' && target[1] == 0) {
            pbuf[plen++] = '/';
        } else if (strcmp(target, ".") != 0) {
            for (int k = 0; target[k] && plen < 250; k++)
                pbuf[plen++] = target[k];
            pbuf[plen++] = '/';
        }
        for (int k = 0; name[k] && plen < 254; k++)
            pbuf[plen++] = name[k];
        pbuf[plen] = 0;

        int fd = open(pbuf, O_RDONLY);
        long sz = -1;
        if (fd >= 0) {
            sz = lseek(fd, 0, SEEK_END);
            close(fd);
        }
        obj_add(&obj, "size", val_num(sz));

        list.list[idx++] = obj;
    }
    return list;
}

/* ===== Evaluator ===== */

static int truthy(value_t v) {
    if (v.type == V_NUM)  return v.num != 0;
    if (v.type == V_STR)  return v.str[0] != 0;
    if (v.type == V_LIST) return v.list_len > 0;
    if (v.type == V_OBJ)  return v.fields != 0;
    return 0;
}

static value_t eval(node_t* n, env_t* e);

static value_t builtin_where(value_t left, node_t* lam, env_t* e) {
    if (left.type != V_LIST) return val_list(0);
    if (!lam || lam->type != N_LAMBDA) return val_list(0);

    int n = left.list_len;
    value_t out = val_list(n);
    if (!out.list) return val_list(0);

    env_t local = *e;
    int j = 0;
    for (int i = 0; i < n; i++) {
        local.self     = left.list[i];
        local.has_self = 1;
        value_t v = eval(lam->a, &local);
        if (truthy(v)) out.list[j++] = left.list[i];
    }
    out.list_len = j;
    return out;
}

static value_t builtin_map(value_t left, node_t* lam, env_t* e) {
    if (left.type != V_LIST) return val_list(0);
    if (!lam || lam->type != N_LAMBDA) return val_list(0);

    int n = left.list_len;
    value_t out = val_list(n);
    if (!out.list) return val_list(0);

    env_t local = *e;
    for (int i = 0; i < n; i++) {
        local.self     = left.list[i];
        local.has_self = 1;
        out.list[i] = eval(lam->a, &local);
    }
    return out;
}

static value_t builtin_sort(value_t left, node_t* lam, env_t* e) {
    if (left.type != V_LIST) return val_list(0);
    if (!lam || lam->type != N_LAMBDA) return val_list(0);

    int n = left.list_len;
    if (n <= 0) return val_list(0);

    long* keys = (long*)malloc(sizeof(long) * n);
    if (!keys) return val_list(0);

    env_t local = *e;
    for (int i = 0; i < n; i++) {
        local.self     = left.list[i];
        local.has_self = 1;
        value_t v = eval(lam->a, &local);
        keys[i] = (v.type == V_NUM) ? v.num : 0;
    }

    value_t out = val_list(n);
    for (int i = 0; i < n; i++) out.list[i] = left.list[i];

    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1 - i; j++) {
            if (keys[j] > keys[j + 1]) {
                long kt = keys[j]; keys[j] = keys[j + 1]; keys[j + 1] = kt;
                value_t vt = out.list[j];
                out.list[j] = out.list[j + 1];
                out.list[j + 1] = vt;
            }
        }
    }

    free(keys);
    return out;
}

static long cmp_val(value_t l, value_t r, int op) {
    long lv = 0, rv = 0;
    if (l.type == V_NUM) lv = l.num;
    if (r.type == V_NUM) rv = r.num;

    if (l.type == V_STR && r.type == V_STR) {
        int c = strcmp(l.str, r.str);
        switch (op) {
            case OP_EQ: return c == 0;
            case OP_NE: return c != 0;
            case OP_LT: return c <  0;
            case OP_GT: return c >  0;
            case OP_LE: return c <= 0;
            case OP_GE: return c >= 0;
        }
    }

    switch (op) {
        case OP_EQ: return lv == rv;
        case OP_NE: return lv != rv;
        case OP_LT: return lv <  rv;
        case OP_GT: return lv >  rv;
        case OP_LE: return lv <= rv;
        case OP_GE: return lv >= rv;
    }
    return 0;
}

static value_t eval(node_t* n, env_t* e) {
    if (!n) return val_nil();

    switch (n->type) {
    case N_NUM: return val_num(n->num);
    case N_STR: return val_str(n->str);

    case N_SELF:
        return e->has_self ? e->self : val_nil();

    case N_LAMBDA: {
        value_t v = val_nil();
        v.type = V_LAMBDA;
        v.ast  = n->a;
        return v;
    }

    case N_IDENT: {
        value_t* v = env_find(e, n->str);
        return v ? *v : val_nil();
    }

    case N_ASSIGN: {
        value_t v = eval(n->a, e);
        env_set(e, n->str, v);
        return v;
    }

    case N_BINOP: {
        value_t l = eval(n->a, e);
        value_t r = eval(n->b, e);

        if (n->op == OP_ADD && l.type == V_STR && r.type == V_STR) {
            char tmp[512];
            strncpy(tmp, l.str, 255);
            tmp[255] = 0;
            strcat(tmp, r.str);
            return val_str(tmp);
        }

        if (n->op >= OP_LT && n->op <= OP_NE) {
            return val_num(cmp_val(l, r, n->op));
        }

        long lv = (l.type == V_NUM) ? l.num : 0;
        long rv = (r.type == V_NUM) ? r.num : 0;
        long res = 0;
        switch (n->op) {
            case OP_ADD: res = lv + rv; break;
            case OP_SUB: res = lv - rv; break;
            case OP_MUL: res = lv * rv; break;
            case OP_DIV: res = rv ? lv / rv : 0; break;
        }
        return val_num(res);
    }

    case N_MEMBER: {
        value_t v = eval(n->a, e);
        if (v.type == V_OBJ) {
            for (obj_field_t* f = v.fields; f; f = f->next) {
                if (strcmp(f->name, n->str) == 0) {
                    if (f->type == V_NUM) return val_num(f->num);
                    if (f->type == V_STR) return val_str(f->str);
                    return val_nil();
                }
            }
            return val_nil();
        }
        if (v.type == V_LIST && strcmp(n->str, "len") == 0) {
            return val_num(v.list_len);
        }
        return val_nil();
    }

    case N_INDEX: {
        value_t base = eval(n->a, e);
        value_t idx  = eval(n->b, e);
        if (base.type == V_LIST && idx.type == V_NUM) {
            long i = idx.num;
            if (i < 0 || i >= base.list_len) return val_nil();
            return base.list[i];
        }
        if (base.type == V_STR && idx.type == V_NUM) {
            long i = idx.num;
            int  len = (int)strlen(base.str);
            if (i < 0 || i >= len) return val_nil();
            char tmp[2]; tmp[0] = base.str[i]; tmp[1] = 0;
            return val_str(tmp);
        }
        return val_nil();
    }

    case N_PIPE: {
        value_t left = eval(n->a, e);
        if (!n->b || n->b->type != N_CALL) return val_nil();
        node_t* call = n->b;

        if (strcmp(call->str, "where") == 0) {
            return builtin_where(left, call->a, e);
        }
        if (strcmp(call->str, "map") == 0) {
            return builtin_map(left, call->a, e);
        }
        if (strcmp(call->str, "sort") == 0) {
            return builtin_sort(left, call->a, e);
        }
        if (strcmp(call->str, "count") == 0) {
            if (left.type == V_LIST) return val_num(left.list_len);
            if (left.type == V_STR)  return val_num((long)strlen(left.str));
            return val_num(0);
        }
        return val_nil();
    }

    case N_CALL: {
        if (strcmp(n->str, "echo") == 0) {
            value_t v = eval(n->a, e);
            print_value(v);
            return val_nil();
        }
        if (strcmp(n->str, "ls") == 0) {
            value_t p = eval(n->a, e);
            if (p.type != V_STR) return val_list(0);
            return builtin_ls(p.str);
        }
        if (strcmp(n->str, "count") == 0) {
            value_t v = eval(n->a, e);
            if (v.type == V_LIST) return val_num(v.list_len);
            if (v.type == V_STR)  return val_num((long)strlen(v.str));
            return val_num(0);
        }
        return val_nil();
    }
    }

    return val_nil();
}

/* ===== Line editor / history ===== */

#define HIST_SIZE 16
#define LINE_MAX  74

static char g_hist[HIST_SIZE][LINE_MAX + 1];
static int  g_hist_count = 0;

static void out_str(const char* s) { write(1, s, strlen(s)); }

static void hist_push(const char* s) {
    if (!s[0]) return;
    if (g_hist_count > 0 &&
        strcmp(g_hist[g_hist_count - 1], s) == 0) return;

    if (g_hist_count < HIST_SIZE) {
        strncpy(g_hist[g_hist_count], s, LINE_MAX);
        g_hist[g_hist_count][LINE_MAX] = 0;
        g_hist_count++;
    } else {
        for (int i = 0; i < HIST_SIZE - 1; i++) {
            strncpy(g_hist[i], g_hist[i + 1], LINE_MAX);
            g_hist[i][LINE_MAX] = 0;
        }
        strncpy(g_hist[HIST_SIZE - 1], s, LINE_MAX);
        g_hist[HIST_SIZE - 1][LINE_MAX] = 0;
    }
}

static void redraw(const char* buf, int len, int cursor) {
    write(1, "\r", 1);
    out_str("hsl> ");
    for (int i = 0; i < len; i++) write(1, &buf[i], 1);
    for (int i = len; i < LINE_MAX; i++) write(1, " ", 1);
    write(1, "\r", 1);
    out_str("hsl> ");
    for (int i = 0; i < cursor; i++) write(1, &buf[i], 1);
}

static int read_line(char* buf, int max) {
    int len = 0;
    int cursor = 0;
    int pos = g_hist_count;
    char saved[LINE_MAX + 1];
    saved[0] = 0;
    int saved_valid = 0;

    buf[0] = 0;
    out_str("hsl> ");

    for (;;) {
        unsigned char c;
        ssize_t n = read(0, &c, 1);
        if (n <= 0) continue;

        if (c == '\n' || c == '\r') {
            write(1, "\n", 1);
            buf[len] = 0;
            return len;
        }
        if (c == 0x03) { out_str("^C\n"); return -1; }
        if (c == 0x0C) { hex_clear(); redraw(buf, len, cursor); continue; }
        if (c == '\b' || c == 0x7F) {
            if (cursor > 0) {
                for (int i = cursor - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--;
                cursor--;
                redraw(buf, len, cursor);
            }
            continue;
        }
        if (c == KEY_UP) {
            if (g_hist_count == 0) continue;
            if (!saved_valid) {
                for (int i = 0; i <= len; i++) saved[i] = buf[i];
                saved_valid = 1;
            }
            if (pos == 0) continue;
            pos--;
            int hl = (int)strlen(g_hist[pos]);
            for (int i = 0; i < hl; i++) buf[i] = g_hist[pos][i];
            buf[hl] = 0;
            len = hl;
            cursor = len;
            redraw(buf, len, cursor);
            continue;
        }
        if (c == KEY_DOWN) {
            if (pos >= g_hist_count) continue;
            pos++;
            int hl;
            if (pos == g_hist_count) {
                hl = (int)strlen(saved);
                for (int i = 0; i < hl; i++) buf[i] = saved[i];
            } else {
                hl = (int)strlen(g_hist[pos]);
                for (int i = 0; i < hl; i++) buf[i] = g_hist[pos][i];
            }
            buf[hl] = 0;
            len = hl;
            cursor = len;
            redraw(buf, len, cursor);
            continue;
        }
        if (c == KEY_LEFT)  { if (cursor > 0) { cursor--; redraw(buf, len, cursor); } continue; }
        if (c == KEY_RIGHT) { if (cursor < len) { cursor++; redraw(buf, len, cursor); } continue; }
        if (c == KEY_HOME)  { cursor = 0;   redraw(buf, len, cursor); continue; }
        if (c == KEY_END)   { cursor = len; redraw(buf, len, cursor); continue; }
        if (c == KEY_DEL) {
            if (cursor < len) {
                for (int i = cursor; i < len - 1; i++) buf[i] = buf[i + 1];
                len--;
                redraw(buf, len, cursor);
            }
            continue;
        }
        if (c >= ' ' && c < 0x7F && len < max - 1) {
            for (int i = len; i > cursor; i--) buf[i] = buf[i - 1];
            buf[cursor] = (char)c;
            len++;
            cursor++;
            redraw(buf, len, cursor);
        }
    }
}

int main(void) {
    puts("HSL v0.4.1");
    puts("type 'exit' to quit");

    static char line[LINE_MAX + 1];
    env_t env;
    env.head = 0;
    env.has_self = 0;

    for (;;) {
        int n = read_line(line, sizeof(line));
        if (n < 0) continue;
        if (n == 0) continue;

        if (strcmp(line, "exit") == 0) return 0;

        hist_push(line);

        tokenize(line);

        while (cur()->type != T_EOF) {
            node_t* st = parse_stmt();
            if (!st) break;

            int suppress = (st->type == N_ASSIGN) ||
                           (st->type == N_CALL && strcmp(st->str, "echo") == 0);

            value_t v = eval(st, &env);
            if (!suppress) print_value(v);

            if (cur()->type == T_SEMI) advance();
            else if (cur()->type != T_EOF) break;
        }
    }
}
