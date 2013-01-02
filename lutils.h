#ifndef L_FUNCTIONAL_INCLUDED
#define L_FUNCTIONAL_INCLUDED

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef __GNUC__

static inline void __autofree(void *p) {
     void **_p = (void**)p;
     free(*_p);
}

static inline G_GNUC_PURE bool str_empty(const char* str) {return str[0] == '\0';}

#define array_foreach(p) for(; *p != NULL; ++p)

#define array_find(arr, ...)                            \
    ({                                                  \
        array_foreach(arr) if (__VA_ARGS__) break;      \
        *arr;                                           \
    })

static inline
GQueue* array_to_queue(void** array) {
    g_assert(array);

    GQueue* q = g_queue_new();
    for(; *array != NULL; array++) {
        g_queue_push_tail(q, *array);
    }
    g_assert(q);
    return q;
}

#define auto_clean(f)   __attribute((cleanup(f)))
#define auto_free       auto_clean(__autofree)

#ifdef G_ENABLE_SLOW_ASSERT
#define g_slow_assert(...) G_STMT_START g_assert(__VA_ARGS__); G_STMT_END
#else
#define g_slow_assert(...)
#endif

#define g_queue_push_back(q, ...)                       \
    ({                                                  \
     g_queue_push_tail(q, __VA_ARGS__);                 \
     q; })

#define g_queue_push_front(q, ...)                      \
    ({                                                  \
     g_queue_push_head(q, __VA_ARGS__);                 \
     q; })

#define lambda(return_type, ...)            \
  ({                                        \
    return_type __fn__ __VA_ARGS__          \
    __fn__;                                 \
  })

#endif

#define union_decl(alg, ...)                                                        \
typedef struct alg {                                                                \
    enum {  __VA_ARGS__ } kind;                                                     \
    union {

#define union_type(type, ...)                                                       \
    struct type { __VA_ARGS__ ;} type;

#define union_end(alg)                                                              \
    };} alg;

#define union_set(instance, type, ...)                                              \
    G_STMT_START {                                                                  \
        (instance)->kind     = (type);                                              \
        (instance)->type   = (struct type) { __VA_ARGS__ };                         \
    } G_STMT_END

#define union_new(alg, type, ...)                                               \
    ({                                                                          \
        alg* instance = g_new(alg, 1);                                          \
        instance->kind     = (type);                                            \
        instance->type   = (struct type) { __VA_ARGS__ };                       \
        instance;                                                               \
    })

#define g_error_e(...)                                                              \
    ({                                                                              \
        g_error(__VA_ARGS__);                                                       \
        NULL;                                                                    \
    })

#define g_assert_e(...)                                                              \
    ({                                                                               \
        g_assert(__VA_ARGS__);                                                       \
        NULL;                                                                        \
    })

#define g_assert_no_match g_assert_e("Should never get here")

#define g_func(type, name, ...) lambda(void, (void* private_it, G_GNUC_UNUSED void* private_no){       \
                                       type name = private_it;                                         \
                                       __VA_ARGS__                                                     \
                                })

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
