#include <hre/config.h>

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>

#include <dm/dm.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <hre-io/user.h>
#include <mc-lib/atomics.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-ltl.h>
#include <pins-lib/pins2pins-mucalc.h>
#include <pins-lib/por/pins2pins-por.h>
#include <ltsmin-lib/mucalc-grammar.h>
#include <ltsmin-lib/mucalc-parse-env.h>
#include <ltsmin-lib/mucalc-syntax.h>
#include <ltsmin-lib/mucalc-lexer.h>
#include <pins-lib/pg-types.h>

static char *mucalc_file = NULL;

static int mucalc_node_count = 0;

struct poptOption mucalc_options[]={
    { "mucalc", 0, POPT_ARG_STRING, &mucalc_file, 0, "modal mu-calculus formula or file with modal mu-calculus formula",
          "<mucalc-file>.mcf|<mucalc formula>"},
    POPT_TABLEEND
};

int GBhaveMucalc() {
    return (mucalc_file) ? 1 : 0;
}

int GBgetMucalcNodeCount() {
    return mucalc_node_count;
}


typedef struct mucalc_node {
    mucalc_expr_t       expression;
    pg_player_enum_t    player;
    int                 priority;
} mucalc_node_t;


typedef struct mucalc_group_entry {
    mucalc_node_t       node;
    int                 parent_group;
} mucalc_group_entry_t;


typedef struct mucalc_groupinfo {
    int                     group_count;
    mucalc_group_entry_t   *entries;
    int                     node_count;
    mucalc_node_t          *nodes;
    int                     variable_count;
    int                    *fixpoint_nodes; // mapping from variable index to node number
    int                     initial_node;
} mucalc_groupinfo_t;


typedef struct mucalc_context {
    model_t         parent;
    int             action_label_index;
    int             action_label_type_no;
    mucalc_parse_env_t env;
    int             mu_idx;
    int             len;
    int             groups;
    mucalc_groupinfo_t groupinfo;
} mucalc_context_t;

typedef struct cb_context {
    model_t         model;
    TransitionCB    cb;
    void           *user_context;
    int            *src;
    mucalc_context_t  *ctx;
    int             group;
    mucalc_node_t   node;
    int             target_idx;
} *cb_context_t;


/**
 * \brief Computes state label for the state.
 * Returns priority if <tt>label</tt> is PG_PRIORITY;
 * the player otherwise.
 */
static int
mucalc_sl_short(model_t model, int label, int *state)
{
    //Print(infoLong, "mucalc_sl_short");
    mucalc_context_t *ctx = GBgetContext(model);
    // State labels are dependent on only the mucalc_node part
    int mucalc_node_idx = state[0];
    mucalc_node_t node = ctx->groupinfo.nodes[mucalc_node_idx];
    if (label == PG_PRIORITY) {
        return node.priority;
    } else { // player
        return node.player;
    }
}


/**
 * \brief Computes state label for the state.
 * Returns priority if <tt>label</tt> is PG_PRIORITY;
 * the player otherwise.
 */
static int
mucalc_sl_long(model_t model, int label, int *state)
{
    //Print(infoLong, "mucalc_sl_long");
    mucalc_context_t *ctx = GBgetContext(model);
    int mucalc_node_idx = state[ctx->mu_idx];
    mucalc_node_t node = ctx->groupinfo.nodes[mucalc_node_idx];
    if (label == PG_PRIORITY) {
        return node.priority;
    } else { // player
        return node.player;
    }
}


/**
 * \brief Computes all state label for the state.
 */
static void
mucalc_sl_all(model_t model, int *state, int *labels)
{
    //Print(infoLong, "mucalc_sl_all");
    mucalc_context_t *ctx = GBgetContext(model);
    int mucalc_node_idx = state[ctx->mu_idx];
    mucalc_node_t node = ctx->groupinfo.nodes[mucalc_node_idx];
    labels[PG_PRIORITY] = node.priority;
    labels[PG_PLAYER] = node.player;
}


/**
 * \brief Outputs the state to <tt>log</tt>.
 */
void mucalc_print_state(log_t log, mucalc_context_t* ctx, int* state)
{
    if (log_active(log))
    {
        mucalc_node_t node = ctx->groupinfo.nodes[state[ctx->mu_idx]];
        const char* type_s = mucalc_type_print(node.expression->type);
        Printf(log, "[node %d, type=%s, ", state[ctx->mu_idx], type_s);
        lts_type_t ltstype = GBgetLTStype(ctx->parent);
        int state_length = lts_type_get_state_length(ltstype);
        for(int i=0; i<state_length; i++)
        {
            if (i > 0) Printf(log, ", ");
            char* name = lts_type_get_state_name(ltstype, i);
            int type_no = lts_type_get_state_typeno(ltstype, i);
            data_format_t format = lts_type_get_format(ltstype, type_no);
            switch(format)
            {
                case LTStypeDirect:
                case LTStypeRange:
                {
                    Printf(log, "%s=%d", name, state[i]);
                }
                break;
                default:
                {
                    chunk c = pins_chunk_get (ctx->parent, type_no, GBchunkPrettyPrint(ctx->parent, i, state[i]));
                    char value[c.len*2+6];
                    chunk2string(c, sizeof value, value);
                    Printf(log, "%s=%s", name, value);
                }
                break;
            }
        }
        Printf(log, "]\n");
    }
}


