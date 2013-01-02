/**
% Port of LLite in C
% Luca Bolognese
% 31/12/2012
**/

/**
Main ideas
==========

This is a port of [LLIte](https://github.com/lucabol/LLite/blob/master/Program.fs) in C. The reason for it is to
experiment with writing functional code in standard C and compare the experience with using a functional language
like F#
**/

/**
Cheating
--------

I will be using glib and an header of convenient macros/functions to help me. I don't think that is cheating.
A modern C praticoner has its bag of tricks.
**/

#include <string.h>
#include <stdbool.h>

#include "glib.h"
#include "lutils.h"

/**
Lack of tuples
--------------

In the snippet below I overcomed such deficiency by declaring a struct. Using the new constructor syntax makes
initializing a static table simple.
**/

typedef struct LangSymbols { char language[40]; char start[10]; char end[10];} LangSymbols;

static
LangSymbols* s_lang_params_table[] = {
    &(LangSymbols) {.language = "fsharp", .start = "(*" "*", .end = "*" "*)"},
    &(LangSymbols) {.language = "c", .start = "/*" "*", .end = "*" "*/"},
    &(LangSymbols) {.language = "csharp", .start = "/*" "*", .end = "*" "*/"},
    &(LangSymbols) {.language = "java", .start = "/*" "*", .end = "*" "*/"},
    NULL
};

/**
Folding over arrays
-------------------

I also need to gather all the languages, aka perform a fold over the queue. You might have noticed my propensity
to add a `NULL` terminator marker to arrays (as for strings). This allows me to avoid passing a size to functions
and allows writing utility macros (as `foreach` below) more simply.

In the rest of this program, every time I end a function with `_z`, it is because I consider it generally
usable and I add a version of it without the `_z` to lutils.h.
**/

#define array_foreach_z(p) for(; *symbols != NULL; ++symbols)

static
GString* usage_new(LangSymbols** symbols) {

    GString* langs = g_string_sized_new(20);
    array_foreach(symbols) g_string_append_printf(langs, "%s ", (*symbols)->language);

    g_string_truncate(langs, strlen(langs->str) - 1);

    GString* usage = g_string_sized_new(100);

    g_string_printf(usage, "                                    \n\
Usage: llite inputFile parameters                               \n\
where:                                                          \n\
One of the following two sets of parameters is mandatory        \n\
    -no string : string opening a narrative comment             \n\
    -nc string : string closing a narrative comment             \n\
or                                                              \n\
    -l language: where language is one of (%s)                  \n\
                                                                \n\
One of the following two sets of parameters is mandatory        \n\
    -co string : string opening a code block                    \n\
    -cc string : string closing a code block                    \n\
or                                                              \n\
    -indent N  : indent the code by N whitespaces               \n\
                                                                \n\
The following parameters are optional:                          \n\
    -o outFile : defaults to the input file name with mkd extension", langs->str);

    return usage;
}

/**
Find an item in an array based on some expression. Returns NULL if not found. Again, this is a common task,
hence I'll abstract it out with a macro (that ends up being a cute use of gcc statment expressions).
**/

#define array_find_z(arr, ...)                          \
    ({                                                  \
        array_foreach(arr) if (__VA_ARGS__) break;      \
        *arr;                                           \
    })

static
LangSymbols* lang_find_symbols(LangSymbols** symbols, char* lang) {
    g_assert(symbols);
    g_assert(lang);

    return array_find(symbols, !strcmp((*symbols)->language, lang));
}

/**
Deallocating stuff
------------------

You might wonder why I don't seem overly worried about deallocating the memory that I allocate.
I haven't gone crazy(yet). You'll see.

Discriminated unions
--------------------

Here are the discriminated unions macros from a previous blog post of mine. I'll need a couple of these
and pre-declare two functions.

**/

union_decl(CodeSymbols, Indented, Surrounded)
    union_type(Indented,    int indentation;)
    union_type(Surrounded,  char* start_code; char* end_code;)
union_end(CodeSymbols);

typedef struct Options {
    char*           start_narrative;
    char*           end_narrative;
    CodeSymbols*    code_symbols;
} Options;

static
gchar* translate(Options*, gchar*);

union_decl(Block, Code, Narrative)
    union_type(Code,        char* code)
    union_type(Narrative,   char* narrative)
union_end(Block);

/**
Main data structure
-------------------

We want to use higher level abstractions that standard C arrays, hence we'll pick a convinient data structure
to use in the rest of the code. A queue allows you to insert at the front and back, with just a one pointer
overhead over a single linked list. Hence it is my data structure of choice for this program.
**/

