/* Chess Online: small JSON reader (original code). Parses one document into a tree that lives in one
 * malloc'd block; free it with json_free. Strings are unescaped to UTF-8. */
#ifndef CO_JSON_H
#define CO_JSON_H

#include <stddef.h>

typedef enum { J_NULL, J_FALSE, J_TRUE, J_NUM, J_STR, J_ARR, J_OBJ } jtype;

typedef struct jval {
    jtype t;
    double num;
    const char *str;    /* J_STR */
    const char *key;    /* set when the value is an object member */
    struct jval *child; /* first element / member */
    struct jval *next;  /* next sibling */
} jval;

/* Returns the root, or NULL if the text is not valid JSON. */
jval *json_parse(const char *text, size_t len);
void json_free(jval *root);

const jval *json_get(const jval *obj, const char *key);
/* Dotted path lookup through objects, e.g. "game.opponent.username". */
const jval *json_path(const jval *obj, const char *path);
const char *json_str(const jval *obj, const char *path, const char *def);
double json_num(const jval *obj, const char *path, double def);
int json_bool(const jval *obj, const char *path, int def);

#endif