/**
 * \brief Callback function used by mucalc_long and mucalc_all, for the modal operators
 * must and may. The mucalc_cb function compares the action label of the underlying
 * transition in the parent LTS to the action expression of the modal operator.
 * If the action matches the expression, a transition is added with the target state being
 * a combination of the target state in the parent LTS and the mu-calculus subformula argument
 * of the modal operator.
 */
void mucalc_cb (void* context, transition_info_t* ti, int* dst, int* cpy) {
    Debug("mucalc_cb");
    cb_context_t cb_ctx = (cb_context_t)context;
    mucalc_context_t *ctx = cb_ctx->ctx;
    model_t parent = GBgetParent(cb_ctx->model);

    mucalc_node_t node = cb_ctx->node;
    mucalc_action_expression_t action_expr = ctx->env->action_expressions[node.expression->value];
    bool negated = action_expr.negated;
    const char* action_expr_string = mucalc_fetch_action_value(ctx->env, action_expr);
    Debug("mucalc_cb: negated=%s, action_expr=%s", (action_expr.negated ? "true":"false"), action_expr_string);

    bool valid_transition = negated;
    if (node.expression->value==-1 || strlen(action_expr_string)==0) // empty string
    {
        valid_transition = !negated;
    }
    else if (node.expression->value!=-1 && ctx->action_label_index!=-1)
    {
        int* edge_labels = ti->labels;
        chunk c = pins_chunk_get (parent, ctx->action_label_type_no, edge_labels[ctx->action_label_index]);
        char label[c.len*2+6];
        chunk2string(c, sizeof label, label);
        assert(strlen(label) >= 2);
        label[strlen(label) - 1] = '\0';
        char* l = label + sizeof(char);
        Debug("mucalc_cb: action label=%s (%d)", l, edge_labels[ctx->action_label_index]);
        // TODO: this part can be easily optimised by storing the action_expr in the same chunktable
        // as the labels.
        // But... if we want to extend this and use, e.g., regular expressions, that is not the way to go.
        if (strlen(action_expr_string)==strlen(l) && strncmp(action_expr_string, l, strlen(action_expr_string))==0)
        {
            valid_transition = !negated;
        }
    }
    if (valid_transition) // Action label matches the action expression.
    {
        int* _edge_labels = NULL;
        transition_info_t _ti = GB_TI(_edge_labels, cb_ctx->group);
        int _dst[ctx->len];
        memcpy(_dst, dst, ctx->len*sizeof(int)-1);
        _dst[ctx->mu_idx] = cb_ctx->target_idx;
        if (cpy == NULL)
        {
            cb_ctx->cb(cb_ctx->user_context, &_ti, _dst, NULL);
        }
        else
        {
            int _cpy[ctx->len];
            memcpy(_cpy, cpy, ctx->len*sizeof(int)-1);
            _cpy[ctx->mu_idx] = 0; // 0=write, 1=copy
            cb_ctx->cb(cb_ctx->user_context, &_ti, _dst, _cpy);
        }
        if (log_active(debug))
        {
            Debug("mucalc_cb: successor:");
            mucalc_print_state(debug, ctx, _dst);
        }
    }
}


static inline void mucalc_successor(model_t self, int *dst, int *cpy, int group, int* transition_count, TransitionCB cb, void *user_context)
{
    mucalc_context_t *ctx = GBgetContext(self);
    int* edge_labels = NULL;
    transition_info_t ti = GB_TI(edge_labels, group);
    cb(user_context, &ti, dst, cpy);
    ++(*transition_count);
    Debug("mucalc_successor:");
    mucalc_print_state(debug, ctx, dst);
}


/**
 * \brief Computes successor states for state <tt>src</tt> for transition group <tt>group</tt>.
 * Calls callback funtion <tt>cb</tt> on each successor state.
 * For most mu-calculus operators, only the mu-calculus node part of the state vector is
 * changed (to the underlying mu-calculus subformulae).
 * For the modal operators (must and may) successor states of the parent LTS are computed.
 * \return an upper bound on the number of successor states (for the modal operator,
 * the callback function mucalc_cb decides whether a transition of the parent LTS is also
 * a valid transition in the parity game).
 */
