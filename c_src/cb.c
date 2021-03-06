#include <libcouchbase/couchbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "callbacks.h"
#include "cb.h"

void *cb_connect_args(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    connect_args_t* args = (connect_args_t*)enif_alloc(sizeof(connect_args_t));

    unsigned arg_length;
    if (!enif_get_list_length(env, argv[0], &arg_length)) goto error0;
    args->host = (char *) malloc(arg_length + 1);
    if (!enif_get_string(env, argv[0], args->host, arg_length + 1, ERL_NIF_LATIN1)) goto error1;

    if (!enif_get_list_length(env, argv[1], &arg_length)) goto error1;
    args->user = (char *) malloc(arg_length + 1);
    if (!enif_get_string(env, argv[1], args->user, arg_length + 1, ERL_NIF_LATIN1)) goto error2;

    if (!enif_get_list_length(env, argv[2], &arg_length)) goto error2;
    args->pass = (char *) malloc(arg_length + 1);
    if (!enif_get_string(env, argv[2], args->pass, arg_length + 1, ERL_NIF_LATIN1)) goto error3;
     
    if (!enif_get_list_length(env, argv[3], &arg_length)) goto error3;
    args->bucket = (char *) malloc(arg_length + 1);
    if (!enif_get_string(env, argv[3], args->bucket, arg_length + 1, ERL_NIF_LATIN1)) goto error4;

    return (void*)args;

    error4:
    free(args->bucket);
    error3:
    free(args->pass);
    error2:
    free(args->user);
    error1:
    free(args->host);
    error0:
    enif_free(args);

    return NULL;
}

ERL_NIF_TERM cb_connect(ErlNifEnv* env, handle_t* handle, void* obj)
{
    connect_args_t* args = (connect_args_t*)obj;

    char * host = args->host;
    char * user = args->user;
    char * pass = args->pass;
    char * bucket = args->bucket;

    lcb_error_t err;
    struct lcb_create_st create_options;
    struct lcb_create_io_ops_st io_opts;

    io_opts.version = 0;
    io_opts.v.v0.type = LCB_IO_OPS_DEFAULT;
    io_opts.v.v0.cookie = NULL;
    
    memset(&create_options, 0, sizeof(create_options));
    err = lcb_create_io_ops(&create_options.v.v0.io, &io_opts);
    if (err != LCB_SUCCESS) {
      printf("failed create io ops\n");
      fprintf(stderr, "Failed to create IO instance: %s\n",
          lcb_strerror(NULL, err));
      return 1;
    }

    create_options.v.v0.host = host;
    create_options.v.v0.user = user;
    create_options.v.v0.bucket = bucket;
    create_options.v.v0.passwd = pass;

    err = lcb_create(&(handle->instance), &create_options);

    free(host);
    free(user);
    free(pass);
    free(bucket);

    if (err != LCB_SUCCESS) {
        return enif_make_tuple2(env, enif_make_atom(env, "error"),
                enif_make_string(env, "Failed to create libcouchbase instance\n", ERL_NIF_LATIN1));
    }

    (void)lcb_set_error_callback(handle->instance, error_callback);
    (void)lcb_set_get_callback(handle->instance, get_callback); 
    (void)lcb_set_store_callback(handle->instance, store_callback); 
    (void)lcb_set_unlock_callback(handle->instance, unlock_callback);
    (void)lcb_set_touch_callback(handle->instance, touch_callback);
    (void)lcb_set_arithmetic_callback(handle->instance, arithmetic_callback);
    (void)lcb_set_remove_callback(handle->instance, remove_callback);

    if (lcb_connect(handle->instance) != LCB_SUCCESS) {
        return enif_make_tuple2(env, enif_make_atom(env, "error"),
                enif_make_string(env, "Failed to connect libcouchbase instance to server\n", ERL_NIF_LATIN1));
    }
    lcb_wait(handle->instance);
    handle->mutex = enif_mutex_create("cberl_instance_mutex");

    return enif_make_atom(env, "ok");
}

