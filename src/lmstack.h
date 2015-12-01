/**
 *  By Pengcheng Li <landy0220@gmail.com>
 *  -- C++ --
 */

#ifndef __LSTACK__
#define __LSTACK__

typedef struct _snode
{
    int e, s;
    struct _snode *next;
}SNODE;

typedef struct _stack
{
    SNODE *head, *tail;
    int count;
}STACK;

inline static void *
L_MALLOC (int n)
{
    void *p;

    p =(void *)mmap(0, n, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if(p != (void *)-1)
    {
        memset(p,0,n);
    }
    else
    {
        printf("error: ac alloc failed\n");
        return NULL;
    }
    return p;
}

inline static void
L_FREE (void *p)
{
    int ret = munmap(p, sizeof(SNODE));
    if ( ret == -1 ) {
        printf("Error: mumap for hash table element failed!\n");
        abort();
    }
}

/* stack_init - init stack */
inline void stack_init (STACK * s)
{
    s->head = s->tail = 0;
    s->count = 0;
}

/* stack_push - add node to stack */
inline void stack_push (STACK * s, int e, int st)
{
    SNODE * q;
    if (!s->head)
    {
        q = s->tail = s->head = (SNODE *) L_MALLOC (sizeof (SNODE));
        assert (q != NULL);
        q->e = e;
        q->s = st;
        q->next = 0;
    }
    else
    {
        q = (SNODE *) L_MALLOC (sizeof (SNODE));
        assert (q != NULL);
        q->e = e;
        q->s = st;
        q->next = s->head;
        s->head = q;
    }
    s->count++;
}

/* stack_remove - remove the 1st node */
inline void stack_toppop (STACK * s, int *e, int *st)
{
    /* if no node, jus return two -1s */
    *e = -1;
    *st = -1;
    SNODE * q;
    if (s->head)
    {
        q = s->head;
        *e = q->e;
        *st = q->s;

        s->head = s->head->next;
        s->count--;
        if (!s->head)
        {
            s->tail = 0;
            s->count = 0;
        }
        L_FREE (q);
    }
}

/* stack_count - return stack length */
inline int stack_count (STACK * s)
{
    return s->count;
}

/* stack_free - free stack */
inline void stack_free (STACK * s)
{
    while(stack_count(s))
    {
        int e,st;
        stack_toppop(s, &e, &st);
    }
}


#endif /*__LSTACK__*/