static int
mucalc_long (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    mucalc_context_t *ctx = GBgetContext(self);
    /* Fill copy vector with value 1 (copy), for all slots except the formula slot (mu_idx).
     * This copy vector is used for all operators except the modal operators.
     * Because all non-formula slots have '-' in the dependency matrix, except for the
     * rows for modal operators, this copy vector has no effect.*/
    int cpy[ctx->len];
    cpy[ctx->mu_idx] = 0; // 0=write, 1=copy
    for(int i=0; i<ctx->mu_idx; i++) { cpy[i] = 1; }
    int mucalc_node_idx = src[ctx->mu_idx];
    mucalc_node_t node = ctx->groupinfo.nodes[mucalc_node_idx];
    if (ctx->groupinfo.entries[group].node.expression->idx == node.expression->idx)
    {  // this group is applicable for this type of expression
        if (log_active(debug))
        {
            Print(debug, " ");
            Print(debug, "mucalc_long");
            mucalc_print_state(debug, ctx, src);
        }
        int transition_count = 0;
        switch(node.expression->type)
        {
            case MUCALC_FORMULA:
            case MUCALC_MU:
            case MUCALC_NU:
            {
                int dst[ctx->len];
                memcpy(dst, src, ctx->len*sizeof(int));
                dst[ctx->mu_idx] = node.expression->arg1->idx;
                mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
            }
            break;
            case MUCALC_AND:
            case MUCALC_OR:
            {
                int dst[ctx->len];
                // left branch
                memcpy(dst, src, ctx->len*sizeof(int));
                dst[ctx->mu_idx] = node.expression->arg1->idx;
                mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
                // right branch
                memcpy(dst, src, ctx->len*sizeof(int));
                dst[ctx->mu_idx] = node.expression->arg2->idx;
                mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
            }
            break;
            case MUCALC_TRUE:
            case MUCALC_FALSE:
            {
                mucalc_successor(self, src, cpy, group, &transition_count, cb, user_context); // self loop
            }
            break;
            case MUCALC_PROPOSITION:
            {
                int dst[ctx->len];
                memcpy(dst, src, ctx->len*sizeof(int));
                mucalc_proposition_t proposition = ctx->env->propositions[node.expression->value];
                model_t parent = GBgetParent (self);
                int src_value_idx = GBchunkPrettyPrint(parent, proposition.state_idx, src[proposition.state_idx]);
                if (src_value_idx==proposition.value_idx)
                { // proposition is true
                    dst[ctx->mu_idx] = ctx->env->true_expr->idx;
                    Print(debug, "Proposition %s is true.", mucalc_fetch_value(ctx->env, MUCALC_PROPOSITION, node.expression->value));
                }
                else
                {
                    dst[ctx->mu_idx] = ctx->env->false_expr->idx;
                    Print(debug, "Proposition %s is false (group=%d, src[state_idx]=%d, value_idx=%d)).",
                          mucalc_fetch_value(ctx->env, MUCALC_PROPOSITION, node.expression->value),
                          group,
                          src_value_idx, proposition.value_idx);
                }
                mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
            }
            break;
            case MUCALC_VAR:
            {
                int dst[ctx->len];
                memcpy(dst, src, ctx->len*sizeof(int));
                dst[ctx->mu_idx] = node.expression->arg1->idx;
                mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
            }
            break;
            case MUCALC_MUST:
            case MUCALC_MAY:
            {
                model_t parent = GBgetParent (self);
                int parent_group = ctx->groupinfo.entries[group].parent_group;
                int parent_src[ctx->len-1];
                memcpy(parent_src, src, ctx->len*sizeof(int)-1);
                cb_context_t cb_ctx = RT_NEW(struct cb_context);
                cb_ctx->model = self;
                cb_ctx->cb = cb;
                cb_ctx->src = src;
                cb_ctx->ctx = ctx;
                cb_ctx->user_context = user_context;
                cb_ctx->group = group;
                cb_ctx->node = node;
                cb_ctx->target_idx = node.expression->arg1->idx;
                transition_count += GBgetTransitionsLong(parent, parent_group, parent_src, mucalc_cb, cb_ctx);
            }
            break;
            case MUCALC_NOT:
            {
                // check if successor is T, F or proposition (only then negation is allowed).
                mucalc_node_t child = ctx->groupinfo.nodes[node.expression->arg1->idx];
                switch(child.expression->type)
                {
                    case MUCALC_FALSE:
                    case MUCALC_TRUE:
                    {
                        // generate the inverse as a successor instead
                        int dst[ctx->len];
                        memcpy(dst, src, ctx->len*sizeof(int));
                        dst[ctx->mu_idx] = (child.expression->type == MUCALC_FALSE) ?
                                ctx->env->true_expr->idx :
                                ctx->env->false_expr->idx;
                        mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
                    }
                    break;
                    case MUCALC_PROPOSITION:
                    {
                        // evaluate the proposition and write the inverse boolean result as successor
                        int dst[ctx->len];
                        memcpy(dst, src, ctx->len*sizeof(int));
                        mucalc_proposition_t proposition = ctx->env->propositions[node.expression->value];
                        model_t parent = GBgetParent (self);
                        int src_value_idx = GBchunkPrettyPrint(parent, proposition.state_idx, src[proposition.state_idx]);
                        if (src_value_idx==proposition.value_idx)
                        { // proposition is true, negation is false
                            dst[ctx->mu_idx] = ctx->env->false_expr->idx;
                            Print(debug, "Proposition !(%s) is false.", mucalc_fetch_value(ctx->env, MUCALC_PROPOSITION, node.expression->value));
                        }
                        else
                        {
                            dst[ctx->mu_idx] = ctx->env->true_expr->idx;
                            Print(debug, "Proposition !(%s) is true (src[state_idx]=%d, value_idx=%d)).",
                                  mucalc_fetch_value(ctx->env, MUCALC_PROPOSITION, node.expression->value),
                                  src_value_idx, proposition.value_idx);
                        }
                        mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
                    }
                    break;
                    default: Abort("Negation only allowed on leaf nodes, "
                            "before true, false or propositions.");
                }
            }
            break;
            default: Abort("mucalc_long: unknown expression type: %d", node.expression->type);
        }
        //Print(infoLong, "  transitions: %d.", transition_count);
        return transition_count;
    }
    else
    {
        return 0;
    }
}


/**
 * \brief Computes successor states for state <tt>src</tt> for all transition groups.
 * Calls callback funtion <tt>cb</tt> on each successor state.
 * For most mu-calculus operators, only the mu-calculus node part of the state vector is
 * changed (to the underlying mu-calculus subformulae).
 * For the modal operators (must and may) successor states of the parent LTS are computed.
 * \return an upper bound on the number of successor states (for the modal operator,
 * the callback function mucalc_cb decides whether a transition of the parent LTS is also
 * a valid transition in the parity game).
 */