void* cb_store_args(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    store_args_t* args = (store_args_t*)enif_alloc(sizeof(store_args_t));

    ErlNifBinary value_binary;

    if (!enif_get_int(env, argv[0], &args->operation)) goto error0;
    if (!enif_get_list_length(env, argv[1], &args->nkey)) goto error0;
    args->nkey += 1;
    args->key = (char *) malloc(args->nkey);
    if (!enif_get_string(env, argv[1], args->key, args->nkey, ERL_NIF_LATIN1)) goto error1;
    if (!enif_inspect_iolist_as_binary(env, argv[2], &value_binary)) goto error1;
    
    args->bytes = malloc(value_binary.size);
    memcpy(args->bytes, value_binary.data, value_binary.size);
    args->nbytes = value_binary.size;

    if (!enif_get_uint(env, argv[3], &args->flags)) goto error2;
    if (!enif_get_int(env, argv[4], &args->exp)) goto error2;
    if (!enif_get_uint64(env, argv[5], (ErlNifUInt64*)&args->cas)) goto error2;

    return args;

    error2:
    free(args->bytes);
    error1:
    free(args->key);
    error0:
    enif_free(args);

    return NULL;
}

ERL_NIF_TERM cb_store(ErlNifEnv* env, handle_t* handle, void* obj)
{
    store_args_t* args = (store_args_t*)obj;

    struct libcouchbase_callback cb;

    lcb_error_t ret;

    lcb_store_cmd_t cmd;
    const lcb_store_cmd_t *commands[1];

    commands[0] = &cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.v.v0.operation = args->operation;
    cmd.v.v0.key = args->key;
    cmd.v.v0.nkey = args->nkey;
    cmd.v.v0.bytes = args->bytes;
    cmd.v.v0.nbytes = args->nbytes;
    cmd.v.v0.flags = args->flags;
    cmd.v.v0.exptime = args->exp;
    cmd.v.v0.cas = args->cas;

    enif_mutex_lock(handle->mutex);
    ret = lcb_store(handle->instance, &cb, 1, commands);
    
    free(args->key);
    free(args->bytes);
    
    if (ret != LCB_SUCCESS) {
        enif_mutex_unlock(handle->mutex);
        return return_lcb_error(env, ret);
    }

    lcb_wait(handle->instance);

    enif_mutex_unlock(handle->mutex);
    if (cb.error != LCB_SUCCESS) {
        return return_lcb_error(env, cb.error);
    }
    return A_OK(env);
}

void* cb_mget_args(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    mget_args_t* args = (mget_args_t*)enif_alloc(sizeof(mget_args_t));

    ERL_NIF_TERM* currKey;
    ERL_NIF_TERM tail;

    if (!enif_get_list_length(env, argv[0], &args->numkeys)) goto error0;     
    args->keys = malloc(sizeof(char*) * args->numkeys);
    args->nkeys = malloc(sizeof(size_t) * args->numkeys);
    currKey = malloc(sizeof(ERL_NIF_TERM));
    tail = argv[0];
    unsigned int arglen;
    int i = 0;
    while(0 != enif_get_list_cell(env, tail, currKey, &tail)) {
        if (!enif_get_list_length(env, *currKey, &arglen)) goto error1;
        args->nkeys[i] = arglen + 1;
        args->keys[i] = malloc(sizeof(char) * args->nkeys[i]);
        if (!enif_get_string(env, *currKey, args->keys[i], args->nkeys[i], ERL_NIF_LATIN1)) goto error2;
        i++;
    }
    
    if (!enif_get_int(env, argv[1], &args->exp)) goto error2;

    free(currKey);

    return (void*)args;

    int f = 0;

    error2:
    free(args->keys[i]);
    error1:
    for(f = 0; f < i; f++) {
        free(args->keys[f]);
    }
    free(args->keys);
    free(args->nkeys);
    free(currKey);
    error0:
    enif_free(args);

    return NULL;
}

