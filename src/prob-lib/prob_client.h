#ifndef PROB_CLIENT_H
#define PROB_CLIENT_H

#include <prob-lib/prob_helpers.h>

typedef struct prob_client* prob_client_t;

extern prob_client_t prob_client_create();

extern void prob_client_destroy(prob_client_t pc);

extern const char* prob_get_zocket(prob_client_t pc);

extern ProBState* prob_next_state(prob_client_t pc, ProBState s, char *transitiongroup, int *size);
extern ProBState* prob_next_action(prob_client_t pc, ProBState s, char *transitiongroup, int *size);

extern void prob_terminate(prob_client_t pc);

extern void prob_disconnect(prob_client_t pc);

extern void prob_connect(prob_client_t pc, const char* file);

extern ProBInitialResponse prob_init(prob_client_t pc, int is_por);

extern void print_matrix(const ProBMatrix m);

extern int prob_get_state_label(prob_client_t pc, ProBState s, char *label);

void prob_get_label_group(prob_client_t pc, ProBState s, int group, int *res);
#endif
