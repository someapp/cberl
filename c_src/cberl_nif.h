#ifndef CBERL_NIF_H
#define CBERL_NIF_H

#include "erl_nif.h"
#include "cberl.h"

typedef struct task {
    ErlNifPid* pid;
    unsigned int cmd;
    void *args;
} task_t;

// Command enum
#define CMD_CONNECT     0
#define CMD_STORE       1
#define CMD_MGET        2
#define CMD_GETL        3
#define CMD_UNLOCK      4
#define CMD_MTOUCH      5
#define CMD_ARITHMETIC  6
#define CMD_REMOVE      7

static void* worker(void *obj);
static int translate_cmd(char *buf);

#endif