static int
mucalc_all (model_t self, int *src, TransitionCB cb, void *user_context)
{
    Print(infoLong, "mucalc_all");
    mucalc_context_t *ctx = GBgetContext(self);
    /* Fill copy vector with value 1 (copy), for all slots except the formula slot (mu_idx).
     * This copy vector is used for all operators except the modal operators.
     * Because all non-formula slots have '-' in the dependency matrix, except for the
     * rows for modal operators, this copy vector has no effect.*/
    int cpy[ctx->len];
    cpy[ctx->mu_idx] = 0; // 0=write, 1=copy
    for(int i=0; i<ctx->mu_idx; i++) { cpy[i] = 1; }
    int mucalc_node_idx = src[ctx->mu_idx];
    mucalc_node_t node = ctx->groupinfo.nodes[mucalc_node_idx];
    int group = -1;
    if (log_active(debug))
    {
        Print(debug, " ");
        Print(debug, "mucalc_all");
        mucalc_print_state(debug, ctx, src);
    }
    int transition_count = 0;
    switch(node.expression->type)
    {
        case MUCALC_FORMULA:
        case MUCALC_MU:
        case MUCALC_NU:
        {
            int dst[ctx->len];
            memcpy(dst, src, ctx->len*sizeof(int));
            dst[ctx->mu_idx] = node.expression->arg1->idx;
            mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
        }
        break;
        case MUCALC_AND:
        case MUCALC_OR:
        {
            int dst[ctx->len];
            // left branch
            memcpy(dst, src, ctx->len*sizeof(int));
            dst[ctx->mu_idx] = node.expression->arg1->idx;
            mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
            // right branch
            memcpy(dst, src, ctx->len*sizeof(int));
            dst[ctx->mu_idx] = node.expression->arg2->idx;
            mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
        }
        break;
        case MUCALC_TRUE:
        case MUCALC_FALSE:
        {
            mucalc_successor(self, src, cpy, group, &transition_count, cb, user_context); // self loop
        }
        break;
        case MUCALC_PROPOSITION:
        {
            int dst[ctx->len];
            memcpy(dst, src, ctx->len*sizeof(int));
            mucalc_proposition_t proposition = ctx->env->propositions[node.expression->value];
            model_t parent = GBgetParent (self);
            int src_value_idx = GBchunkPrettyPrint(parent, proposition.state_idx, src[proposition.state_idx]);
            if (src_value_idx==proposition.value_idx)
            { // proposition is true
                dst[ctx->mu_idx] = ctx->env->true_expr->idx;
                Print(debug, "Proposition %s is true.", mucalc_fetch_value(ctx->env, MUCALC_PROPOSITION, node.expression->value));
                //mucalc_print_state(infoLong, ctx, src);
            }
            else
            {
                dst[ctx->mu_idx] = ctx->env->false_expr->idx;
                Print(debug, "Proposition %s is false (src[state_idx]=%d, value_idx=%d)).",
                                          mucalc_fetch_value(ctx->env, MUCALC_PROPOSITION, node.expression->value),
                                          src_value_idx, proposition.value_idx);
                //mucalc_print_state(infoLong, ctx, src);
            }
            mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
        }
        break;
        case MUCALC_VAR:
        {
            int dst[ctx->len];
            memcpy(dst, src, ctx->len*sizeof(int));
            dst[ctx->mu_idx] = node.expression->arg1->idx;
            mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
        }
        break;
        case MUCALC_MUST:
        case MUCALC_MAY:
        {
            model_t parent = GBgetParent (self);
            int parent_src[ctx->len-1];
            memcpy(parent_src, src, ctx->len*sizeof(int)-1);
            cb_context_t cb_ctx = RT_NEW(struct cb_context);
            cb_ctx->model = self;
            cb_ctx->cb = cb;
            cb_ctx->src = src;
            cb_ctx->ctx = ctx;
            cb_ctx->user_context = user_context;
            cb_ctx->group = group;
            cb_ctx->node = node;
            cb_ctx->target_idx = node.expression->arg1->idx;
            transition_count += GBgetTransitionsAll(parent, parent_src, mucalc_cb, cb_ctx);
        }
        break;
        case MUCALC_NOT:
        {
            // check if successor is T, F or proposition (only then negation is allowed).
            mucalc_node_t child = ctx->groupinfo.nodes[node.expression->arg1->idx];
            switch(child.expression->type)
            {
                case MUCALC_FALSE:
                case MUCALC_TRUE:
                {
                    // generate the inverse as a successor instead
                    int dst[ctx->len];
                    memcpy(dst, src, ctx->len*sizeof(int));
                    dst[ctx->mu_idx] = (child.expression->type == MUCALC_FALSE) ?
                            ctx->env->true_expr->idx :
                            ctx->env->false_expr->idx;
                    mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
                }
                break;
                case MUCALC_PROPOSITION:
                {
                    // evaluate the proposition and write the inverse boolean result as successor
                    int dst[ctx->len];
                    memcpy(dst, src, ctx->len*sizeof(int));
                    mucalc_proposition_t proposition = ctx->env->propositions[node.expression->value];
                    model_t parent = GBgetParent (self);
                    int src_value_idx = GBchunkPrettyPrint(parent, proposition.state_idx, src[proposition.state_idx]);
                    if (src_value_idx==proposition.value_idx)
                    { // proposition is true, negation is false
                        dst[ctx->mu_idx] = ctx->env->false_expr->idx;
                        Print(debug, "Proposition !(%s) is false.", mucalc_fetch_value(ctx->env, MUCALC_PROPOSITION, node.expression->value));
                    }
                    else
                    {
                        dst[ctx->mu_idx] = ctx->env->true_expr->idx;
                        Print(debug, "Proposition !(%s) is true (src[state_idx]=%d, value_idx=%d)).",
                              mucalc_fetch_value(ctx->env, MUCALC_PROPOSITION, node.expression->value),
                              src_value_idx, proposition.value_idx);
                    }
                    mucalc_successor(self, dst, cpy, group, &transition_count, cb, user_context);
                }
                break;
                default: Abort("Negation only allowed on leaf nodes, "
                        "before true, false or propositions.");
            }
        }
        break;
        default: Abort("mucalc_all: unknown expression type: %d", node.expression->type);
    }
    //Print(infoLong, "  transitions: %d.", transition_count);
    return transition_count;
}


