#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "python.h"

#define DEFAULT_STRING_LEN 256

#define CHAR_IS_EOF(x)         x == 0
#define CHAR_IS_NEWLINE(x)     x == 10
#define CHAR_IS_TAB(x)         x == 13
#define CHAR_IS_SPACE(x)       x == 32
#define CHAR_IS_NUMBER(x)      (x >= 48 && x <= 57)
#define CHAR_IS_IDN(x)         (x >= 65 && x <= 90) || (x >= 97 && x <= 122)
#define CHAR_IS_OPERATOR(x)    x == 33 || (x >= 35 && x <= 38)\
                                  || (x >= 40 && x <= 47)\
                                  || (x >= 58 && x <= 64) || x == 96\
                                  || (x >= 123 && x <= 126)

enum CHTokenType
{
    CH_OPERATOR,
    CH_IDN,
    CH_DSTRING,
    CH_SSTRING,
    CH_NUMBER,
    CH_SPACE,
    CH_TAB,
    CH_NL
};

struct CHList
{
    void *ptr;
    struct CHList *next;
};

struct CHString
{
    char *str;
    size_t size;
};

struct CHToken
{
    enum CHTokenType type;
    struct CHString *value;
};

static struct CHString* createstring(char*, size_t);
static struct CHString* stringcat(char*, struct CHString*);
static void freestring(struct CHString*);
static struct CHList* createlist();
static void listadd(struct CHList*, void*);
static void freelist(struct CHList*);
static void render(struct CHList*);
static struct CHToken* createtoken(int);
static void freetoken(struct CHToken*);
static void freetokenlist(struct CHList*);
static void tokenize(char*);
char* code2html(char*, unsigned char);

static struct CHList *tokens;
static struct CHString *out;

static char buff[1<<25];

static struct CHString* createstring(char* str, size_t len)
{
    struct CHString *s = (struct CHString*)malloc(sizeof(struct CHString));
    s->size = strlen(str) + len;
    s->str = (char*)calloc(s->size, sizeof(char));

    strcat(s->str, str);

    return s;
}

static struct CHString* stringcat(char *from, struct CHString *to)
{
    size_t flen = strlen(from);
    size_t tlen = strlen(to->str);
    struct CHString *ns;

    if (flen + tlen + 1 >= to->size) {
        ns = createstring("", flen + tlen + DEFAULT_STRING_LEN);
        strcat(ns->str, to->str);
        strcat(ns->str, from);
        freestring(to);
        return ns;
    }

    strcat(to->str, from);

    return to;
}

static void freestring(struct CHString *s)
{
    free(s->str);
    free(s);
}

static struct CHList* createlist()
{
    struct CHList *l = (struct CHList*)malloc(sizeof(struct CHList));
    l->ptr = NULL;
    l->next = NULL;
    return l;
}

static void listadd(struct CHList *list, void *ptr)
{
    if (list->next == NULL) {
        if (list->ptr == NULL) {
            list->ptr = ptr;
        } else {
            struct CHList *l = createlist();
            l->ptr = ptr;
            list->next = l;
        }
    } else {
        listadd(list->next, ptr);
    }
}

static void freelist(struct CHList *list)
{
    if (list->next != NULL) {
        freelist(list->next);
    }
    free(list);
}

static int iskeyword(char *keywords[], size_t size, char *key)
{
    int i = 0;
    for (; i < size; i++)
        if (strcmp(keywords[i], key) == 0)
            return 1;
    return 0;
}

static void rendercolor(char *type, char *val)
{
    out = stringcat("<span class=\"code-", out);
    out = stringcat(type, out);
    out = stringcat("\">", out);
    out = stringcat(val, out);
    out = stringcat("</span>", out);
}

static void render(struct CHList *list)
{
    struct CHToken *t;
    struct CHList *next = list;
    unsigned char nl = 0;
    unsigned int i = 0;

    out = stringcat("<pre><code class=\"code-highlight\">", out);
    while (next != NULL) {
        if (next->ptr != NULL) {
            t = (struct CHToken*)next->ptr;

            switch (t->type) {
                case CH_IDN:
                    if (iskeyword(python_keywords, 11, t->value->str))
                        rendercolor("keyword", t->value->str);
                    else
                        rendercolor("misc", t->value->str);
                    break;
                case CH_OPERATOR:
                    rendercolor("operator", t->value->str);
                    break;
                case CH_DSTRING:
                    rendercolor("string", t->value->str);
                    break;
                case CH_SSTRING:
                    rendercolor("string", t->value->str);
                    break;
                case CH_NUMBER:
                    rendercolor("number", t->value->str);
                    break;
                case CH_SPACE:
                    out = stringcat(" ", out);
                    break;
                case CH_TAB:
                    for (i = 0; i < 4; i++)
                        out = stringcat(" ", out);
                    break;
                case CH_NL:
                    out = stringcat("\n", out);
                    break;
            }
        }

        next = next->next;
    }
    out = stringcat("</code></pre>", out);
}

static struct CHToken* createtoken(int type)
{
    struct CHToken *t = (struct CHToken*)malloc(sizeof(struct CHToken));

    t->type = type;
    t->value = createstring("", DEFAULT_STRING_LEN);

    return t;
}

