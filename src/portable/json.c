/* Chess Online: small JSON reader. Original code; see json.h. */
#include "json.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *s, *end;
    char *pool;   /* one block: nodes from the front, strings from the back */
    size_t cap, front, back;
    int depth;
} parser;

static void *take_node(parser *p)
{
    size_t need = (sizeof(jval) + 7) & ~(size_t)7;
    void *r;
    if (p->front + need > p->back) return NULL;
    r = p->pool + p->front;
    p->front += need;
    memset(r, 0, sizeof(jval));
    return r;
}

static char *take_str(parser *p, size_t n)
{
    if (p->back < p->front + n) return NULL;
    p->back -= n;
    return p->pool + p->back;
}

static void ws(parser *p)
{
    while (p->s < p->end && (*p->s == ' ' || *p->s == '\t' || *p->s == '\n' || *p->s == '\r')) p->s++;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int utf8_put(char *o, unsigned cp)
{
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static int read4(const char *s, unsigned *out)
{
    int i, v;
    *out = 0;
    for (i = 0; i < 4; i++) {
        v = hexval(s[i]);
        if (v < 0) return -1;
        *out = (*out << 4) | (unsigned)v;
    }
    return 0;
}

static const char *parse_string(parser *p)
{
    const char *start, *q;
    size_t raw = 0;
    char *out, *o;
    if (p->s >= p->end || *p->s != '"') return NULL;
    start = ++p->s;
    for (q = start; q < p->end && *q != '"'; q++) {
        if (*q == '\\') q++;
        else if ((unsigned char)*q < 0x20) return NULL;
    }
    if (q >= p->end) return NULL;
    raw = (size_t)(q - start);
    out = take_str(p, raw + 1); /* unescaped text is never longer than the escaped text */
    if (!out) return NULL;
    o = out;
    for (q = start; *q != '"'; q++) {
        if (*q != '\\') { *o++ = *q; continue; }
        q++;
        switch (*q) {
        case '"': *o++ = '"'; break;
        case '\\': *o++ = '\\'; break;
        case '/': *o++ = '/'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'n': *o++ = '\n'; break;
        case 'r': *o++ = '\r'; break;
        case 't': *o++ = '\t'; break;
        case 'u': {
            unsigned cp, lo;
            if (q + 4 >= p->end || read4(q + 1, &cp)) return NULL;
            q += 4;
            if (cp >= 0xD800 && cp < 0xDC00 && q + 6 < p->end && q[1] == '\\' && q[2] == 'u' && !read4(q + 3, &lo) &&
                lo >= 0xDC00 && lo < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                q += 6;
            } else if (cp >= 0xD800 && cp < 0xE000) {
                cp = 0xFFFD;
            }
            o += utf8_put(o, cp);
            break;
        }
        default: return NULL;
        }
    }
    *o = 0;
    p->s = q + 1;
    return out;
}

static jval *parse_value(parser *p);

static jval *parse_container(parser *p, int obj)
{
    jval *v = take_node(p), *last = NULL;
    char close = obj ? '}' : ']';
    if (!v) return NULL;
    v->t = obj ? J_OBJ : J_ARR;
    p->s++;
    if (++p->depth > 64) return NULL;
    ws(p);
    if (p->s < p->end && *p->s == close) {
        p->s++;
        p->depth--;
        return v;
    }
    for (;;) {
        const char *key = NULL;
        jval *item;
        ws(p);
        if (obj) {
            key = parse_string(p);
            if (!key) return NULL;
            ws(p);
            if (p->s >= p->end || *p->s != ':') return NULL;
            p->s++;
        }
        item = parse_value(p);
        if (!item) return NULL;
        item->key = key;
        if (last) last->next = item;
        else v->child = item;
        last = item;
        ws(p);
        if (p->s >= p->end) return NULL;
        if (*p->s == ',') { p->s++; continue; }
        if (*p->s == close) { p->s++; break; }
        return NULL;
    }
    p->depth--;
    return v;
}

static jval *parse_value(parser *p)
{
    jval *v;
    ws(p);
    if (p->s >= p->end) return NULL;
    switch (*p->s) {
    case '{': return parse_container(p, 1);
    case '[': return parse_container(p, 0);
    case '"':
        v = take_node(p);
        if (!v) return NULL;
        v->t = J_STR;
        v->str = parse_string(p);
        return v->str ? v : NULL;
    case 't':
    case 'f':
    case 'n': {
        static const char *words[] = {"true", "false", "null"};
        static const jtype types[] = {J_TRUE, J_FALSE, J_NULL};
        int i;
        for (i = 0; i < 3; i++) {
            size_t n = strlen(words[i]);
            if ((size_t)(p->end - p->s) >= n && memcmp(p->s, words[i], n) == 0) {
                v = take_node(p);
                if (!v) return NULL;
                v->t = types[i];
                p->s += n;
                return v;
            }
        }
        return NULL;
    }
    default: {
        char buf[64];
        size_t n = 0;
        char *endp;
        while (p->s + n < p->end && n < sizeof buf - 1 && strchr("+-0123456789.eE", p->s[n])) n++;
        if (!n) return NULL;
        memcpy(buf, p->s, n);
        buf[n] = 0;
        v = take_node(p);
        if (!v) return NULL;
        v->t = J_NUM;
        v->num = strtod(buf, &endp);
        if (endp != buf + n) return NULL;
        p->s += n;
        return v;
    }
    }
}

jval *json_parse(const char *text, size_t len)
{
    parser p;
    jval *root;
    memset(&p, 0, sizeof p);
    /* every value takes at least one character plus a separator, so len / 2 + 2 nodes always fit;
     * unescaped strings never exceed len bytes */
    p.cap = (len / 2 + 2) * ((sizeof(jval) + 7) & ~(size_t)7) + len + 64;
    p.pool = (char *)malloc(p.cap);
    if (!p.pool) return NULL;
    p.back = p.cap;
    p.s = text;
    p.end = text + len;
    root = parse_value(&p);
    if (root) {
        ws(&p);
        if (p.s != p.end) root = NULL;
    }
    if (!root) {
        free(p.pool);
        return NULL;
    }
    return root; /* the root is the first node, at the start of the pool */
}

void json_free(jval *root)
{
    free(root);
}

const jval *json_get(const jval *obj, const char *key)
{
    const jval *c;
    if (!obj || obj->t != J_OBJ) return NULL;
    for (c = obj->child; c; c = c->next)
        if (c->key && strcmp(c->key, key) == 0) return c;
    return NULL;
}

const jval *json_path(const jval *obj, const char *path)
{
    char part[64];
    while (obj && *path) {
        size_t n = strcspn(path, ".");
        if (n >= sizeof part) return NULL;
        memcpy(part, path, n);
        part[n] = 0;
        obj = json_get(obj, part);
        path += n;
        if (*path == '.') path++;
    }
    return obj;
}

const char *json_str(const jval *obj, const char *path, const char *def)
{
    const jval *v = json_path(obj, path);
    return v && v->t == J_STR ? v->str : def;
}

double json_num(const jval *obj, const char *path, double def)
{
    const jval *v = json_path(obj, path);
    return v && v->t == J_NUM ? v->num : def;
}

int json_bool(const jval *obj, const char *path, int def)
{
    const jval *v = json_path(obj, path);
    if (!v) return def;
    if (v->t == J_TRUE) return 1;
    if (v->t == J_FALSE) return 0;
    return def;
}
