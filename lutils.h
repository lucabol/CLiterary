#ifndef L_FUNCTIONAL_INCLUDED
#define L_FUNCTIONAL_INCLUDED

#include <stdlib.h>
#include <stdio.h>

#ifdef __GNUC__
// It should really be an attribute 'unused' on each function, so as not to disable it for all translation unit
#pragma GCC diagnostic ignored "-Wunused-function"
// Needs nested functions and block expressions

 static __inline__ void __autofree(void *p) {
     void **_p = (void**)p;
     free(*_p);
 }

#define auto_clean(f)   __attribute((cleanup(f)))
#define auto_free       auto_clean(__autofree)

#define lambda(return_type, function_body)  \
  ({                                        \
    return_type __fn__ function_body        \
    __fn__;                                 \
  })

#endif

#define union_decl(alg, ...)                                                        \
typedef struct alg {                                                                \
    enum {  __VA_ARGS__ } kind;                                                     \
    union {

#define union_type(type, ...)                                                       \
    struct type { __VA_ARGS__ } type;

#define union_end(alg)                                                              \
    };} alg;

#define union_set(instance, type, ...)                                              \
    G_STMT_START {                                                                  \
        (instance)->kind     = (type);                                              \
        (instance)->type   = (struct type) { __VA_ARGS__ };                         \
    } G_STMT_END

#define g_assert_e(expr) (                                                          \
    (G_LIKELY (!expr) ?                                                             \
       (void)g_assertion_message_expr (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, \
                                                 #expr)                             \
    : (void) 1) )

#define union_fail(...) (g_assert_e(((void)(__VA_ARGS__) , false)), (__VA_ARGS__))

#define union_case_only_s(instance, type, ...)                                      \
        G_STMT_START {                                                              \
        if((instance)->kind == (type)) {                                            \
            G_GNUC_UNUSED struct type* it = &((instance)->type); __VA_ARGS__; }     \
        else g_assert_not_reached();                                                \
        } G_STMT_END

#define union_case_first_s(alg, instance, type, ...)                                \
    G_STMT_START {                                                                  \
        alg* private_tmp = (instance);                                              \
        if(private_tmp->kind == type) {                                             \
            G_GNUC_UNUSED struct type* it = &((private_tmp)->type); __VA_ARGS__; }

#define union_case_s(type, ...)                                                     \
        else if(private_tmp->kind == type) {                                        \
            G_GNUC_UNUSED struct type* it = &((private_tmp)->type); __VA_ARGS__; }

#define union_case_last_s(type, ...)                                                \
        else if(private_tmp->kind == type) {                                        \
            G_GNUC_UNUSED struct type* it = &((private_tmp)->type); __VA_ARGS__; }  \
            else g_assert_not_reached(); } G_STMT_END

#define union_case_default_s(...)                                                   \
        else __VA_ARGS__; } G_STMT_END

// Need to use assert here because g_assert* cannot be used in expressions as it expands to do .. while(0)
#define union_case_only(instance, type, ...)                                        \
        ( (instance)->kind == (type) ? (__VA_ARGS__) : (assert(false), __VA_ARGS__) )

#define union_case_first(instance, type, ...)                                       \
        ( (instance)->kind == (type) ? (__VA_ARGS__) :

#define union_case(instance, type, ...)                                             \
        (instance)->kind == (type) ? (__VA_ARGS__) :

#define union_case_last(instance, type, ...)                                        \
        (instance)->kind == (type) ? (__VA_ARGS__) : (assert(false), (__VA_ARGS__)) )
#define union_case_default(...)                                                     \
        (__VA_ARGS__) )

#endif // L_FUNCTIONAL_INCLUDED