ERL_NIF_TERM cb_mget(ErlNifEnv* env, handle_t* handle, void* obj)
{
    mget_args_t* args = (mget_args_t*)obj;

    struct libcouchbase_callback_m cb; 

    lcb_error_t ret;
    
    ERL_NIF_TERM* results;
    ERL_NIF_TERM returnValue;
    //ErlNifBinary *databin;
    ErlNifBinary databin;
    unsigned int numkeys = args->numkeys;
    void** keys = args->keys;
    size_t* nkeys = args->nkeys;
    int exp = args->exp;
    int i = 0;

    cb.currKey = 0;
    cb.ret = malloc(sizeof(struct libcouchbase_callback*) * numkeys);

    enif_mutex_lock(handle->mutex);

    const lcb_get_cmd_t* commands[numkeys];
    i = 0;
    for (; i < numkeys; i++) {
      lcb_get_cmd_t *get = calloc(1, sizeof(*get));
      get->version = 0;
      get->v.v0.key = keys[i];
      get->v.v0.nkey = nkeys[i];
      get->v.v0.exptime = exp;
      commands[i] = get;
    }

    ret = lcb_get(handle->instance, &cb, numkeys, commands);

    if (ret != LCB_SUCCESS) {
        enif_mutex_unlock(handle->mutex); 
        return return_lcb_error(env, ret);
    }
    lcb_wait(handle->instance);
    enif_mutex_unlock(handle->mutex); 
    

    results = malloc(sizeof(ERL_NIF_TERM) * numkeys);
    i = 0; 
    for(; i < numkeys; i++) {
        if (cb.ret[i]->error == LCB_SUCCESS) {
            //databin = malloc(sizeof(ErlNifBinary));
            enif_alloc_binary(cb.ret[i]->size, &databin);
            memcpy(databin.data, cb.ret[i]->data, cb.ret[i]->size);
            results[i] = enif_make_tuple4(env, 
                    enif_make_uint64(env, cb.ret[i]->cas), 
                    enif_make_int(env, cb.ret[i]->flag), 
                    enif_make_string_len(env, cb.ret[i]->key, cb.ret[i]->nkey - 1, ERL_NIF_LATIN1),
                    enif_make_binary(env, &databin));
            free(cb.ret[i]->data);
            //free(databin);
        } else {
            results[i] = enif_make_tuple2(env, 
                    enif_make_string_len(env, cb.ret[i]->key, cb.ret[i]->nkey - 1, ERL_NIF_LATIN1),
                    return_lcb_error(env, cb.ret[i]->error));
        }
        free(cb.ret[i]->key);
        free(cb.ret[i]);
        free(keys[i]);
        free((lcb_get_cmd_t*) commands[i]);
    }

    returnValue = enif_make_list_from_array(env, results, numkeys);
    
    free(results);
    free(cb.ret);
    free(keys);
    free(nkeys);

    return enif_make_tuple2(env, A_OK(env), returnValue);
}

void* cb_getl_args(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    getl_args_t* args = (getl_args_t*)enif_alloc(sizeof(getl_args_t));

    if (!enif_get_list_length(env, argv[0], &args->nkey)) goto error0;
    args->nkey += 1;
    args->key = (char *) malloc(args->nkey);
    if (!enif_get_string(env, argv[0], args->key, args->nkey, ERL_NIF_LATIN1)) goto error1; 
    
    if (!enif_get_int(env, argv[2], &args->exp)) goto error1;

    return (void*)args;

    error1:
    free(args->key);
    error0:
    enif_free(args);

    return NULL;
}

ERL_NIF_TERM cb_getl(ErlNifEnv* env, handle_t* handle, void* obj)
{
    getl_args_t* args = (getl_args_t*)obj;

    struct libcouchbase_callback cb; 

    lcb_error_t ret; 
      
    enif_mutex_lock(handle->mutex);

    lcb_get_cmd_t cmd;
    const lcb_get_cmd_t *commands[1];
    commands[0] = &cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.v.v0.key = args->key;
    cmd.v.v0.nkey = args->nkey;
    cmd.v.v0.exptime = args->exp;
    cmd.v.v0.lock = 1;
    ret = lcb_get(handle->instance, &cb, 1, commands);
 
    free(args->key);

    if (ret != LCB_SUCCESS) {
        enif_mutex_unlock(handle->mutex); 
        return return_lcb_error(env, ret);
    }
    lcb_wait(handle->instance);
    enif_mutex_unlock(handle->mutex); 
    if(cb.error != LCB_SUCCESS) {
        return return_lcb_error(env, cb.error);
    } 

    return enif_make_tuple2(env, A_OK(env), return_value(env, &cb));   
}