/**
 * \brief Parses the mu-calculus formula in <tt>file</tt>.
 */
void mucalc_parse_file(const char *file, mucalc_parse_env_t env)
{
    FILE *in=fopen( file, "r" );
    stream_t stream = NULL;
    size_t used;
    if (in) {
        Print(infoLong, "Opening stream.");
        stream = stream_input(in);
    } else {
        Print(infoLong, "Read into memory.");
        stream = stream_read_mem((void*)file, strlen(file), &used);
    }
    yyscan_t scanner;
    env->input=stream;
    env->parser=mucalc_parse_alloc(RTmalloc);
    mucalc_parse(env->parser, TOKEN_EXPR, 0, env);
    mucalc_lex_init_extra(env, &scanner);
    mucalc_lex(scanner);
    mucalc_check_formula(env, env->formula_tree);
    mucalc_lex_destroy(scanner);
    stream_close(&env->input);
    mucalc_parse_free(env->parser, RTfree);
}


/**
 * \brief Computes the transition groups for the mu-calculus layer, based on the
 * subformula nodes and the number of transition groups in the parent LTS.
 */
void mucalc_compute_groups(mucalc_groupinfo_t* groupinfo, int parent_groups, int* count)
{
    for(int k=0; k<groupinfo->node_count; k++)
    {
        mucalc_node_t node = groupinfo->nodes[k];
        //const char* type_s = mucalc_type_print(node.expression->type);
        //Print(infoLong, "mucalc_compute_groups: k=%d, type=%s", k, type_s);

        switch(node.expression->type)
        {
            case MUCALC_FORMULA:
            case MUCALC_MU:
            case MUCALC_NU:
            case MUCALC_AND:
            case MUCALC_OR:
            case MUCALC_TRUE:
            case MUCALC_FALSE:
            case MUCALC_PROPOSITION:
            case MUCALC_VAR:
            case MUCALC_NOT:
                {
                    //Print(infoLong, "mucalc_compute_groups: count=%d, k=%d, type=%s", *count, k, type_s);
                    mucalc_group_entry_t entry;
                    entry.node = node;
                    entry.parent_group = -1;
                    groupinfo->entries[(*count)++] = entry;
                }
                break;
            case MUCALC_MUST:
            case MUCALC_MAY:
                for(int i=0; i<parent_groups; i++)
                {
                    //Print(infoLong, "mucalc_compute_groups: count=%d, k=%d, type=%s, parent_group=%d", *count, k, type_s, i);
                    mucalc_group_entry_t entry;
                    entry.node = node;
                    entry.parent_group = i;
                    groupinfo->entries[(*count)++] = entry;
                }
                break;
            default: Abort("mucalc_compute_groups: unknown expression type: %d", node.expression->type);
        }
    }
}


/**
 * \brief Computes nodes to represent the mu-calculus subformulae, to be used in the state vector to encode the current mu-calculus subformula.
 * FIXME: do some sanity checks: fixpoint variables unique in context, variables occur positively in fixpoint equations.
 */