static
GQueue* blockize(Options, char*);

/**
There is already a function in glib to check if a string has a certain prefix (`g_str_has_prefix`). We need one
that returns the remaining string after the prefix. We also define a g_slow_assert that is executed just if
G_ENABLE_SLOW_ASSERT is defined
**/

static
char* str_after_prefix(char* src, char* prefix) {
    g_assert(src);
    g_assert(prefix);
    g_slow_assert(g_str_has_prefix(src, prefix));

    while(*prefix != '\0')
        if(*src == *prefix) ++src, ++prefix;
        else break;

    return src;
}

/**
Tokenizer
---------

The structure of the function is identical to the F# version. The big bread-winners are statement expressions ...
**/

union_decl(Token, OpenComment, CloseComment, Text)
    union_type(OpenComment, int line)
    union_type(CloseComment,int line)
    union_type(Text,        char* text)
union_end(Token);

GQueue* tokenize(Options* options, char* source) {
    g_assert(options);
    g_assert(source);

    struct tuple { int line; GString* acc; char* rem;};

    bool is_opening(char* src)          { return g_str_has_prefix(src, options->start_narrative);}
    bool is_closing(char* src)          { return g_str_has_prefix(src, options->end_narrative);}
    char* remaining_open (char* src)    { return str_after_prefix(src, options->start_narrative);}
    char* remaining_close(char* src)    { return str_after_prefix(src, options->end_narrative);}

    struct tuple text(char* src, GString* acc, int line) {
        inline struct tuple stop_parse_text() { return (struct tuple) {.line = line, .acc = acc, .rem = src};}

        return  str_empty (src)? stop_parse_text() :
                is_opening(src)? stop_parse_text() :
                is_closing(src)? stop_parse_text() :
                                ({
                                  int line2         = g_str_has_prefix(src, "\n") ? line + 1 : line;
                                  GString* newAcc   = g_string_append_c(acc, *src);
                                  char* rem         = src + 1;
                                  text(rem, newAcc, line2);
                                });
    }

    GQueue* tokenize_rec(char* src, GQueue* acc, int line) {
        return  str_empty(src)  ?   acc                     :
                is_opening(src) ?   tokenize_rec(remaining_open(src),
                                               g_queue_push_back(acc, union_new(Token, OpenComment, .line = line)),
                                               line)        :
                is_closing(src) ?   tokenize_rec(remaining_close(src),
                                               g_queue_push_back(acc, union_new(Token, CloseComment, .line = line)),
                                               line)        :
                                ({
                                    struct tuple t = text(src, g_string_sized_new(200), line);
                                    tokenize_rec(t.rem,
                                        g_queue_push_back(acc, union_new(Token, Text, .text = t.acc->str)), t.line);
                                 });
    }

    return tokenize_rec(source, g_queue_new(), 1);
}

/**
Parser
------
**/

union_decl(Chunk, NarrativeChunk, CodeChunk)
    union_type(NarrativeChunk,  GQueue* tokens)
    union_type(CodeChunk,       GQueue* tokens)
union_end(Chunk);