void* cb_unlock_args(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    unlock_args_t* args = (unlock_args_t*)enif_alloc(sizeof(unlock_args_t));

    if (!enif_get_list_length(env, argv[0], &args->nkey)) goto error0;
    args->nkey += 1;
    args->key = (char *) malloc(args->nkey);
    if (!enif_get_string(env, argv[0], args->key, args->nkey, ERL_NIF_LATIN1)) goto error1;
    
    if (!enif_get_int(env, argv[1], &args->cas)) goto error1;

    return (void*)args;

    error1:
    free(args->key);
    error0:
    enif_free(args);

    return NULL;
}

ERL_NIF_TERM cb_unlock(ErlNifEnv* env, handle_t* handle, void* obj)
{
    unlock_args_t* args = (unlock_args_t*)obj;

    struct libcouchbase_callback cb; 

    lcb_error_t ret; //for checking responses
         
    enif_mutex_lock(handle->mutex);

    lcb_unlock_cmd_t unlock;
    memset(&unlock, 0, sizeof(unlock));
    unlock.v.v0.key = args->key;
    unlock.v.v0.nkey = args->nkey;
    unlock.v.v0.cas = args->cas;
    const lcb_unlock_cmd_t* commands[] = { &unlock };
    ret = lcb_unlock(handle->instance, &cb, 1, commands);

    free(args->key);

    if (ret != LCB_SUCCESS) {
        enif_mutex_unlock(handle->mutex); 
        return return_lcb_error(env, ret);
    }
    lcb_wait(handle->instance);
    enif_mutex_unlock(handle->mutex); 
    if(cb.error != LCB_SUCCESS) {
        return return_lcb_error(env, cb.error);
    } 
    return A_OK(env);
}

void* cb_mtouch_args(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    mtouch_args_t* args = (mtouch_args_t*)enif_alloc(sizeof(mtouch_args_t));

    ERL_NIF_TERM* currKey;
    ERL_NIF_TERM tail;
    unsigned int arglen;

    if (!enif_get_list_length(env, argv[0], &args->numkeys)) goto error0;
    args->keys = malloc(sizeof(char*) * args->numkeys);
    args->nkeys = malloc(sizeof(size_t) * args->numkeys);
    currKey = malloc(sizeof(ERL_NIF_TERM));
    tail = argv[0];
    int i = 0;
    while(0 != enif_get_list_cell(env, tail, currKey, &tail)) {
        if (!enif_get_list_length(env, *currKey, &arglen)) goto error1;
        args->nkeys[i] = arglen + 1;
        args->keys[i] = malloc(sizeof(char) * args->nkeys[i]);
        if (!enif_get_string(env, *currKey, args->keys[i], args->nkeys[i], ERL_NIF_LATIN1)) goto error2;
        i++;
    } 
    
    args->exp = malloc(sizeof(int64_t) * args->numkeys);
    tail = argv[1];
    int i2 = 0;
    while(0 != enif_get_list_cell(env, tail, currKey, &tail)) {
        if (!enif_get_int64(env, *currKey, &args->exp[i2])) goto error3;
        i2++;
    }

    free(currKey);

    return (void*)args;

    int f = 0;

    error3:
    free(args->exp);
    error2:
    free(args->keys[i]);
    error1:
    for(f = 0; f < i; f++) {
        free(args->keys[f]);
    }
    free(args->keys);
    free(args->nkeys);
    error0:
    enif_free(args);

    return NULL;
}

