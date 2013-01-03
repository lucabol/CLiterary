Options* s_fsharp_options = NULL;

static char* tokens[] = {"before (** inside **) after",
                        "(** aaf  faf **)(** afaf **)",
                        "",
                        "(****)",
                        "fafdaf",
                        "afadf afafa (** afaf **)",
                        NULL} ;

static
GString* print_tokens(GQueue* tokens) {
    GString* result = g_string_sized_new(64);
    g_queue_foreach(tokens, g_func(Token*, tok,
                                char* s =   tok->kind == OpenComment  ? "(**"  :
                                            tok->kind == CloseComment ? "**)" :
                                                                        tok->Text.text;
                                g_string_append_printf(result, "%s", s);
                              ), NULL);
    return result;
}

static
void test_tokenizer() {

    void testToken(char* s) {
        GQueue* q = tokenize(s_fsharp_options, s);

        GString* result = print_tokens(q);

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
        g_queue_foreach(q, g_func(Chunk*, c,
                                    GString* s =    c->kind == NarrativeChunk  ?
                                                        print_tokens(enrich(c->NarrativeChunk.tokens)):
                                                    c->kind == CodeChunk ?
                                                        print_tokens(c->CodeChunk.tokens)     :
                                                        g_assert_no_match;
                                    g_string_append_printf(result, "%s", s->str);
                                  ), NULL);

            g_assert_cmpstr(s, ==, result->str);
    }
    char** toks = tokens;
    array_foreach(toks) testToken(*toks);
}

static
GString* print_blocks(GQueue* q) {

    char* enrich(char* narrative) {
        return g_strjoin("", s_fsharp_options->start_narrative, narrative, s_fsharp_options->end_narrative, NULL);
    }

    GString* result = g_string_sized_new(64);
    g_queue_foreach(q, g_func(Block*, b,
                            char* s =   b->kind == Narrative  ? enrich(b->Narrative.narrative)  :
                                        b->kind == Code       ? b->Code.code                    :
                                                                g_assert_no_match;
                            g_string_append(result, s);
                            ), NULL);
    return result;
}

static
void test_blockize() {

    void testToken(char* str) {
        GQueue* q = blockize(s_fsharp_options, str);

        GString* result = print_blocks(q);
        g_assert_cmpstr(str, ==, result->str);
    }

    char** toks = tokens;
    array_foreach(toks) testToken(*toks);
}

static
void test_notalpha() {
    g_assert(is_str_all_spaces("\n       "));
    g_assert(is_str_all_spaces("\t"));
    g_assert(is_str_all_spaces(""));
    g_assert(!is_str_all_spaces("\t  c "));
    g_assert(!is_str_all_spaces("a "));
    g_assert(!is_str_all_spaces(" a"));
    g_assert(!is_str_all_spaces("\t b "));
}

typedef struct str_pair { char* exp; char* got;} str_pair;

static
void test_remove_empty_blocks() {
    str_pair* t[] = {
        &(str_pair) {.exp = "(**  **) aa", .got = " aa"},
        &(str_pair) {.exp = "  (**  **) aa", .got = " aa"},
        &(str_pair) {.exp = "  (** a **) aa", .got = "(** a **) aa"},
        &(str_pair) {.exp = "  (** a **) \n", .got = "(** a **)"},
        NULL
    };

    str_pair** ptr = t;
    array_foreach(ptr) {
        GQueue* q = remove_empty_blocks(s_fsharp_options, blockize(s_fsharp_options, (*ptr)->exp));

        GString* result = print_blocks(q);
        g_assert_cmpstr((*ptr)->got, ==, result->str);
    };
}

static
void test_merge_blocks() {
    str_pair* t[] = {
        &(str_pair) {.exp = "(**abc**)(**def**)", .got = "abc\ndef"},
        &(str_pair) {.exp = "  (**  **)aa(** **)bb", .got = "aa\nbb"},
        NULL
    };

    str_pair** ptr = t;
    array_foreach(ptr) {
        GQueue* removed = remove_empty_blocks(s_fsharp_options, blockize(s_fsharp_options, (*ptr)->exp));
        GQueue* q = merge_blocks(s_fsharp_options,removed);

        GString* result = print_blocks(q);
        g_assert_cmpstr((*ptr)->got, ==, result->str);
    };
}

static
void test_after_prefix() {
    str_pair* t[] = {
        &(str_pair) {.exp = "(**abc", .got = "abc"},
        &(str_pair) {.exp = "(**", .got = ""},
        NULL
    };

    str_pair** ptr = t;
    array_foreach(ptr) {
        char* result = str_after_prefix((*ptr)->exp, "(**");
        g_assert_cmpstr((*ptr)->got, ==, result);
    };
}

static
void test_indent() {
    str_pair* t[] = {
        &(str_pair) {.exp = "(**abc**)\n(**def**)", .got = "    (**abc**)\n    (**def**)"},
        &(str_pair) {.exp = "(**  **)aa(** **)\nbb", .got = "    (**  **)aa(** **)\n    bb"},
        NULL
    };

    str_pair** ptr = t;
    array_foreach(ptr) {
        char* result = indent(4, (*ptr)->exp);
        g_assert_cmpstr((*ptr)->got, ==, result);
    };
}

static
void test_code_tags() {
    str_pair* t[] = {
        &(str_pair) {.exp = " bb ", .got = "\n````fsharp\nbb\n````\n"},
        &(str_pair) {.exp = "(** bb **)", .got = "(**\nbb\n**)"},
        &(str_pair) {.exp = "bb (** aa **)", .got = "\n````fsharp\nbb\n````\n(**\naa\n**)"},
        NULL
    };

    str_pair** ptr = t;
    array_foreach(ptr) {
        GQueue* q = process_phases(s_fsharp_options, blockize(s_fsharp_options, (*ptr)->exp));

        GString* result = print_blocks(q);
        g_assert_cmpstr((*ptr)->got, ==, result->str);
    };
}

static
void test_translate() {
    str_pair* t[] = {
        &(str_pair) {.exp = " bb ", .got = "\n````fsharp\nbb\n````\n"},
        &(str_pair) {.exp = "(** bb **)", .got = "\nbb\n"},
        &(str_pair) {.exp = "bb (** aa **)", .got = "\n````fsharp\nbb\n````\n\naa\n"},
        NULL
    };

    str_pair** ptr = t;
    array_foreach(ptr) {
        char* result = translate(s_fsharp_options, (*ptr)->exp);

        g_assert_cmpstr((*ptr)->got, ==, result);
    };
}

int run_tests(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);

    if(g_test_quick()) {

        s_fsharp_options    = g_new(Options, 1);
        *s_fsharp_options   = (Options) {.start_narrative = "(**",
                                              .end_narrative = "**)",
                                              .code_symbols = union_new(CodeSymbols, Surrounded,
                                                                        .start_code = "````fsharp",
                                                                        .end_code   = "````")};

        g_test_add_func("/clite/tokenizer",     test_tokenizer);
        g_test_add_func("/clite/parser",        test_parser);
        g_test_add_func("/clite/blockize",      test_blockize);
        g_test_add_func("/clite/notalpha",      test_notalpha);
        g_test_add_func("/clite/remblocks",     test_remove_empty_blocks);
        g_test_add_func("/clite/mergeblocks",   test_merge_blocks);
        g_test_add_func("/clite/indent",        test_indent);
        g_test_add_func("/clite/afterprefix",   test_after_prefix);
        g_test_add_func("/clite/codetags",      test_code_tags);
        g_test_add_func("/clite/translate",      test_translate);
    }

    return g_test_run();
}