int mucalc_compute_nodes(mucalc_expr_t expr, mucalc_node_t* nodes, int* fixpoint_nodes, int priority, pg_player_enum_t player, int parent_groups)
{
    if (expr == NULL)
        return 0;
    //Print(infoLong, "mucalc_compute_nodes: type=%s (%d)", mucalc_type_print(expr->type), expr->idx);
    switch(expr->type)
    {
        case MUCALC_FORMULA:
            {
                mucalc_node_t node;
                node.expression = expr;
                node.priority = priority;
                node.player = PG_OR;
                nodes[expr->idx] = node;
                return 1 + mucalc_compute_nodes(expr->arg1, nodes, fixpoint_nodes, priority, node.player, parent_groups);
            }
            break;
        case MUCALC_MU:
        case MUCALC_NU:
            {
                int pri = priority;
                if (expr->type == MUCALC_MU && !(pri % 2)) { // pri is even
                    pri++;
                } else if (expr->type == MUCALC_NU && (pri % 2)) { // pri is odd
                    pri++;
                }
                mucalc_node_t node;
                node.expression = expr;
                node.priority = pri;
                node.player = PG_OR;
                nodes[expr->idx] = node;
                fixpoint_nodes[expr->value] = expr->idx;
                return 1 + mucalc_compute_nodes(expr->arg1, nodes, fixpoint_nodes, pri, node.player, parent_groups);
            }
            break;
        case MUCALC_MUST:
        case MUCALC_MAY:
            {
                mucalc_node_t node;
                node.expression = expr;
                node.priority = priority;
                node.player = (expr->type == MUCALC_MUST) ? PG_AND : PG_OR;
                nodes[expr->idx] = node;
                return parent_groups + mucalc_compute_nodes(expr->arg1, nodes, fixpoint_nodes, priority, node.player, parent_groups);
            }
            break;
        case MUCALC_AND:
        case MUCALC_OR:
            {
                mucalc_node_t node;
                node.expression = expr;
                node.priority = priority;
                node.player = (expr->type == MUCALC_AND) ? PG_AND : PG_OR;
                nodes[expr->idx] = node;
                return 1 + mucalc_compute_nodes(expr->arg1, nodes, fixpoint_nodes, priority, node.player, parent_groups)
                         + mucalc_compute_nodes(expr->arg2, nodes, fixpoint_nodes, priority, node.player, parent_groups);
            }
            break;
        case MUCALC_NOT:
            {
                // check if successor is only T, F or proposition.
                switch(expr->arg1->type)
                {
                    case MUCALC_FALSE:
                    case MUCALC_TRUE:
                    case MUCALC_PROPOSITION:
                    break;
                    default: Abort("Negation only allowed on leaf nodes, "
                            "before true, false or propositions.");
                }
                mucalc_node_t node;
                node.expression = expr;
                node.priority = priority;
                node.player = PG_OR;
                nodes[expr->idx] = node;
                return 1 + mucalc_compute_nodes(expr->arg1, nodes, fixpoint_nodes, priority, node.player, parent_groups);
            }
            break;
        case MUCALC_TRUE:
        case MUCALC_FALSE:
            {
                // Case already handled on global level to make sure that true and false nodes are unique.
                return 0;
            }
            break;
        case MUCALC_PROPOSITION:
            {
                mucalc_node_t node;
                node.expression = expr;
                node.priority = priority;
                node.player = PG_OR;
                nodes[expr->idx] = node;
                return 1;
            }
            break;
        case MUCALC_VAR:
            {
                int fixpoint_idx = fixpoint_nodes[expr->value];
                if (fixpoint_idx==-1)
                    Abort("No fixpoint equation found for fixpoint variable %d", expr->value);
                mucalc_node_t fixpoint_node = nodes[fixpoint_idx];
                mucalc_node_t node;
                expr->arg1 = fixpoint_node.expression;
                node.expression = expr;
                node.priority = fixpoint_node.priority;
                node.player = fixpoint_node.player;
                nodes[expr->idx] = node;
                return 1;
            }
            break;
        default: Abort("mucalc_compute_nodes: unknown expression type: %d", expr->type);
    }
    (void) player;
}


/**
 * \brief Computes transition group information.
 * \return a mucalc_groupinfo_t object containing subformula nodes and transition group entries.
 */
mucalc_groupinfo_t mucalc_compute_groupinfo(mucalc_parse_env_t env, int parent_groups)
{
    Print(infoLong, "Computing groupinfo...");
    mucalc_groupinfo_t groupinfo;
    mucalc_expr_t tree = env->formula_tree;
    Print(infoLong, "Node count: %d", env->subformula_count);
    groupinfo.node_count = env->subformula_count;
    groupinfo.nodes = (mucalc_node_t*)RTmalloc(groupinfo.node_count*sizeof(struct mucalc_node));
    groupinfo.variable_count = env->variable_count;
    groupinfo.fixpoint_nodes = (int*)RTmalloc(groupinfo.variable_count*sizeof(int));
    for(int i=0; i< groupinfo.variable_count; i++)
    {
        groupinfo.fixpoint_nodes[i] = -1;
    }
    mucalc_node_t true_node;
    true_node.expression = env->true_expr;
    true_node.priority = 0;
    true_node.player = PG_AND;
    groupinfo.nodes[env->true_expr->idx] = true_node;
    mucalc_node_t false_node;
    false_node.expression = env->false_expr;
    false_node.priority = 1;
    false_node.player = PG_OR;
    groupinfo.nodes[env->false_expr->idx] = false_node;

    groupinfo.group_count = 2 + mucalc_compute_nodes(tree, groupinfo.nodes, groupinfo.fixpoint_nodes, 2, PG_OR, parent_groups);
    groupinfo.initial_node = env->formula_tree->idx;

    groupinfo.entries = (mucalc_group_entry_t*)RTmalloc(groupinfo.group_count*sizeof(struct mucalc_group_entry));
    int count = 0;
    mucalc_compute_groups(&groupinfo, parent_groups, &count);
    for(int i=0; i<groupinfo.group_count; i++)
    {
        mucalc_group_entry_t entry = groupinfo.entries[i];
        const char* type_s = mucalc_type_print(entry.node.expression->type);
        Debug("Group %d: node=%d, type=%s, parent_group=%d, priority=%d, player=%d, value=%s, arg1=%d, arg2=%d.",
              entry.node.expression->idx, i, type_s, entry.parent_group, entry.node.priority, entry.node.player,
              mucalc_fetch_value(env, entry.node.expression->type, entry.node.expression->value),
              (entry.node.expression->arg1==NULL ? -1 : entry.node.expression->arg1->idx),
              (entry.node.expression->arg2==NULL ? -1 : entry.node.expression->arg2->idx));
        (void)type_s;
    }
    Print(infoLong, "Done computing groupinfo.");
    return groupinfo;
}