ERL_NIF_TERM cb_mtouch(ErlNifEnv* env, handle_t* handle, void* obj)
{
    mtouch_args_t* args = (mtouch_args_t*)obj;

    struct libcouchbase_callback_m cb; 
    int i = 0;
    lcb_error_t ret; //for checking responses
    
    ERL_NIF_TERM* results;
    ERL_NIF_TERM returnValue;

    
    cb.currKey = 0;
    cb.ret = malloc(sizeof(struct libcouchbase_callback*) * args->numkeys);

    enif_mutex_lock(handle->mutex);

    const lcb_touch_cmd_t* commands[args->numkeys];
    i = 0;
    for (; i < args->numkeys; i++) {
      lcb_touch_cmd_t* touch = calloc(1, sizeof(*touch));
      touch->version = 0;
      touch->v.v0.key = args->keys[i];
      touch->v.v0.nkey = args->nkeys[i];
      touch->v.v0.exptime = args->exp[i];
      commands[i] = touch;
    }

    ret = lcb_touch(handle->instance, &cb, args->numkeys, commands);
    
    if (ret != LCB_SUCCESS) {
        enif_mutex_unlock(handle->mutex); 
        return return_lcb_error(env, ret);
    }
    lcb_wait(handle->instance);
    enif_mutex_unlock(handle->mutex); 
    
    results = malloc(sizeof(ERL_NIF_TERM) * args->numkeys);
    i = 0; 
    for(; i < args->numkeys; i++) {
        if (cb.ret[i]->error == LCB_SUCCESS) {
            results[i] = enif_make_tuple2(env,
                    enif_make_string_len(env, cb.ret[i]->key, cb.ret[i]->nkey - 1, ERL_NIF_LATIN1),
                    A_OK(env));
        } else {
            results[i] = enif_make_tuple2(env,
                    enif_make_string_len(env, cb.ret[i]->key, cb.ret[i]->nkey - 1, ERL_NIF_LATIN1),
                    return_lcb_error(env, cb.ret[i]->error));
        }
        free(cb.ret[i]->key);
        free(cb.ret[i]);
        free(args->keys[i]);
        free((lcb_touch_cmd_t*) commands[i]);
    }
    returnValue = enif_make_list_from_array(env, results, args->numkeys);

    free(results);
    free(cb.ret);
    free(args->keys);
    free(args->nkeys);

    return enif_make_tuple2(env, A_OK(env), returnValue);
}

void* cb_arithmetic_args(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    arithmetic_args_t* args = (arithmetic_args_t*)enif_alloc(sizeof(arithmetic_args_t));

    if (!enif_get_list_length(env, argv[0], &args->nkey)) goto error0;
    args->nkey += 1;
    args->key = (char *) malloc(args->nkey);
    if (!enif_get_string(env, argv[0], args->key, args->nkey, ERL_NIF_LATIN1)) goto error1;
    if (!enif_get_int64(env, argv[1], (ErlNifSInt64*)&args->delta)) goto error1;
    if (!enif_get_uint64(env, argv[2], (ErlNifUInt64 *)&args->exp)) goto error1;
    if (!enif_get_int(env, argv[3], &args->create)) goto error1;
    if (!enif_get_uint64(env, argv[4], (ErlNifUInt64 *)&args->initial)) goto error1;

    return (void*)args;

    error1:
    free(args->key);
    error0:
    enif_free(args);

    return NULL;
}

ERL_NIF_TERM cb_arithmetic(ErlNifEnv* env, handle_t* handle, void* obj)
{
    arithmetic_args_t* args = (arithmetic_args_t*)obj;

    struct libcouchbase_callback cb; 

    lcb_error_t ret; //for checking responses
    
    enif_mutex_lock(handle->mutex);

    lcb_arithmetic_cmd_t arithmetic;
    const lcb_arithmetic_cmd_t* commands[1];
    commands[0] = &arithmetic;
    memset(&arithmetic, 0, sizeof(arithmetic));
    arithmetic.v.v0.key = args->key;
    arithmetic.v.v0.nkey = args->nkey;
    arithmetic.v.v0.initial = args->initial;
    arithmetic.v.v0.create = args->create;
    arithmetic.v.v0.delta = args->delta;
    ret = lcb_arithmetic(handle->instance, &cb, 1, commands);
 
    free(args->key);
    if (ret != LCB_SUCCESS) {
        enif_mutex_unlock(handle->mutex); 
        return return_lcb_error(env, ret);
    }
    lcb_wait(handle->instance);
    enif_mutex_unlock(handle->mutex); 
    if(cb.error != LCB_SUCCESS) {
        return return_lcb_error(env, cb.error);
    } 
    return enif_make_tuple2(env, A_OK(env), return_value(env, &cb));
}