static void freetoken(struct CHToken *token)
{
    freestring(token->value);
    free(token);
}

static void freetokenlist(struct CHList *list)
{
    if (list->ptr != NULL) {
        freetoken((struct CHToken*)list->ptr);
        list->ptr = NULL;
    }

    if (list->next != NULL) {
        freetokenlist(list->next);
    }
}

static void tokenize(char *code)
{
    struct CHToken *t;
    unsigned int ch = 0;
    char c[1];
    char dl;

    while (code[ch]) {
        if (CHAR_IS_OPERATOR(code[ch])) {
            t = createtoken(CH_OPERATOR);
            c[0] = code[ch];
            t->value = stringcat(c, t->value);
            listadd(tokens, t);
            ++ch;
        } else if (code[ch] == '"' || code[ch] == '\'') {
            dl = code[ch];
            
            if (dl == '"') t = createtoken(CH_DSTRING);
            if (dl == '\'') t = createtoken(CH_SSTRING);

            c[0] = dl;
            t->value = stringcat(c, t->value);
            ++ch;
            while (!CHAR_IS_EOF(code[ch]) && code[ch] != dl) {
                c[0] = code[ch];
                t->value = stringcat(c, t->value);
                ++ch;
            }
            c[0] = dl;
            t->value = stringcat(c, t->value);
            ++ch;

            listadd(tokens, t);
        } else if (CHAR_IS_SPACE(code[ch])) {
            t = createtoken(CH_SPACE);
            listadd(tokens, t);
            ++ch;
        } else if (CHAR_IS_NEWLINE(code[ch])) {
            t = createtoken(CH_NL);
            listadd(tokens, t);
            ++ch;
        } else if (CHAR_IS_IDN(code[ch])) {
            t = createtoken(CH_IDN);

            while (CHAR_IS_IDN(code[ch]) || CHAR_IS_NUMBER(code[ch])) {
                c[0] = code[ch];
                t->value = stringcat(c, t->value);
                ch++;
            }

            listadd(tokens, t);
        } else if (CHAR_IS_NUMBER(code[ch])) {
            t = createtoken(CH_NUMBER);

            while (CHAR_IS_NUMBER(code[ch])) {
                c[0] = code[ch];
                t->value = stringcat(c, t->value);
                ch++;
            }

            if (code[ch] == '.' && CHAR_IS_NUMBER(code[ch + 1])) {
                c[0] = code[ch];
                t->value = stringcat(c, t->value);
                ++ch;

                while (CHAR_IS_NUMBER(code[ch])) {
                    c[0] = code[ch];
                    t->value = stringcat(c, t->value);
                    ch++;
                }
            }

            listadd(tokens, t);
        } else {
            ++ch;
        }
    }
}

static int findcode(char *code, size_t *len)
{
    unsigned int ch = 0;
    int start = 0, end = 0;

    while (code[ch]) {
        if (code[ch] == '`') {
            if (code[ch+1] == '`' && code[ch+2] == '`') {
                start = ch;

                ch += 3;
                while (!CHAR_IS_EOF(code[ch]) && code[ch] != '`') ++ch;

                if (code[ch] == '`' && code[ch+1] == '`' && code[ch+2] == '`') {
                    end = ch + 2;
                    break;
                }
            }
        } else {
            ++ch;
        }
    }

    if (end == 0) return -1;
    *len = (end - start);

    return start;
}

char* code2html(char *code, unsigned char mdenable)
{
    char *html;
    int index;
    size_t len, codelen;
    char *scode;

    tokens = createlist();

    if (mdenable) {
        index = findcode(code, &len);

        if (index >= 0) {
            scode = (char*)calloc(len - 5, sizeof(char));
            memcpy(scode, code + index + 3, len - 6);
            tokenize(scode);
            free(scode);
        }
    } else {
        tokenize(code);
    }

    out = createstring("", strlen(code) + 1024);

    render(tokens);

    if (mdenable && index > 0) {
        codelen = strlen(code);
        html = (char*)calloc(codelen + strlen(out->str) + 1, sizeof(char));
        memcpy(html, code, index - 1);
        strcat(html, out->str);
        strcat(html, code + index + len + 1);

        /*
        if (codelen >= len) {
            memcpy(html, code + len + 1, codelen);
        }*/
    } else {
        html = (char*)calloc(strlen(out->str) + 1, sizeof(char));
        strcat(html, out->str);
    }

    freestring(out);
    freetokenlist(tokens);
    freelist(tokens);

    return html;
}

int main(int argc, char **argv)
{
    FILE *f;
    char *html;
    char *fname;
    unsigned char mdenable = 0;

    if (argc < 2) {
        printf("Usage: code2html  <filename>\n");
        return -1;
    }

    if (argc > 2) {
        fname = argv[2];
        if (strcmp(argv[1], "-md") == 0) {
            mdenable = 1;
        }
    } else {
        fname = argv[1];
    }

    f = fopen(fname, "r");

    if (f == NULL) {
        printf("Couldn't open file or doesn't exists.\n");
        return -1;
    }

    fread(buff, 1, 1<<25, f);
    fclose(f);

    html = code2html(buff, mdenable);
    printf("%s", html);
    free(html);

    return 0;
}