/**
 * \brief Add proposition values to chunktable
 */
void mucalc_add_proposition_values(model_t model)
{
    mucalc_context_t *ctx = GBgetContext(model);
    mucalc_parse_env_t env = ctx->env;
    lts_type_t ltstype = GBgetLTStype(model);

    for(int p=0; p<env->proposition_count; p++)
    {
        mucalc_proposition_t proposition = env->propositions[p];
        char* id = SIget(env->ids,proposition.id);
        mucalc_value_t value_obj = env->values[proposition.value];
        for(int i=0; i<ctx->len-1; i++)
        {
            char* name = lts_type_get_state_name(ltstype, i);
            if (strlen(id)==strlen(name) && strncmp(id, name, strlen(id))==0)
            {
                env->propositions[p].state_idx = i;
                env->propositions[p].state_typeno = lts_type_get_state_typeno(ltstype, i);
                data_format_t format = lts_type_get_format(ltstype, env->propositions[p].state_typeno);
                if (value_obj.type==MUCALC_VALUE_STRING)
                {
                    if (format != LTStypeChunk && format != LTStypeEnum)
                    {
                        Abort("State part %d matches the proposition id %s, but value types do not match.", i, id);
                    }
                    char* value = SIget(env->strings,proposition.value);
                    int len = strlen(value);
                    char decode[len];
                    chunk data={.data=decode,.len=len};
                    string2chunk(value, &data);
                    env->propositions[p].value_idx = pins_chunk_put (model, env->propositions[p].state_typeno, data);
                    Print(infoLong, "State part %d matches the proposition id %s. Value stored at index %d.", i, id, env->propositions[p].value_idx);
                }
                else
                {
                    if (format == LTStypeChunk || format == LTStypeEnum)
                    {
                        Warning(info, "Warning: state part %d matches the proposition id %s, but is a chunk type.", i, id);
                    }
                    env->propositions[p].value_idx = value_obj.value;
                    Print(infoLong, "State part %d matches the proposition id %s. Value is %d.", i, id, env->propositions[p].value_idx);
                }
            }
        }
        if (env->propositions[p].state_idx == -1)
        {
            Warning(info, "No state part matches the proposition %s", mucalc_fetch_value(env, MUCALC_PROPOSITION, p));
        }
    }
}

/**
 * \brief Determines action label type and position for action matching
 */
void mucalc_add_action_labels(model_t model)
{
    mucalc_context_t *ctx = GBgetContext(model);
    lts_type_t ltstype = GBgetLTStype(model);
    int m = lts_type_get_edge_label_count(ltstype);
    ctx->action_label_index = -1;
    ctx->action_label_type_no = -1;
    for(int i=0; i<m; i++)
    {
        char* action_label_name1 = "action_labels";
        size_t action_label_len1 = strlen(action_label_name1);
        char* action_label_name2 = "action";
        size_t action_label_len2 = strlen(action_label_name2);
        char* name = lts_type_get_edge_label_name(ltstype, i);
        size_t name_len = strlen(name);
        if (    (action_label_len1==name_len && strncmp(action_label_name1, name, action_label_len1)==0)
            ||  (action_label_len2==name_len && strncmp(action_label_name2, name, action_label_len2)==0))
        {
            ctx->action_label_index = i;
            ctx->action_label_type_no = lts_type_get_edge_label_typeno(ltstype, i);
            Print(infoLong, "Action label: %s (%d)", name, i);
        }
    }
}

static matrix_t sl_info;


/**
 * \brief Initialises the mu-calculus PINS layer.
 */
