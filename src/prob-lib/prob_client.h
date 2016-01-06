#ifndef PROB_CLIENT_H
#define PROB_CLIENT_H

#include <prob-lib/prob_helpers.h>

typedef struct prob_client* prob_client_t;

extern prob_client_t prob_client_create();

extern void prob_client_destroy(prob_client_t pc);

extern const char* prob_get_zocket(prob_client_t pc);

extern ProBState* prob_next_state(prob_client_t pc, ProBState s, char *transitiongroup, int *size);

extern void prob_terminate(prob_client_t pc);

extern void prob_disconnect(prob_client_t pc);

extern void prob_connect(prob_client_t pc, const char* file);

extern ProBInitialResponse prob_init(prob_client_t pc);

extern void print_matrix(const ProBMatrix m);

#endif