void* cb_remove_args(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    remove_args_t* args = (remove_args_t*)enif_alloc(sizeof(remove_args_t));

    if (!enif_get_list_length(env, argv[0], &args->nkey)) goto error0;
    args->nkey += 1;
    args->key = (char *) malloc(args->nkey);
    if (!enif_get_string(env, argv[0], args->key, args->nkey, ERL_NIF_LATIN1)) goto error1;
    
    if (!enif_get_int(env, argv[1], &args->cas)) goto error1;

    return (void*)args;

    error1:
    free(args->key);
    error0:
    enif_free(args);

    return NULL;
}

ERL_NIF_TERM cb_remove(ErlNifEnv* env, handle_t* handle, void* obj)
{
    remove_args_t* args = (remove_args_t*)obj;

    struct libcouchbase_callback cb; 

    lcb_error_t ret; //for checking responses
        
    enif_mutex_lock(handle->mutex); 
    
    lcb_remove_cmd_t remove;
    const lcb_remove_cmd_t* commands[1];
    commands[0] = &remove;
    memset(&remove, 0, sizeof(remove));
    remove.v.v0.key = args->key;
    remove.v.v0.nkey = args->nkey;
    remove.v.v0.cas = args->cas;

    ret = lcb_remove(handle->instance, &cb, 1, commands);

    free(args->key);
    if (ret != LCB_SUCCESS) {
        enif_mutex_unlock(handle->mutex); 
        return return_lcb_error(env, ret);
    }
    lcb_wait(handle->instance);
    enif_mutex_unlock(handle->mutex); 
    if(cb.error != LCB_SUCCESS) {
        return return_lcb_error(env, cb.error);
    } 

    return A_OK(env);
}

ERL_NIF_TERM return_lcb_error(ErlNifEnv* env, int const value) {
    switch (value) {
        case LCB_SUCCESS:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "success"));
        case LCB_AUTH_CONTINUE:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "auth_continue"));
        case LCB_AUTH_ERROR:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "auth_error"));
        case LCB_DELTA_BADVAL:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "delta_badval"));
        case LCB_E2BIG:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "e2big"));
        case LCB_EBUSY:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "ebusy"));
        case LCB_EINTERNAL:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "einternal"));
        case LCB_EINVAL:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "einval"));
        case LCB_ENOMEM:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "enomem"));
        case LCB_ERANGE:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "erange"));
        case LCB_ERROR:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "error"));
        case LCB_ETMPFAIL:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "etmpfail"));
        case LCB_KEY_EEXISTS:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "key_eexists"));
        case LCB_KEY_ENOENT:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "key_enoent"));
        case LCB_NETWORK_ERROR:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "network_error"));
        case LCB_NOT_MY_VBUCKET:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "not_my_vbucket"));
        case LCB_NOT_STORED:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "not_stored"));
        case LCB_NOT_SUPPORTED:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "not_supported"));
        case LCB_UNKNOWN_COMMAND:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "unknown_command"));
        case LCB_UNKNOWN_HOST:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "unknown_host"));
        case LCB_PROTOCOL_ERROR:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "protocol_error"));
        case LCB_ETIMEDOUT:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "etimedout"));
        case LCB_CONNECT_ERROR:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "connect_error"));
        case LCB_BUCKET_ENOENT:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "bucket_enoent"));
        case LCB_CLIENT_ENOMEM:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "client_enomem"));
        default:
            return enif_make_tuple2(env, A_ERROR(env), enif_make_atom(env, "unknown_error"));            
    }
}

ERL_NIF_TERM return_value(ErlNifEnv* env, void * cookie) {
    struct libcouchbase_callback *cb;
    cb = (struct libcouchbase_callback *)cookie;
    ErlNifBinary value_binary;
    ERL_NIF_TERM term;
    enif_alloc_binary(cb->size, &value_binary);
    memcpy(value_binary.data, cb->data, cb->size);
    term  =   enif_make_tuple3(env, enif_make_int(env, cb->cas),
                                           enif_make_int(env, cb->flag), 
                                           enif_make_binary(env, &value_binary));
    free(cb->data);
    return term;
}
