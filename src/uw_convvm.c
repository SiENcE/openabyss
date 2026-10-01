/* SPDX-License-Identifier: MIT */
#include "uw_convvm.h"
#include "uw_conv.h"

/* conv_vm_run dispatches through the 42-word table at body
 * offset 0x1af4; the cases that are not inline call operator functions.
 * Every access goes through the far pointers, so an index
 * past the arrays reads whatever lies there: the port refuses it and says so
 * in `fault` instead. */

static uint16_t code_at(uw_convvm *vm, uint32_t i) {
    if (i >= vm->code_words) { vm->fault = 1; return 0xffff; }
    return (uint16_t)(vm->code[i * 2] | vm->code[i * 2 + 1] << 8);
}

/* A read that does not count as an access: the dispatch loads both operands
 * up front, and an operator with one never touches the second. */
static int16_t peek(const uw_convvm *vm, uint32_t i) {
    return i < vm->mem_words ? (int16_t)vm->mem[i] : 0;
}

static uint16_t *mem_at(uw_convvm *vm, uint32_t i) {
    static uint16_t sink;
    if (i >= vm->mem_words) { vm->fault = 1; sink = 0; return &sink; }
    return &vm->mem[i];
}

/* The stack is the memory array from memslots on. */
static uint16_t *st(uw_convvm *vm, uint16_t i) {
    return mem_at(vm, (uint32_t)vm->memslots + i);
}

bool uw_convvm_start(uw_convvm *vm) {
    vm->sp = 0;
    vm->bp = 0;
    vm->ip = 0;
    vm->fault = 0;
    vm->steps = 0;
    return code_at(vm, 0) == UW_OP_START;
}

/* The binary operators: both operands off the stack, the result pushed --
 * `below op top`. */
static void binop(uw_convvm *vm, int16_t v) {
    vm->sp--;
    *st(vm, vm->sp) = (uint16_t)v;
}

