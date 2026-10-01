/* SPDX-License-Identifier: MIT */
/* The conversation VM: conv_vm_run and the operator
 * functions beside it, ported from the instructions.
 *
 * A word-addressed stack machine. Its registers are data-segment words in the
 * original -- SP, BP, IP, the saved result, the last
 * CALLI's id -- and its memory one far-heap array holding
 * the conversation's `memslots` globals and then the stack (a separate
 * pointer names the first stack word). PUSHI_EFF pushes an index into the WHOLE array,
 * memslots + BP + operand, which is how a local is addressed: FETCHM and STO
 * take such an index.
 *
 * The VM does not know strings or the game. Three operators hand a string id
 * to the host (SAY_OP and RESPOND_OP after conv_expand_string, through the
 * imports named "say" and "respond"; STRCMP comparing two expanded strings)
 * and CALLI calls the builtin registered for its operand. A builtin that
 * waits for the player -- babl_menu -- pauses the machine AT its CALLI; the
 * host resumes it with the answer (uw_convvm_resume_calli), which is where a
 * state saved during the wait stands.
 */
#ifndef UW_CONVVM_H
#define UW_CONVVM_H

#include "uw.h"

typedef struct uw_convvm uw_convvm;

typedef struct {
    /* CALLI id: `top` is the stack slot the builtin is handed (the argument
     * count, the arguments below it). Return the result, or set *pause to
     * stop the machine at the CALLI with nothing changed. */
    int16_t (*calli)(uw_convvm *vm, uint16_t id, uint16_t *top, int *pause);
    /* SAY_OP / RESPOND_OP: the string id that was on top of the stack. The
     * original pops it and then calls the builtin, which can wait for the
     * player to page on; *pause stops the machine there, SP popped and IP
     * still on the operator. */
    void (*say)(uw_convvm *vm, uint16_t string_id, int *pause);
    void (*respond)(uw_convvm *vm, uint16_t string_id, int *pause);
    /* STRCMP: nonzero when the two strings (ids) differ, conv_op_strcmp's
     * comparison of their expansions. */
    int (*strcmp_ids)(uw_convvm *vm, uint16_t below, uint16_t top);
    void *user;
} uw_convvm_host;

struct uw_convvm {
    const uint8_t *code;       /* little-endian words */
    size_t         code_words;
    uint16_t      *mem;        /* memslots globals, then the stack */
    size_t         mem_words;
    uint16_t       memslots;
    uint16_t       ip, sp, bp, reg, calli_id;
    uw_convvm_host host;
    int            fault;      /* an index outside code or memory: the original reads on */
    long           steps;
};

/* conv_vm_run's head: SP, BP and IP zeroed; false unless code[0] is START. */
bool uw_convvm_start(uw_convvm *vm);

/* One pass of the dispatch loop. 1 to go on, 0 when the loop stops (EXIT_OP,
 * RET with nothing on the stack, an opcode past 0x29), -1 when a builtin
 * paused at its CALLI. */
int uw_convvm_step(uw_convvm *vm);

/* Steps until the loop stops or pauses, or `max_steps` pass (0 for no
 * limit). Returns what the last step returned (1 when the limit ended it). */
int uw_convvm_run(uw_convvm *vm, long max_steps);

/* A paused CALLI answered: the result over the top of the stack and into the
 * saved register, IP past the operand -- the rest of conv_vm_calli. */
void uw_convvm_resume_calli(uw_convvm *vm, int16_t result);

/* A paused SAY_OP or RESPOND_OP finished: IP past it. */
void uw_convvm_resume_say(uw_convvm *vm);

#endif