model_t
GBaddMucalc (model_t model)
{
    /* add mu calculus */
    if (!mucalc_file) return model;

    if (PINS_LTL) Abort("The --mucalc option and --ltl options can not be combined.");
    if (PINS_POR) Abort("The --mucalc option and --por options can not be combined.");

    Warning(info,"Initializing mu-calculus layer... formula: %s", mucalc_file);
    model_t _model = GBcreateBase();
    mucalc_context_t *ctx = RTmalloc(sizeof *ctx);
    ctx->parent = model;
    GBsetContext(_model, ctx);

    // Parse the mu-calculus file
    mucalc_parse_env_t env = mucalc_parse_env_create();
    mucalc_parse_file(mucalc_file, env);
    if (env->error)
    {
        mucalc_parse_env_destroy(env);
        Abort("Error while parsing mu-calculus formula.");
    }
    ctx->env = env;

    // Copy and extend ltstype
    lts_type_t ltstype = GBgetLTStype(model);
    int mu_idx = lts_type_get_state_length(ltstype);
    ctx->mu_idx = mu_idx;
    ctx->len = mu_idx + 1;
    lts_type_t _ltstype = lts_type_clone(ltstype);
    lts_type_set_edge_label_count(_ltstype, 0); // parity games have no edge labels
    // Set new state length
    lts_type_set_state_length(_ltstype, ctx->len);
    // Add type for the mucalc_node values
    int type_count = lts_type_get_type_count(_ltstype);
    int mu_type = lts_type_add_type(_ltstype, "mu", NULL);
    lts_type_set_format(_ltstype, mu_type, LTStypeDirect);
    // Sanity check, type mu is new (added last)
    HREassert (mu_type == type_count, "mu type error");
    // Add state part
    lts_type_set_state_name(_ltstype, mu_idx, "mu");
    lts_type_set_state_typeno(_ltstype, mu_idx, mu_type);

    // Set state label dependencies
    int num_state_labels = 2;
    dm_create(&sl_info, num_state_labels, ctx->len);
    for (int i = 0; i < num_state_labels; i++) {
        dm_set(&sl_info, i, ctx->mu_idx);
    }
    GBsetStateLabelInfo(_model, &sl_info);

    // Define state labels
    lts_type_set_state_label_count(_ltstype, num_state_labels);
    lts_type_set_state_label_name(_ltstype, PG_PRIORITY, "priority");
    lts_type_set_state_label_type(_ltstype, PG_PRIORITY, "int");
    lts_type_set_state_label_name(_ltstype, PG_PLAYER, "player");
    lts_type_set_state_label_type(_ltstype, PG_PLAYER, "int");

    GBcopyChunkMaps(_model, model);
    // Set new LTS type
    GBsetLTStype(_model, _ltstype);
    // Extend the chunk maps
    GBgrowChunkMaps(_model, type_count);

    matrix_t *p_dm = GBgetDMInfo (model);
    int parent_groups = dm_nrows( p_dm );
    matrix_t *p_dm_r = GBgetDMInfoRead(model);
    matrix_t *p_dm_mw = GBgetDMInfoMayWrite(model);
    matrix_t *p_dm_w = GBgetDMInfoMustWrite(model);

    // Compute transition groups
    ctx->groupinfo = mucalc_compute_groupinfo(env, parent_groups);
    mucalc_node_count = ctx->groupinfo.node_count;

    mucalc_add_proposition_values(_model);
    mucalc_add_action_labels(_model);

    // Compute dependency matrix, add mucalc node
    matrix_t *_p_dm = (matrix_t*) RTmalloc(sizeof(matrix_t));
    matrix_t *_p_dm_r = (matrix_t*) RTmalloc(sizeof(matrix_t));
    matrix_t *_p_dm_mw = (matrix_t*) RTmalloc(sizeof(matrix_t));
    matrix_t *_p_dm_w = (matrix_t*) RTmalloc(sizeof(matrix_t));
    dm_create(_p_dm, ctx->groupinfo.group_count, ctx->len);
    dm_create(_p_dm_r, ctx->groupinfo.group_count, ctx->len);
    dm_create(_p_dm_mw, ctx->groupinfo.group_count, ctx->len);
    dm_create(_p_dm_w, ctx->groupinfo.group_count, ctx->len);
    for(int i=0; i < ctx->groupinfo.group_count; i++) {
        // copy old matrix rows
        int parent_group = ctx->groupinfo.entries[i].parent_group;
        if (parent_group != -1)
        {
            for(int j=0; j < ctx->len-1; j++) {
                if (dm_is_set(p_dm, parent_group, j))
                    dm_set(_p_dm, i, j);
                if (dm_is_set(p_dm_r, parent_group, j))
                    dm_set(_p_dm_r, i, j);
                if (dm_is_set(p_dm_mw, parent_group, j))
                    dm_set(_p_dm_mw, i, j);
                if (dm_is_set(p_dm_w, parent_group, j))
                    dm_set(_p_dm_w, i, j);
            }
        }
        else
        {
            // set state slots used in propositions as read dependent
            mucalc_expr_t expr = ctx->groupinfo.entries[i].node.expression;
            if (expr->type==MUCALC_PROPOSITION)
            {
                mucalc_proposition_t proposition = ctx->env->propositions[expr->value];
                Print(infoLong, "Setting read dependency for proposition in group %d: slot %d", i, proposition.state_idx);
                dm_set(_p_dm, i, proposition.state_idx);
                dm_set(_p_dm_r, i, proposition.state_idx);
            }
        }
        // Set mucalc node as dependent
        dm_set(_p_dm, i, ctx->mu_idx);
        dm_set(_p_dm_r, i, ctx->mu_idx);
        dm_set(_p_dm_mw, i, ctx->mu_idx);
        dm_set(_p_dm_w, i, ctx->mu_idx);
    }

    GBsetDMInfo(_model, _p_dm);
    GBsetDMInfoRead(_model, _p_dm_r);
    GBsetDMInfoMayWrite(_model, _p_dm_mw);
    GBsetDMInfoMustWrite(_model, _p_dm_w);

    // Set the state label functions for parity games
    GBsetStateLabelShort(_model, mucalc_sl_short);
    GBsetStateLabelLong(_model, mucalc_sl_long);
    GBsetStateLabelsAll(_model, mucalc_sl_all);

    lts_type_validate(_ltstype);

    if (log_active(infoLong)){
        lts_type_printf(infoLong, _ltstype);
    }

    // Set the Next-State functions
    GBsetNextStateLong(_model, mucalc_long);
    GBsetNextStateAll(_model, mucalc_all);

    GBinitModelDefaults(&_model, model);

    // Compute the initial state
    int s0[ctx->len];
    // Copy values from parent model
    GBgetInitialState(model, s0);
    // Set mucalc initial node
    s0[ctx->mu_idx] = ctx->groupinfo.initial_node;
    GBsetInitialState(_model, s0);
    Print(infoLong, "Initial state:");
    mucalc_print_state(infoLong, ctx, s0);

    return _model;
}
