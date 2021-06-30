/**
 * stm.c - pequeño motor de maquina de estados donde los eventos son los
 *         del selector.c
 */
#include <stdlib.h>
#include <proxy.h>
#include <logger.h>
#include <stm.h>

#define N(x) (sizeof(x)/sizeof((x)[0]))

void
stm_init(struct state_machine *stm) {
    // verificamos que los estados son correlativos, y que están bien asignados.
    for(unsigned i = 0 ; i <= stm->max_state; i++) {
        if(i != stm->states[i].state) {
            abort();
        }
    }

    if(stm->initial < stm->max_state) {
        stm->current = NULL;
    } else {
        abort();
    }
}

inline static void
handle_first(struct state_machine *stm, selector_key_t *key) {
    if(stm->current == NULL) {
        stm->current = stm->states + stm->initial;

        if(NULL != stm->current->on_arrival) {
            stm->current->on_arrival(stm->current->state, key);
        }
    }
}

inline static
void jump(struct state_machine *stm, unsigned next, selector_key_t *key) {
    if(next > stm->max_state) {
        abort();
    }
    if(stm->current != stm->states + next) {
        if(stm->current != NULL && stm->current->on_departure != NULL) {
            stm->current->on_departure(stm->current->state, key);
        }
        stm->current = stm->states + next;

        if (I(key)->client_socket != -1)
            selector_set_interest(key->s, I(key)->client_socket, stm->current->client_interest);

        if (I(key)->target_socket != -1)
            selector_set_interest(key->s, I(key)->target_socket, stm->current->target_interest);

        if (I(key)->doh.server_socket != -1)
            selector_set_interest(key->s, I(key)->doh.server_socket, stm->current->doh_interest);

        if(stm->current->rst_buffer & READ_BUFFER)
            buffer_reset(&(I(key)->read_buffer));

        if(stm->current->rst_buffer & WRITE_BUFFER)
            buffer_reset(&(I(key)->write_buffer));

        if(stm->states[next].description != NULL) {
            log(DEBUG, "Jumping to state %s\n", stm->states[next].description);
        } else {
            log(DEBUG, "Jumping to state %u\n", next);
        }

        if(NULL != stm->current->on_arrival) {
            const unsigned int ret = stm->current->on_arrival(stm->current->state, key);
            jump(stm, ret, key);
        }
    }
}

unsigned
stm_handler_read(struct state_machine *stm, selector_key_t *key) {
    handle_first(stm, key);
    if(stm->current->on_read_ready == 0) {
        abort();
    }
    const unsigned int ret = stm->current->on_read_ready(stm->current->state, key);
    jump(stm, ret, key);

    return ret;
}

unsigned
stm_handler_write(struct state_machine *stm, selector_key_t *key) {
    handle_first(stm, key);
    if(stm->current->on_write_ready == 0) {
        abort();
    }
    const unsigned int ret = stm->current->on_write_ready(stm->current->state, key);
    jump(stm, ret, key);

    return ret;
}

unsigned
stm_handler_block(struct state_machine *stm, selector_key_t *key) {
    handle_first(stm, key);
    if(stm->current->on_block_ready == 0) {
        abort();
    }
    log(DEBUG, "Handling block on state %u", stm->current->state);
    const unsigned int ret = stm->current->on_block_ready(key);
    jump(stm, ret, key);

    return ret;
}

void
stm_handler_close(struct state_machine *stm, selector_key_t *key) {
    if(stm->current != NULL && stm->current->on_departure != NULL) {
        stm->current->on_departure(stm->current->state, key);
    }
}

unsigned
stm_state(struct state_machine *stm) {
    unsigned ret = stm->initial;
    if(stm->current != NULL) {
        ret= stm->current->state;
    }
    return ret;
}