int uw_convvm_step(uw_convvm *vm) {
    uint16_t op = code_at(vm, vm->ip);
    int16_t top, below;
    vm->steps++;
    if (op > UW_CONV_MAX_OP) return 0;
    top = peek(vm, (uint32_t)vm->memslots + vm->sp);
    below = peek(vm, (uint32_t)vm->memslots + (uint16_t)(vm->sp - 1));
    switch (op) {
    case 0x00: vm->ip++; break;                                   /* NOP */
    case 0x01: binop(vm, (int16_t)(top + below)); vm->ip++; break; /* OPADD */
    case 0x02: binop(vm, (int16_t)(below * top)); vm->ip++; break; /* OPMUL: imul, low word */
    case 0x03: binop(vm, (int16_t)(below - top)); vm->ip++; break; /* OPSUB */
    case 0x04:                                                    /* OPDIV: by zero, 0x7fff */
        if (below == -32768 && top == -1) { vm->fault = 1; return 0; }   /* idiv's trap */
        binop(vm, top == 0 ? 0x7fff : (int16_t)(below / top));
        vm->ip++;
        break;
    case 0x05:                                                    /* OPMOD: by zero, 0x7fff */
        if (below == -32768 && top == -1) { vm->fault = 1; return 0; }
        binop(vm, top == 0 ? 0x7fff : (int16_t)(below % top));
        vm->ip++;
        break;
    case 0x06: binop(vm, (int16_t)(below != 0 || top != 0)); vm->ip++; break; /* OPOR */
    case 0x07: binop(vm, (int16_t)(below != 0 && top != 0)); vm->ip++; break; /* OPAND */
    case 0x08: *st(vm, vm->sp) = (uint16_t)(top == 0); vm->ip++; break;      /* OPNOT */
    case 0x09: binop(vm, (int16_t)(below > top)); vm->ip++; break;   /* TSTGT */
    case 0x0a: binop(vm, (int16_t)(below >= top)); vm->ip++; break;  /* TSTGE */
    case 0x0b: binop(vm, (int16_t)(below < top)); vm->ip++; break;   /* TSTLT */
    case 0x0c: binop(vm, (int16_t)(below <= top)); vm->ip++; break;  /* TSTLE */
    case 0x0d: binop(vm, (int16_t)(below == top)); vm->ip++; break;  /* TSTEQ */
    case 0x0e: binop(vm, (int16_t)(below != top)); vm->ip++; break;  /* TSTNE */
    case 0x0f: vm->ip = code_at(vm, (uint32_t)vm->ip + 1); break;    /* JMP: absolute */
    case 0x10:                                                    /* BEQ: taken when the top is 0 */
        vm->sp--;
        vm->ip = top == 0 ? (uint16_t)(vm->ip + code_at(vm, (uint32_t)vm->ip + 1) + 1)
                          : (uint16_t)(vm->ip + 2);
        break;
    case 0x11:                                                    /* BNE: taken when it is not */
        vm->sp--;
        vm->ip = top != 0 ? (uint16_t)(vm->ip + code_at(vm, (uint32_t)vm->ip + 1) + 1)
                          : (uint16_t)(vm->ip + 2);
        break;
    case 0x12:                                                    /* BRA: relative, to the operand */
        vm->ip = (uint16_t)(vm->ip + code_at(vm, (uint32_t)vm->ip + 1) + 1);
        break;
    case 0x13:                                                    /* CALL: the return IP pushed */
        vm->sp++;
        *st(vm, vm->sp) = (uint16_t)(vm->ip + 2);
        vm->ip = code_at(vm, (uint32_t)vm->ip + 1);
        break;
    case 0x14: {                                                  /* CALLI */
        uint16_t id = code_at(vm, (uint32_t)vm->ip + 1);
        int pause = 0;
        int16_t r;
        vm->calli_id = id;
        r = vm->host.calli ? vm->host.calli(vm, id, st(vm, vm->sp), &pause) : 0;
        if (pause) return -1;
        uw_convvm_resume_calli(vm, r);
        break;
    }
    case 0x15:                                                    /* RET: stops on an empty stack */
        if ((int16_t)vm->sp <= 0) return 0;
        vm->ip = *st(vm, vm->sp);
        vm->sp--;
        break;
    case 0x16:                                                    /* PUSHI */
        vm->sp++;
        *st(vm, vm->sp) = code_at(vm, (uint32_t)vm->ip + 1);
        vm->ip = (uint16_t)(vm->ip + 2);
        break;
    case 0x17:                                                    /* PUSHI_EFF: memslots + operand + BP */
        vm->sp++;
        *st(vm, vm->sp) = (uint16_t)(vm->memslots + code_at(vm, (uint32_t)vm->ip + 1) + vm->bp);
        vm->ip = (uint16_t)(vm->ip + 2);
        break;
    case 0x18: vm->sp--; vm->ip++; break;                         /* POP */
    case 0x19:                                                    /* SWAP */
        *st(vm, vm->sp) = (uint16_t)below;
        *st(vm, (uint16_t)(vm->sp - 1)) = (uint16_t)top;
        vm->ip++;
        break;
    case 0x1a:                                                    /* PUSHBP */
        vm->sp++;
        *st(vm, vm->sp) = vm->bp;
        vm->ip++;
        break;
    case 0x1b: vm->bp = (uint16_t)top; vm->sp--; vm->ip++; break; /* POPBP */
    case 0x1c: vm->bp = vm->sp; vm->ip++; break;                  /* SPTOBP */
    case 0x1d: vm->sp = vm->bp; vm->ip++; break;                  /* BPTOSP */
    case 0x1e: vm->sp = (uint16_t)(vm->sp + top - 1); vm->ip++; break; /* ADDSP */
    case 0x1f:                                                    /* FETCHM: memory[top] */
        *st(vm, vm->sp) = *mem_at(vm, (uint16_t)top);
        vm->ip++;
        break;
    case 0x20:                                                    /* STO: memory[below] = top */
        *mem_at(vm, (uint16_t)below) = (uint16_t)top;
        vm->sp = (uint16_t)(vm->sp - 2);
        vm->ip++;
        break;
    case 0x21: binop(vm, (int16_t)(top + below - 1)); vm->ip++; break; /* OFFSET */
    case 0x22: vm->ip++; break;                                   /* START */
    case 0x23: vm->reg = (uint16_t)top; vm->ip++; break;          /* SAVE_REG: not popped */
    case 0x24:                                                    /* PUSH_REG */
        vm->sp++;
        *st(vm, vm->sp) = vm->reg;
        vm->ip++;
        break;
    case 0x25:                                                    /* STRCMP: 1 when equal */
        binop(vm, (int16_t)(vm->host.strcmp_ids
                            ? vm->host.strcmp_ids(vm, (uint16_t)below, (uint16_t)top) == 0 : 0));
        vm->ip++;
        break;
    case 0x26: return 0;                                          /* EXIT_OP */
    case 0x27: {                                                  /* SAY_OP */
        int pause = 0;
        vm->sp--;
        if (vm->host.say) vm->host.say(vm, (uint16_t)top, &pause);
        if (pause) return -1;
        vm->ip++;
        break;
    }
    case 0x28: {                                                  /* RESPOND_OP */
        int pause = 0;
        vm->sp--;
        if (vm->host.respond) vm->host.respond(vm, (uint16_t)top, &pause);
        if (pause) return -1;
        vm->ip++;
        break;
    }
    case 0x29: *st(vm, vm->sp) = (uint16_t)-top; vm->ip++; break; /* OPNEG */
    }
    return 1;
}

int uw_convvm_run(uw_convvm *vm, long max_steps) {
    long n = 0;
    for (;;) {
        int r = uw_convvm_step(vm);
        if (r != 1) return r;
        if (max_steps && ++n >= max_steps) return 1;
    }
}

void uw_convvm_resume_say(uw_convvm *vm) {
    vm->ip++;
}

void uw_convvm_resume_calli(uw_convvm *vm, int16_t result) {
    *st(vm, vm->sp) = (uint16_t)result;
    vm->reg = (uint16_t)result;
    vm->ip = (uint16_t)(vm->ip + 2);
}