static
GQueue* parse(Options* options, GQueue* tokens) {
    g_assert(options);
    g_assert(tokens);

    struct tuple { GQueue* acc; GQueue* rem;};

    #define error(...) ({ g_error(__VA_ARGS__); (struct tuple) {.acc = NULL, .rem = NULL}; })

    struct tuple parse_narrative(GQueue* acc, GQueue* rem) {


        Token* h    = g_queue_pop_head(rem);
        GQueue* t   = rem;

        return  g_queue_is_empty(t)     ? error("You haven't closed your last narrative comment")   :
                h->kind == OpenComment  ?
                    error("Don't open narrative comments inside narrative comments at line %i", h->OpenComment.line) :
                h->kind == CloseComment ? (struct tuple) {.acc = acc, .rem = t}                     :
                h->kind == Text         ? parse_narrative(g_queue_push_back(acc, h), t)             :
                                          error("Should never get here");
    };

    struct tuple parse_code(GQueue* acc, GQueue* rem) {

        Token* h    = g_queue_pop_head(rem);
        GQueue* t   = rem;

        return  g_queue_is_empty(t)     ? (struct tuple) {.acc = acc, .rem = t}                             :
                h->kind == OpenComment  ? (struct tuple) {.acc = acc, .rem = g_queue_push_front(rem, h)}    :
                h->kind == CloseComment ? parse_code(g_queue_push_back(acc, h), rem)                        :
                h->kind == Text         ? parse_code(g_queue_push_back(acc, h), rem)                        :
                                          error("Should never get here");
    };
    #undef error

    GQueue* parse_rec(GQueue* acc, GQueue* rem) {

        Token* h    = g_queue_pop_head(rem);
        GQueue* t   = rem;

        return  g_queue_is_empty(t)     ? acc                                                               :
                h->kind == OpenComment  ? ({
                                           GQueue* emp = g_queue_new();
                                           struct tuple tu = parse_narrative(emp, t);
                                           Chunk* ch = union_new(Chunk, NarrativeChunk, .tokens = tu.acc );
                                           GQueue* newQ = g_queue_push_back(acc, ch);
                                           parse_rec(newQ, tu.rem);
                                           })                                                              :
                h->kind == CloseComment ? g_error_e("Don't insert a close narrative comment at the start of your program at line %i",
                                                h->OpenComment.line)                                        :
                h->kind == Text         ?
                                        ({
                                           GQueue* emp = g_queue_new();
                                           struct tuple tu = parse_code(g_queue_push_front(emp, h), t);
                                           parse_rec(g_queue_push_back(acc, union_new(Chunk, CodeChunk, .tokens = tu.acc )),
                                                     tu.rem);
                                          })                                                               :
                                          g_error_e("Should never get here");
    }

    return parse_rec(g_queue_new(), tokens);
}

#define RUN_TESTS

#ifndef RUN_TESTS

int main(int argc, char* argv[])
{
    return 0;
}

#else

Options* s_fsharp_options = NULL;

static char* tokens[] = {"before (** inside **) after", "(** aaf  (** afaf **) faf **)", "", "(****)", "fafdaf", NULL} ;

static
GString* print_tokens(GQueue* tokens) {
    GString* result = g_string_sized_new(64);
    g_queue_foreach(tokens, lambda(void, (void* it, G_GNUC_UNUSED void* no) {
                                Token* tok = it;
                                char* s =   tok->kind == OpenComment  ? "(**"  :
                                            tok->kind == CloseComment ? "**)" :
                                                                        tok->Text.text;
                                g_string_append_printf(result, "%s", s);
                              }), NULL);
    return result;
}

static
void test_tokenizer() {

    void testToken(char* s) {
        GQueue* q = tokenize(s_fsharp_options, s);

        GString* result = print_tokens(q);

        //g_message("%s == %s",s, result->str);
        g_assert_cmpstr(s, ==, result->str);
    }
    char** toks = tokens;
    array_foreach(toks) testToken(*toks);
}

static
void test_parser() {

    inline GQueue* enrich(GQueue* tokens) {
        g_queue_push_front(tokens, union_new(Token, OpenComment, .line = 0));
        g_queue_push_back(tokens, union_new(Token, CloseComment, .line = 0));
        return tokens;
    }

    void testToken(char* s) {
        GQueue* q = parse(s_fsharp_options, tokenize(s_fsharp_options, s));

        GString* result = g_string_sized_new(64);
        g_queue_foreach(q, lambda(void, (void* it, G_GNUC_UNUSED void* no) {
                                    Chunk* c = it;
                                    GString* s =    c->kind == NarrativeChunk  ?
                                                        print_tokens(enrich(c->NarrativeChunk.tokens)):
                                                    c->kind == CodeChunk ?
                                                        print_tokens(c->CodeChunk.tokens)     :
                                                        g_error_e("Should never get here");
                                    g_string_append_printf(result, "%s", s->str);
                                  }), NULL);

            g_message("%s == %s",s, result->str);
            g_assert_cmpstr(s, ==, result->str);
    }
    char** toks = tokens;
    array_foreach(toks) testToken(*toks);
}

int runTests(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);

    if(g_test_quick()) {

        s_fsharp_options    = g_new(Options, 1);
        *s_fsharp_options   = (Options) {.start_narrative = "(**",
                                              .end_narrative = "**)",
                                              .code_symbols = union_new(CodeSymbols, Surrounded,
                                                                        .start_code = "````fsharp",
                                                                        .end_code   = "````")};

        g_test_add_func("/clite/tokenizer", test_tokenizer);
        g_test_add_func("/clite/parser", test_parser);
    }

    return g_test_run();
}

int main(int argc, char* argv[])
{
    return runTests(argc, argv);
}

#endif // RUN_TESTS
