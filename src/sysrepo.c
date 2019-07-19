/**
 * @file sysrepo.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief sysrepo API routines
 *
 * @copyright
 * Copyright 2018 Deutsche Telekom AG.
 * Copyright 2018 - 2019 CESNET, z.s.p.o.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#include <libyang/libyang.h>

/**
 * @brief Allocate a new connection structure.
 *
 * @param[in] opts Connection options.
 * @param[out] conn_p Allocated connection.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
sr_conn_new(const sr_conn_options_t opts, sr_conn_ctx_t **conn_p)
{
    sr_conn_ctx_t *conn;
    sr_error_info_t *err_info = NULL;

    conn = calloc(1, sizeof *conn);
    SR_CHECK_MEM_RET(!conn, err_info);

    if ((err_info = sr_shmmain_ly_ctx_init(conn))) {
        goto error1;
    }

    conn->opts = opts;

    if ((err_info = sr_mutex_init(&conn->ptr_lock, 0))) {
        goto error2;
    }

    if ((err_info = sr_shmmain_createlock_open(&conn->main_shm_create_lock))) {
        goto error3;
    }

    if ((err_info = sr_rwlock_init(&conn->main_shm_remap_lock, 0))) {
        goto error4;
    }

    conn->main_shm.fd = -1;
    conn->ext_shm.fd = -1;

    *conn_p = conn;
    return NULL;

error4:
    close(conn->main_shm_create_lock);
error3:
    pthread_mutex_destroy(&conn->ptr_lock);
error2:
    ly_ctx_destroy(conn->ly_ctx, NULL);
error1:
    free(conn);
    return err_info;
}

/**
 * @brief Free a connection structure.
 *
 * @param[in] conn Connection to free.
 */
static void
sr_conn_free(sr_conn_ctx_t *conn)
{
    ly_ctx_destroy(conn->ly_ctx, NULL);
    pthread_mutex_destroy(&conn->ptr_lock);
    if (conn->main_shm_create_lock > -1) {
        close(conn->main_shm_create_lock);
    }
    sr_rwlock_destroy(&conn->main_shm_remap_lock);
    sr_shm_clear(&conn->main_shm);
    sr_shm_clear(&conn->ext_shm);
    free(conn);
}

API int
sr_connect(const sr_conn_options_t opts, sr_conn_ctx_t **conn_p)
{
    sr_error_info_t *err_info = NULL;
    sr_conn_ctx_t *conn = NULL;
    struct lyd_node *sr_mods = NULL;
    int created = 0, changed = 0, exists;

    SR_CHECK_ARG_APIRET(!conn_p, NULL, err_info);

    /* check that all required directories exist */
    if ((err_info = sr_shmmain_check_dirs())) {
        goto cleanup;
    }

    /* create basic connection structure */
    if ((err_info = sr_conn_new(opts, &conn))) {
        goto cleanup;
    }

    /* CREATE LOCK */
    if ((err_info = sr_shmmain_createlock(conn->main_shm_create_lock))) {
        goto cleanup;
    }

    /* open the main SHM */
    if ((err_info = sr_shmmain_shm_main_open(&conn->main_shm, &created))) {
        goto cleanup_unlock;
    }

    /* open the main ext SHM */
    if ((err_info = sr_shmmain_shm_ext_open(&conn->ext_shm, created))) {
        goto cleanup_unlock;
    }

    /* check whether any internal module data exist */
    if ((err_info = sr_shmmain_ly_int_data_exists(&exists))) {
        goto cleanup_unlock;
    }
    if (!exists) {
        /* create new persistent module data file */
        if ((err_info = sr_shmmain_ly_int_data_create(conn, &sr_mods))) {
            goto cleanup_unlock;
        }
        changed = 1;
    } else if (created || (!(opts & SR_CONN_NO_SCHED_CHANGES) && !((sr_main_shm_t *)conn->main_shm.addr)->conn_state.conn_count)) {
        /* if there are no connections, parse internal data and apply scheduled changes */
        if ((err_info = sr_shmmain_ly_int_data_parse(conn, &sr_mods))) {
            goto cleanup_unlock;
        }
        if ((err_info = sr_shmmain_ly_int_data_sched_apply(conn, sr_mods, &changed))) {
            goto cleanup_unlock;
        }
    }

    if (changed) {
        /* store updated internal sysrepo data */
        if ((err_info = sr_shmmain_ly_int_data_print(&sr_mods))) {
            goto cleanup_unlock;
        }
    }

    if (changed || created) {
        /* clear all main SHM modules (if main SHM was just created, there aren't any anyway) */
        if ((err_info = sr_shm_remap(&conn->main_shm, sizeof(sr_main_shm_t)))) {
            goto cleanup_unlock;
        }

        /* clear main ext SHM (there can be no connections and no modules) */
        if ((err_info = sr_shm_remap(&conn->ext_shm, sizeof(size_t)))) {
            goto cleanup_unlock;
        }
        /* set wasted mem to 0 */
        *((size_t *)conn->ext_shm.addr) = 0;

        /* add all the modules in internal sysrepo data into main SHM */
        if ((err_info = sr_shmmain_shm_add(conn, sr_mods->child))) {
            goto cleanup_unlock;
        }

        /* update version */
        ++((sr_main_shm_t *)conn->main_shm.addr)->ver;
    }

    /* update libyang context with current modules from SHM */
    if ((err_info = sr_shmmain_ly_ctx_update(conn))) {
        goto cleanup_unlock;
    }

    if (created) {
        /* copy full datastore from <startup> to <running> */
        if ((err_info = sr_shmmain_files_startup2running(conn))) {
            goto cleanup_unlock;
        }
    }

    /* CREATE UNLOCK */
    sr_shmmain_createunlock(conn->main_shm_create_lock);

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 1))) {
        goto cleanup;
    }

    /* add connection into state */
    err_info = sr_shmmain_state_add_conn(conn);

    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 1);

    /* success */
    goto cleanup;

cleanup_unlock:
    /* CREATE UNLOCK */
    sr_shmmain_createunlock(conn->main_shm_create_lock);

cleanup:
    lyd_free_withsiblings(sr_mods);
    if (err_info) {
        sr_conn_free(conn);
        if (created) {
            /* remove any created SHM so it is not considered properly created */
            shm_unlink(SR_MAIN_SHM);
            shm_unlink(SR_EXT_SHM);
        }
    } else {
        *conn_p = conn;
    }
    return sr_api_ret(NULL, err_info);
}

API int
sr_disconnect(sr_conn_ctx_t *conn)
{
    sr_error_info_t *err_info = NULL;
    int ret = SR_ERR_OK, rc;

    if (!conn) {
        return ret;
    }

    /* stop all the sessions */
    while (conn->session_count) {
        /* API function */
        rc = sr_session_stop(conn->sessions[0]);
        if (rc != SR_ERR_OK) {
            ret = rc;
        }
    }

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 1))) {
        ret = err_info->err_code;
        sr_errinfo_free(&err_info);
    } else {
        /* remove from state */
        sr_shmmain_state_del_conn((sr_main_shm_t *)conn->main_shm.addr, conn->ext_shm.addr, conn, getpid());

        /* SHM WRITE UNLOCK */
        sr_shmmain_unlock(conn, 1, 1);
    }

    /* free cache */
    if (conn->opts & SR_CONN_CACHE_RUNNING) {
        sr_rwlock_destroy(&conn->mod_cache.lock);
        lyd_free_withsiblings(conn->mod_cache.data);
        free(conn->mod_cache.mods);
    }

    /* free attributes */
    sr_conn_free(conn);

    return ret;
}

API int
sr_connection_count(uint32_t *conn_count)
{
    sr_error_info_t *err_info = NULL;
    sr_main_shm_t *main_shm;
    sr_shm_t shm = SR_SHM_INITIALIZER;
    int shm_lock = -1;

    SR_CHECK_ARG_APIRET(!conn_count, NULL, err_info);

    if ((err_info = sr_shmmain_createlock_open(&shm_lock))) {
        goto cleanup;
    }

    /* CREATE LOCK */
    if ((err_info = sr_shmmain_createlock(shm_lock))) {
        goto cleanup;
    }

    /* open the main SHM */
    err_info = sr_shmmain_shm_main_open(&shm, NULL);

    /* CREATE UNLOCK */
    sr_shmmain_createunlock(shm_lock);

    if (err_info) {
        goto cleanup;
    }
    if (shm.fd == -1) {
        /* main SHM does not even exist yet */
        *conn_count = 0;
        goto cleanup;
    }

    main_shm = (sr_main_shm_t *)shm.addr;

    /* MAIN SHM READ LOCK */
    if ((err_info = sr_rwlock(&main_shm->lock, SR_MAIN_LOCK_TIMEOUT * 1000, 0, __func__))) {
        goto cleanup;
    }

    *conn_count = main_shm->conn_state.conn_count;

    /* MAIN SHM READ UNLOCK */
    sr_rwunlock(&main_shm->lock, 0, __func__);

    /* success */

cleanup:
    if (shm_lock > -1) {
        close(shm_lock);
    }
    sr_shm_clear(&shm);
    return sr_api_ret(NULL, err_info);
}

API int
sr_connection_recover(void)
{
    sr_error_info_t *err_info = NULL;
    sr_main_shm_t *main_shm;
    sr_shm_t shm_main = SR_SHM_INITIALIZER, shm_ext = SR_SHM_INITIALIZER;
    int shm_lock = -1;

    if ((err_info = sr_shmmain_createlock_open(&shm_lock))) {
        goto cleanup;
    }

    /* CREATE LOCK */
    if ((err_info = sr_shmmain_createlock(shm_lock))) {
        goto cleanup;
    }

    /* open the main SHM */
    err_info = sr_shmmain_shm_main_open(&shm_main, NULL);

    /* CREATE UNLOCK */
    sr_shmmain_createunlock(shm_lock);

    if (err_info) {
        goto cleanup;
    }
    if (shm_main.fd == -1) {
        /* main SHM does not even exist yet */
        goto cleanup;
    }

    main_shm = (sr_main_shm_t *)shm_main.addr;

    /* MAIN SHM WRITE LOCK */
    if ((err_info = sr_rwlock(&main_shm->lock, SR_MAIN_LOCK_TIMEOUT * 1000, 1, __func__))) {
        goto cleanup;
    }

    /* open main ext SHM */
    if ((err_info = sr_shmmain_shm_ext_open(&shm_ext, 0))) {
        goto cleanup_unlock;
    }

    /* clear all stale connections */
    if ((err_info = sr_shmmain_state_recover(&shm_main, &shm_ext))) {
        goto cleanup_unlock;
    }

    /* success */

cleanup_unlock:
    /* MAIN SHM WRITE UNLOCK */
    sr_rwunlock(&main_shm->lock, 1, __func__);

cleanup:
    if (shm_lock > -1) {
        close(shm_lock);
    }
    sr_shm_clear(&shm_main);
    sr_shm_clear(&shm_ext);
    return sr_api_ret(NULL, err_info);
}

API const struct ly_ctx *
sr_get_context(sr_conn_ctx_t *conn)
{
    if (!conn) {
        return NULL;
    }

    return conn->ly_ctx;
}

API void
sr_set_diff_check_callback(sr_conn_ctx_t *conn, sr_diff_check_cb callback)
{
    sr_error_info_t *err_info = NULL;

    if (!conn) {
        return;
    }

    if (geteuid()) {
        /* not a root */
        sr_errinfo_new(&err_info, SR_ERR_UNAUTHORIZED, NULL, "Root access required.");
        sr_errinfo_free(&err_info);
        return;
    }

    conn->diff_check_cb = callback;
}

API int
sr_session_start(sr_conn_ctx_t *conn, const sr_datastore_t datastore, sr_session_ctx_t **session)
{
    sr_error_info_t *err_info = NULL;
    sr_main_shm_t *main_shm;
    uid_t uid;

    SR_CHECK_ARG_APIRET(!conn || !session, NULL, err_info);

    *session = calloc(1, sizeof **session);
    if (!*session) {
        SR_ERRINFO_MEM(&err_info);
        return sr_api_ret(NULL, err_info);
    }

    /* REMAP READ LOCK */
    if ((err_info = sr_rwlock(&conn->main_shm_remap_lock, SR_MAIN_LOCK_TIMEOUT * 1000, 0, __func__))) {
        goto error;
    }

    /* use new SR session ID and increment it */
    main_shm = (sr_main_shm_t *)conn->main_shm.addr;
    (*session)->sid.sr = ATOMIC_INC_RELAXED(main_shm->new_sr_sid);
    if ((*session)->sid.sr == (uint32_t)(ATOMIC_T_MAX - 1)) {
        /* the value in the main SHM is actually ATOMIC_T_MAX and calling another INC would cause an overflow */
        ATOMIC_STORE_RELAXED(main_shm->new_sr_sid, 1);
    }

    /* REMAP READ UNLOCK */
    sr_rwunlock(&conn->main_shm_remap_lock, 0, __func__);

    /* remember current real process owner */
    uid = getuid();
    if ((err_info = sr_get_pwd(&uid, &(*session)->sid.user))) {
        goto error;
    }

    /* add the session into conn */
    if ((err_info = sr_ptr_add(&conn->ptr_lock, (void ***)&conn->sessions, &conn->session_count, *session))) {
        goto error;
    }

    (*session)->conn = conn;
    (*session)->ds = datastore;
    if ((err_info = sr_mutex_init(&(*session)->ptr_lock, 0))) {
        goto error;
    }

    SR_LOG_INF("Session %u (user \"%s\") created.", (*session)->sid.sr, (*session)->sid.user);

    return sr_api_ret(NULL, NULL);

error:
    free((*session)->sid.user);
    free(*session);
    *session = NULL;
    return sr_api_ret(NULL, err_info);
}

API int
sr_session_stop(sr_session_ctx_t *session)
{
    sr_error_info_t *err_info = NULL, *tmp_err;
    uint32_t i;

    if (!session) {
        return sr_api_ret(NULL, NULL);
    }

    /* remove ourselves from conn sessions */
    tmp_err = sr_ptr_del(&session->conn->ptr_lock, (void ***)&session->conn->sessions, &session->conn->session_count, session);
    if (tmp_err) {
        /* continue */
        sr_errinfo_merge(&err_info, tmp_err);
    }

    /* SHM WRITE LOCK */
    if ((tmp_err = sr_shmmain_lock_remap(session->conn, 1, 0))) {
        /* continue */
        sr_errinfo_merge(&err_info, tmp_err);
    } else {
        /* release any held locks */
        sr_shmmod_release_locks(session->conn, session->sid);

        /* stop all subscriptions of this session */
        while (session->subscription_count) {
            if ((tmp_err = sr_subs_session_del(session, session->subscriptions[0]))) {
                /* continue */
                sr_errinfo_merge(&err_info, tmp_err);
            }
        }

        /* SHM WRITE UNLOCK */
        sr_shmmain_unlock(session->conn, 1, 0);
    }

    /* free attributes */
    free(session->sid.user);
    for (i = 0; i < SR_WRITABLE_DS_COUNT; ++i) {
        lyd_free_withsiblings(session->dt[i].edit);
    }
    sr_errinfo_free(&session->err_info);
    pthread_mutex_destroy(&session->ptr_lock);
    free(session);
    return sr_api_ret(NULL, err_info);
}

API int
sr_session_switch_ds(sr_session_ctx_t *session, sr_datastore_t ds)
{
    sr_error_info_t *err_info = NULL;

    SR_CHECK_ARG_APIRET(!session, session, err_info);

    session->ds = ds;
    return sr_api_ret(session, err_info);
}

API sr_datastore_t
sr_session_get_ds(sr_session_ctx_t *session)
{
    if (!session) {
        return 0;
    }

    return session->ds;
}

API int
sr_get_error(sr_session_ctx_t *session, const sr_error_info_t **error_info)
{
    sr_error_info_t *err_info = NULL;

    SR_CHECK_ARG_APIRET(!session || !error_info, session, err_info);

    *error_info = session->err_info;

    /* do not modify session errors */
    return SR_ERR_OK;
}

API int
sr_set_error(sr_session_ctx_t *session, const char *message, const char *path)
{
    sr_error_info_t *err_info = NULL;

    SR_CHECK_ARG_APIRET(!session || ((session->ev != SR_SUB_EV_CHANGE) && (session->ev != SR_SUB_EV_UPDATE)
            && (session->ev != SR_SUB_EV_OPER) && (session->ev != SR_SUB_EV_RPC)) || !message, session, err_info);

    sr_errinfo_new(&err_info, SR_ERR_OK, path, message);

    /* set the error and return its return code (SR_ERR_OK) */
    return sr_api_ret(session, err_info);
}

API uint32_t
sr_session_get_id(sr_session_ctx_t *session)
{
    if (!session) {
        return 0;
    }

    return session->sid.sr;
}

API void
sr_session_set_nc_id(sr_session_ctx_t *session, uint32_t nc_sid)
{
    if (!session) {
        return;
    }

    session->sid.nc = nc_sid;
}

API uint32_t
sr_session_get_nc_id(sr_session_ctx_t *session)
{
    if (!session) {
        return 0;
    }

    return session->sid.nc;
}

API int
sr_session_set_user(sr_session_ctx_t *session, const char *user)
{
    sr_error_info_t *err_info = NULL;
    uid_t uid;

    SR_CHECK_ARG_APIRET(!session || !user, session, err_info);

    if (geteuid()) {
        /* not a root */
        sr_errinfo_new(&err_info, SR_ERR_UNAUTHORIZED, NULL, "Root access required.");
        return sr_api_ret(session, err_info);
    }

    /* check that the user is valid */
    if ((err_info = sr_get_pwd(&uid, (char **)&user))) {
        return sr_api_ret(session, err_info);
    }

    /* replace the user */
    free(session->sid.user);
    session->sid.user = strdup(user);
    if (!session->sid.user) {
        SR_ERRINFO_MEM(&err_info);
    }

    return sr_api_ret(session, err_info);
}

API const char *
sr_session_get_user(sr_session_ctx_t *session)
{
    sr_error_info_t *err_info = NULL;

    if (!session) {
        return NULL;
    }

    if (geteuid()) {
        /* not a root */
        sr_errinfo_new(&err_info, SR_ERR_UNAUTHORIZED, NULL, "Root access required.");
        sr_api_ret(session, err_info);
        return NULL;
    }

    /* return the user */
    return session->sid.user;
}

API sr_conn_ctx_t *
sr_session_get_connection(sr_session_ctx_t *session)
{
    if (!session) {
        return NULL;
    }

    return session->conn;
}

API const char *
sr_get_repo_path(void)
{
    char *value;

    value = getenv(SR_REPO_PATH_ENV);
    if (value) {
        return value;
    }

    return SR_REPO_PATH;
}

/**
 * @brief Learn YANG module name and format.
 *
 * @param[in] schema_path Path to the module file.
 * @param[out] module_name Name of the module.
 * @param[out] format Module format.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
sr_get_module_name_format(const char *schema_path, char **module_name, LYS_INFORMAT *format)
{
    sr_error_info_t *err_info = NULL;
    const char *ptr;
    int index;

    /* learn the format */
    if ((strlen(schema_path) > 4) && !strcmp(schema_path + strlen(schema_path) - 4, ".yin")) {
        *format = LYS_YIN;
        ptr = schema_path + strlen(schema_path) - 4;
    } else if ((strlen(schema_path) > 5) && !strcmp(schema_path + strlen(schema_path) - 5, ".yang")) {
        *format = LYS_YANG;
        ptr = schema_path + strlen(schema_path) - 5;
    } else {
        sr_errinfo_new(&err_info, SR_ERR_INVAL_ARG, NULL, "Unknown format of module \"%s\".", schema_path);
        return err_info;
    }

    /* parse module name */
    for (index = 0; (ptr != schema_path) && (ptr[0] != '/'); ++index, --ptr);
    if (ptr[0] == '/') {
        ++ptr;
        --index;
    }
    *module_name = strndup(ptr, index);
    SR_CHECK_MEM_RET(!*module_name, err_info);
    ptr = strchr(*module_name, '@');
    if (ptr) {
        /* truncate revision */
        ((char *)ptr)[0] = '\0';
    }

    return NULL;
}

/**
 * @brief Parse a YANG module.
 *
 * @param[in] ly_ctx Context to use.
 * @param[in] schema_path Path to the module file.
 * @param[in] format Module format.
 * @param[in] search_dir Optional search directory.
 * @return err_info, NULL on success.
 */
static const struct lys_module *
sr_parse_module(struct ly_ctx *ly_ctx, const char *schema_path, LYS_INFORMAT format, const char *search_dir)
{
    const struct lys_module *ly_mod;
    const char * const *search_dirs;
    int index;

    /* add searchdir if not already there */
    if (search_dir) {
        search_dirs = ly_ctx_get_searchdirs(ly_ctx);
        for (index = 0; search_dirs[index]; ++index) {
            if (!strcmp(search_dirs[index], search_dir)) {
                break;
            }
        }
        if (!search_dirs[index]) {
            ly_ctx_set_searchdir(ly_ctx, search_dir);
            /* it could have been moved on realloc */
            search_dirs = ly_ctx_get_searchdirs(ly_ctx);
        }
    }

    /* parse the module */
    ly_mod = lys_parse_path(ly_ctx, schema_path, format);

    /* remove search dir */
    if (search_dir && search_dirs[index]) {
        ly_ctx_unset_searchdirs(ly_ctx, index);
    }

    return ly_mod;
}

/**
 * @brief Defragment main SHM if needed.
 *
 * @param[in] conn Connection to use.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
sr_check_main_shm_defrag(sr_conn_ctx_t *conn)
{
    sr_error_info_t *err_info = NULL;
    char *buf;

    if (*((size_t *)conn->ext_shm.addr) <= SR_SHM_WASTED_MAX_MEM) {
        /* not enough wasted memory, leave it as it is */
        return NULL;
    }

    SR_LOG_DBGMSG("#SHM before defrag");
    sr_shmmain_ext_print(&conn->main_shm, conn->ext_shm.addr, conn->ext_shm.size);

    /* defrag mem into a separate memory */
    if ((err_info = sr_shmmain_ext_defrag(&conn->main_shm, &conn->ext_shm, &buf))) {
        return err_info;
    }

    /* remap main ext SHM */
    if ((err_info = sr_shm_remap(&conn->ext_shm, conn->ext_shm.size - *((size_t *)conn->ext_shm.addr)))) {
        goto cleanup;
    }

    SR_LOG_INF("Main ext SHM was defragmented and %u B were saved.", *((size_t *)conn->ext_shm.addr));

    /* copy the defragmented memory into ext SHM (has wasted set to 0) */
    memcpy(conn->ext_shm.addr, buf, conn->ext_shm.size);

    SR_LOG_DBGMSG("#SHM after defrag");
    sr_shmmain_ext_print(&conn->main_shm, conn->ext_shm.addr, conn->ext_shm.size);

    /* success */

cleanup:
    free(buf);
    return err_info;
}

API int
sr_install_module(sr_conn_ctx_t *conn, const char *schema_path, const char *search_dir,
        const char **features, int feat_count)
{
    sr_error_info_t *err_info = NULL;
    const struct lys_module *ly_mod;
    LYS_INFORMAT format;
    char *mod_name = NULL;
    int index;

    SR_CHECK_ARG_APIRET(!conn || !schema_path, NULL, err_info);

    /* learn module name and format */
    if ((err_info = sr_get_module_name_format(schema_path, &mod_name, &format))) {
        return sr_api_ret(NULL, err_info);
    }

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 1))) {
        free(mod_name);
        return sr_api_ret(NULL, err_info);
    }

    /* check whether the module is not already in the context */
    ly_mod = ly_ctx_get_module(conn->ly_ctx, mod_name, NULL, 1);
    if (ly_mod && ly_mod->implemented) {
        /* it is currently in the context, try to parse it again to check revisions */
        ly_mod = lys_parse_path(conn->ly_ctx, schema_path, format);
        if (!ly_mod) {
            sr_errinfo_new_ly_first(&err_info, conn->ly_ctx);
            sr_errinfo_new(&err_info, SR_ERR_EXISTS, NULL, "Module \"%s\" is already in sysrepo.", mod_name);
            goto cleanup_unlock;
        }

        /* same modules, so if it is scheduled for deletion, we can unschedule it */
        err_info = sr_shmmain_unsched_del_module_with_imps(conn, ly_mod);
        if (err_info && (err_info->err_code == SR_ERR_NOT_FOUND)) {
            sr_errinfo_free(&err_info);
            sr_errinfo_new(&err_info, SR_ERR_EXISTS, NULL, "Module \"%s\" is already in sysrepo.", ly_mod->name);
            goto cleanup_unlock;
        }
        goto cleanup_unlock;
    }
    free(mod_name);
    mod_name = NULL;

    /* parse the module */
    if (!(ly_mod = sr_parse_module(conn->ly_ctx, schema_path, format, search_dir))) {
        sr_errinfo_new_ly(&err_info, conn->ly_ctx);
        goto cleanup_unlock;
    }

    /* enable all features */
    for (index = 0; index < feat_count; ++index) {
        if (lys_features_enable(ly_mod, features[index])) {
            ly_ctx_remove_module(ly_mod, NULL);
            goto cleanup_unlock;
        }
    }

    /* store the model file and create data files for module and all of its imports */
    if ((err_info = sr_create_module_files_with_imps_r(ly_mod))) {
        ly_ctx_remove_module(ly_mod, NULL);
        goto cleanup_unlock;
    }

    /* add into main SHM */
    if ((err_info = sr_shmmain_add_module_with_imps(conn, ly_mod))) {
        goto cleanup_unlock;
    }

    /* update version */
    conn->main_ver = ++((sr_main_shm_t *)conn->main_shm.addr)->ver;

    /* defrag main SHM if needed */
    if ((err_info = sr_check_main_shm_defrag(conn))) {
        goto cleanup_unlock;
    }

    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 1);

    return sr_api_ret(NULL, NULL);

cleanup_unlock:
    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 1);

    free(mod_name);
    return sr_api_ret(NULL, err_info);
}

API int
sr_remove_module(sr_conn_ctx_t *conn, const char *module_name)
{
    sr_error_info_t *err_info = NULL;
    const struct lys_module *ly_mod;

    SR_CHECK_ARG_APIRET(!conn || !module_name, NULL, err_info);

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 0))) {
        return sr_api_ret(NULL, err_info);
    }

    /* try to find this module */
    ly_mod = ly_ctx_get_module(conn->ly_ctx, module_name, NULL, 1);
    if (!ly_mod || !ly_mod->implemented) {
        sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
        goto error_unlock;
    }

    /* check write permission */
    if ((err_info = sr_perm_check(module_name, 1))) {
        goto error_unlock;
    }

    /* schedule module removal from sysrepo */
    if ((err_info = sr_shmmain_deferred_del_module(conn, module_name))) {
        goto error_unlock;
    }

    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 0);

    return sr_api_ret(NULL, NULL);

error_unlock:
    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 0);
    return sr_api_ret(NULL, err_info);
}

API int
sr_update_module(sr_conn_ctx_t *conn, const char *schema_path, const char *search_dir)
{
    sr_error_info_t *err_info = NULL;
    struct ly_ctx *tmp_ly_ctx = NULL;
    const struct lys_module *ly_mod, *upd_ly_mod;
    LYS_INFORMAT format;
    char *mod_name = NULL;

    SR_CHECK_ARG_APIRET(!conn || !schema_path, NULL, err_info);

    /* learn about the module */
    if ((err_info = sr_get_module_name_format(schema_path, &mod_name, &format))) {
        return sr_api_ret(NULL, err_info);
    }

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 0))) {
        free(mod_name);
        return sr_api_ret(NULL, err_info);
    }

    /* try to find this module */
    ly_mod = ly_ctx_get_module(conn->ly_ctx, mod_name, NULL, 1);
    if (!ly_mod || !ly_mod->implemented) {
        sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", mod_name);
        goto cleanup_unlock;
    }

    /* check write permission */
    if ((err_info = sr_perm_check(mod_name, 1))) {
        goto cleanup_unlock;
    }

    /* create new temporary context */
    if ((err_info = sr_ly_ctx_new(&tmp_ly_ctx))) {
        goto cleanup_unlock;
    }

    /* try to parse the update module */
    if (!(upd_ly_mod = sr_parse_module(tmp_ly_ctx, schema_path, format, search_dir))) {
        sr_errinfo_new_ly(&err_info, tmp_ly_ctx);
        goto cleanup_unlock;
    }

    /* it must have a revision */
    if (!upd_ly_mod->rev_size) {
        sr_errinfo_new(&err_info, SR_ERR_INVAL_ARG, NULL, "Update module \"%s\" does not have a revision.", mod_name);
        goto cleanup_unlock;
    }

    /* it must be a different module from the installed one */
    if (ly_mod->rev_size && !strcmp(upd_ly_mod->rev[0].date, ly_mod->rev[0].date)) {
        sr_errinfo_new(&err_info, SR_ERR_EXISTS, NULL, "Module \"%s@%s\" already installed.", mod_name, ly_mod->rev[0].date);
        goto cleanup_unlock;
    }

    /* schedule module update */
    if ((err_info = sr_shmmain_deferred_upd_module(conn, upd_ly_mod))) {
        goto cleanup_unlock;
    }

    /* store update module and any imports */
    if ((err_info = sr_create_module_update_imps_r(upd_ly_mod))) {
        goto cleanup_unlock;
    }

    /* success */

cleanup_unlock:
    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 0);

    ly_ctx_destroy(tmp_ly_ctx, NULL);
    free(mod_name);
    return sr_api_ret(NULL, err_info);
}

API int
sr_cancel_update_module(sr_conn_ctx_t *conn, const char *module_name)
{
    sr_error_info_t *err_info = NULL;
    const struct lys_module *ly_mod;
    char *path = NULL;

    SR_CHECK_ARG_APIRET(!conn || !module_name, NULL, err_info);

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 0))) {
        return sr_api_ret(NULL, err_info);
    }

    /* try to find this module */
    ly_mod = ly_ctx_get_module(conn->ly_ctx, module_name, NULL, 1);
    if (!ly_mod || !ly_mod->implemented) {
        sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
        goto cleanup_unlock;
    }

    /* check write permission */
    if ((err_info = sr_perm_check(module_name, 1))) {
        goto cleanup_unlock;
    }

    /* unschedule module update */
    if ((err_info = sr_shmmain_unsched_upd_module(conn, module_name))) {
        goto cleanup_unlock;
    }

    /* success */

cleanup_unlock:
    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 0);

    free(path);
    return sr_api_ret(NULL, err_info);
}

API int
sr_set_module_replay_support(sr_conn_ctx_t *conn, const char *module_name, int replay_support)
{
    sr_error_info_t *err_info = NULL;
    const struct lys_module *ly_mod;

    SR_CHECK_ARG_APIRET(!conn || !module_name, NULL, err_info);

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 0))) {
        return sr_api_ret(NULL, err_info);
    }

    /* try to find this module */
    ly_mod = ly_ctx_get_module(conn->ly_ctx, module_name, NULL, 1);
    if (!ly_mod || !ly_mod->implemented) {
        sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
        goto cleanup_unlock;
    }

    /* update replay-support flag both in LY data tree and in main SHM */
    if ((err_info = sr_shmmain_update_replay_support(conn, module_name, replay_support))) {
        goto cleanup_unlock;
    }

    /* success */

cleanup_unlock:
    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 0);
    return sr_api_ret(NULL, err_info);
}

API int
sr_set_module_access(sr_conn_ctx_t *conn, const char *module_name, const char *owner, const char *group, mode_t perm)
{
    sr_error_info_t *err_info = NULL;
    sr_mod_t *shm_mod;
    time_t from_ts, to_ts;
    char *path;

    SR_CHECK_ARG_APIRET(!conn || !module_name || (!owner && !group && ((int)perm == -1)), NULL, err_info);

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 0))) {
        return sr_api_ret(NULL, err_info);
    }

    /* try to find this module in main SHM */
    shm_mod = sr_shmmain_find_module(&conn->main_shm, conn->ext_shm.addr, module_name, 0);
    if (!shm_mod) {
        sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
        goto cleanup_unlock;
    }

    /* get running SHM file path */
    if ((err_info = sr_path_ds_shm(module_name, SR_DS_RUNNING, 1, &path))) {
        goto cleanup_unlock;
    }

    /* update running file permissions and owner */
    err_info = sr_chmodown(path, owner, group, perm);
    free(path);
    if (err_info) {
        goto cleanup_unlock;
    }

    /* get startup file path */
    if ((err_info = sr_path_startup_file(module_name, &path))) {
        goto cleanup_unlock;
    }

    /* update startup file permissions and owner */
    err_info = sr_chmodown(path, owner, group, perm);
    free(path);
    if (err_info) {
        goto cleanup_unlock;
    }

    if (shm_mod->flags & SR_MOD_REPLAY_SUPPORT) {
        if ((err_info = sr_replay_find_file(module_name, 1, 1, &from_ts, &to_ts))) {
            goto cleanup_unlock;
        }
        while (from_ts && to_ts) {
            /* get next notification file path */
            if ((err_info = sr_path_notif_file(module_name, from_ts, to_ts, &path))) {
                goto cleanup_unlock;
            }

            /* update notification file permissions and owner */
            err_info = sr_chmodown(path, owner, group, perm);
            free(path);
            if (err_info) {
                goto cleanup_unlock;
            }
        }
    }

    /* success */

cleanup_unlock:
    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 0);
    return sr_api_ret(NULL, err_info);
}

API int
sr_get_module_access(sr_conn_ctx_t *conn, const char *module_name, char **owner, char **group, mode_t *perm)
{
    sr_error_info_t *err_info = NULL;
    const struct lys_module *ly_mod;

    SR_CHECK_ARG_APIRET(!conn || !module_name || (!owner && !group && !perm), NULL, err_info);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 0, 0))) {
        return sr_api_ret(NULL, err_info);
    }

    /* try to find this module */
    ly_mod = ly_ctx_get_module(conn->ly_ctx, module_name, NULL, 1);
    if (!ly_mod || !ly_mod->implemented) {
        sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
        goto cleanup_unlock;
    }

    /* learn owner and permissions */
    if ((err_info = sr_perm_get(module_name, owner, group, perm))) {
        goto cleanup_unlock;
    }

    /* success */

cleanup_unlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(conn, 0, 0);
    return sr_api_ret(NULL, err_info);
}

/**
 * @brief En/disable module feature.
 *
 * @param[in] conn Connection to use.
 * @param[in] module_name Module to change.
 * @param[in] feature_name Feature to change.
 * @param[in] enable Whether to enable or disable the feature.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
sr_change_module_feature(sr_conn_ctx_t *conn, const char *module_name, const char *feature_name, int enable)
{
    sr_error_info_t *err_info = NULL;
    const struct lys_module *ly_mod;
    int ret;

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 0))) {
        return err_info;
    }

    /* try to find this module */
    ly_mod = ly_ctx_get_module(conn->ly_ctx, module_name, NULL, 1);
    if (!ly_mod || !ly_mod->implemented) {
        sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
        goto cleanup;
    }

    /* check write perm */
    if ((err_info = sr_perm_check(module_name, 1))) {
        goto cleanup;
    }

    /* check feature in the current context */
    ret = lys_features_state(ly_mod, feature_name);
    if (ret == -1) {
        sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Feature \"%s\" was not found in module \"%s\".",
                feature_name, module_name);
        goto cleanup;
    }

    /* mark the change (if any) in LY data tree */
    if ((err_info = sr_shmmain_deferred_change_feature(conn, ly_mod->name, feature_name, enable, ret))) {
        goto cleanup;
    }

    /* success */

cleanup:
    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 0);
    return err_info;
}

API int
sr_enable_module_feature(sr_conn_ctx_t *conn, const char *module_name, const char *feature_name)
{
    sr_error_info_t *err_info;

    SR_CHECK_ARG_APIRET(!conn || !module_name || !feature_name, NULL, err_info);

    err_info = sr_change_module_feature(conn, module_name, feature_name, 1);

    return sr_api_ret(NULL, err_info);
}

API int
sr_disable_module_feature(sr_conn_ctx_t *conn, const char *module_name, const char *feature_name)
{
    sr_error_info_t *err_info;

    SR_CHECK_ARG_APIRET(!conn || !module_name || !feature_name, NULL, err_info);

    err_info = sr_change_module_feature(conn, module_name, feature_name, 0);

    return sr_api_ret(NULL, err_info);
}

API int
sr_get_module_info(sr_conn_ctx_t *conn, struct lyd_node **sysrepo_data)
{
    sr_error_info_t *err_info;

    SR_CHECK_ARG_APIRET(!conn || !sysrepo_data, NULL, err_info);

    err_info = sr_shmmain_ly_int_data_parse(conn, sysrepo_data);

    return sr_api_ret(NULL, err_info);
}

API int
sr_get_item(sr_session_ctx_t *session, const char *path, sr_val_t **value)
{
    sr_error_info_t *err_info = NULL, *cb_err_info = NULL;
    struct ly_set *set = NULL;
    struct sr_mod_info_s mod_info;

    SR_CHECK_ARG_APIRET(!session || !path || !value, session, err_info);

    *value = NULL;
    memset(&mod_info, 0, sizeof mod_info);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    /* collect all required modules */
    if ((err_info = sr_shmmod_collect_xpath(session->conn, path, session->ds, &mod_info))) {
        goto cleanup_shm_unlock;
    }

    /* check read perm */
    if ((err_info = sr_modinfo_perm_check(&mod_info, 0))) {
        goto cleanup_shm_unlock;
    }

    /* MODULES READ LOCK */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 0, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* load modules data */
    if ((err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_REQ, 1, &session->sid, &cb_err_info)) || cb_err_info) {
        goto cleanup_mods_unlock;
    }

    /* filter the required data */
    if ((err_info = sr_modinfo_get_filter(&mod_info, path, session, &set))) {
        goto cleanup_mods_unlock;
    }

    if (set->number > 1) {
        sr_errinfo_new(&err_info, SR_ERR_INVAL_ARG, NULL, "More subtrees match \"%s\".", path);
        goto cleanup_mods_unlock;
    }

    *value = malloc(sizeof **value);
    SR_CHECK_MEM_GOTO(!*value, err_info, cleanup_mods_unlock);

    if ((err_info = sr_val_ly2sr(set->set.d[0], *value))) {
        goto cleanup_mods_unlock;
    }

    /* success */

cleanup_mods_unlock:
    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 0);

cleanup_shm_unlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    ly_set_free(set);
    sr_modinfo_free(&mod_info);
    if (cb_err_info) {
        /* return callback error if some was generated */
        sr_errinfo_merge(&err_info, cb_err_info);
        err_info->err_code = SR_ERR_CALLBACK_FAILED;
    }
    return sr_api_ret(session, err_info);
}

API int
sr_get_items(sr_session_ctx_t *session, const char *xpath, sr_val_t **values, size_t *value_cnt)
{
    sr_error_info_t *err_info = NULL, *cb_err_info = NULL;
    struct ly_set *set = NULL;
    struct sr_mod_info_s mod_info;
    uint32_t i;

    SR_CHECK_ARG_APIRET(!session || !xpath || !values || !value_cnt, session, err_info);

    *values = NULL;
    *value_cnt = 0;
    memset(&mod_info, 0, sizeof mod_info);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    /* collect all required modules */
    if ((err_info = sr_shmmod_collect_xpath(session->conn, xpath, session->ds, &mod_info))) {
        goto cleanup_shm_unlock;
    }

    /* check read perm */
    if ((err_info = sr_modinfo_perm_check(&mod_info, 0))) {
        goto cleanup_shm_unlock;
    }

    /* MODULES READ LOCK */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 0, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* load modules data */
    if ((err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_REQ, 1, &session->sid, &cb_err_info)) || cb_err_info) {
        goto cleanup_mods_unlock;
    }

    /* filter the required data */
    if ((err_info = sr_modinfo_get_filter(&mod_info, xpath, session, &set))) {
        goto cleanup_mods_unlock;
    }

    if (set->number) {
        *values = calloc(set->number, sizeof **values);
        SR_CHECK_MEM_GOTO(!*values, err_info, cleanup_mods_unlock);
    }

    for (i = 0; i < set->number; ++i) {
        if ((err_info = sr_val_ly2sr(set->set.d[i], (*values) + i))) {
            goto cleanup_mods_unlock;
        }
        ++(*value_cnt);
    }

    /* success */

cleanup_mods_unlock:
    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 0);

cleanup_shm_unlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    ly_set_free(set);
    sr_modinfo_free(&mod_info);
    if (cb_err_info) {
        /* return callback error if some was generated */
        sr_errinfo_merge(&err_info, cb_err_info);
        err_info->err_code = SR_ERR_CALLBACK_FAILED;
    }
    if (err_info) {
        sr_free_values(*values, *value_cnt);
        *values = NULL;
        *value_cnt = 0;
    }
    return sr_api_ret(session, err_info);
}

API int
sr_get_subtree(sr_session_ctx_t *session, const char *path, struct lyd_node **subtree)
{
    sr_error_info_t *err_info = NULL, *cb_err_info = NULL;
    struct sr_mod_info_s mod_info;
    struct ly_set *set = NULL;

    SR_CHECK_ARG_APIRET(!session || !path || !subtree, session, err_info);

    memset(&mod_info, 0, sizeof mod_info);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    /* collect all required modules */
    if ((err_info = sr_shmmod_collect_xpath(session->conn, path, session->ds, &mod_info))) {
        goto cleanup_shm_unlock;
    }

    /* check read perm */
    if ((err_info = sr_modinfo_perm_check(&mod_info, 0))) {
        goto cleanup_shm_unlock;
    }

    /* MODULES READ LOCK */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 0, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* load modules data */
    if ((err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_REQ, 1, &session->sid, &cb_err_info)) || cb_err_info) {
        goto cleanup_mods_unlock;
    }

    /* filter the required data */
    if ((err_info = sr_modinfo_get_filter(&mod_info, path, session, &set))) {
        goto cleanup_mods_unlock;
    }

    if (set->number > 1) {
        sr_errinfo_new(&err_info, SR_ERR_INVAL_ARG, NULL, "More subtrees match \"%s\".", path);
        goto cleanup_mods_unlock;
    }

    if (set->number == 1) {
        *subtree = lyd_dup(set->set.d[0], LYD_DUP_OPT_RECURSIVE);
        if (!*subtree) {
            sr_errinfo_new_ly(&err_info, session->conn->ly_ctx);
            goto cleanup_mods_unlock;
        }
    } else {
        *subtree = NULL;
    }

    /* success */

cleanup_mods_unlock:
    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 0);

cleanup_shm_unlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    ly_set_free(set);
    sr_modinfo_free(&mod_info);
    if (cb_err_info) {
        /* return callback error if some was generated */
        sr_errinfo_merge(&err_info, cb_err_info);
        err_info->err_code = SR_ERR_CALLBACK_FAILED;
    }
    return sr_api_ret(session, err_info);
}

API int
sr_get_data(sr_session_ctx_t *session, const char *xpath, struct lyd_node **data)
{
    sr_error_info_t *err_info = NULL, *cb_err_info = NULL;
    uint32_t i;
    struct sr_mod_info_s mod_info;
    struct ly_set *subtrees = NULL;
    struct lyd_node *node;

    SR_CHECK_ARG_APIRET(!session || !xpath || !data, session, err_info);

    *data = NULL;
    memset(&mod_info, 0, sizeof mod_info);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    /* collect all required modules */
    if ((err_info = sr_shmmod_collect_xpath(session->conn, xpath, session->ds, &mod_info))) {
        goto cleanup_shm_unlock;
    }

    /* check read perm */
    if ((err_info = sr_modinfo_perm_check(&mod_info, 0))) {
        goto cleanup_shm_unlock;
    }

    /* MODULES READ LOCK */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 0, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* load modules data */
    if ((err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_REQ, 1, &session->sid, &cb_err_info)) || cb_err_info) {
        goto cleanup_mods_unlock;
    }

    /* filter the required data */
    if ((err_info = sr_modinfo_get_filter(&mod_info, xpath, session, &subtrees))) {
        goto cleanup_mods_unlock;
    }

    /* duplicate all returned subtrees with their parents and merge into one data tree */
    for (i = 0; i < subtrees->number; ++i) {
        node = lyd_dup(subtrees->set.d[i], LYD_DUP_OPT_RECURSIVE | LYD_DUP_OPT_WITH_PARENTS | LYD_DUP_OPT_WITH_WHEN);
        if (!node) {
            sr_errinfo_new_ly(&err_info, session->conn->ly_ctx);
            lyd_free_withsiblings(*data);
            *data = NULL;
            goto cleanup_mods_unlock;
        }

        if (!*data) {
            /* find parent */
            while (node->parent) {
                node = node->parent;
            }
            *data = node;
        } else {
            /* merge */
            if (lyd_merge(*data, node, LYD_OPT_DESTRUCT | LYD_OPT_EXPLICIT)) {
                sr_errinfo_new_ly(&err_info, session->conn->ly_ctx);
                lyd_free_withsiblings(node);
                lyd_free_withsiblings(*data);
                *data = NULL;
                goto cleanup_mods_unlock;
            }
        }
    }

    /* success */

cleanup_mods_unlock:
    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 0);

cleanup_shm_unlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    ly_set_free(subtrees);
    sr_modinfo_free(&mod_info);
    if (cb_err_info) {
        /* return callback error if some was generated */
        sr_errinfo_merge(&err_info, cb_err_info);
        err_info->err_code = SR_ERR_CALLBACK_FAILED;
    }
    return sr_api_ret(session, err_info);
}

API void
sr_free_val(sr_val_t *value)
{
    if (!value) {
        return;
    }

    free(value->xpath);
    switch (value->type) {
    case SR_BINARY_T:
    case SR_BITS_T:
    case SR_ENUM_T:
    case SR_IDENTITYREF_T:
    case SR_INSTANCEID_T:
    case SR_STRING_T:
    case SR_ANYXML_T:
    case SR_ANYDATA_T:
        free(value->data.string_val);
        break;
    default:
        /* nothing to free */
        break;
    }

    free(value);
}

API void
sr_free_values(sr_val_t *values, size_t count)
{
    size_t i;

    if (!values || !count) {
        return;
    }

    for (i = 0; i < count; ++i) {
        free(values[i].xpath);
        switch (values[i].type) {
        case SR_BINARY_T:
        case SR_BITS_T:
        case SR_ENUM_T:
        case SR_IDENTITYREF_T:
        case SR_INSTANCEID_T:
        case SR_STRING_T:
        case SR_ANYXML_T:
        case SR_ANYDATA_T:
            free(values[i].data.string_val);
            break;
        default:
            /* nothing to free */
            break;
        }
    }

    free(values);
}

API int
sr_set_item(sr_session_ctx_t *session, const char *path, const sr_val_t *value, const sr_edit_options_t opts)
{
    sr_error_info_t *err_info = NULL;
    char str[22], *str_val;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(session->ds) || !value || (!path && !value->xpath), session, err_info);

    str_val = sr_val_sr2ly_str(session->conn->ly_ctx, value, str);
    if (!path) {
        path = value->xpath;
    }

    /* API function */
    return sr_set_item_str(session, path, str_val, opts);
}

/**
 * @brief Add an edit (add/delete/move) operation into session.
 *
 * @param[in] session Session to use.
 * @param[in] xpath XPath of the change.
 * @param[in] value Value of the change.
 * @param[in] operation Operation.
 * @param[in] def_operation Default operation.
 * @param[in] position Optional position for move.
 * @param[in] keys Optional list keys predicate of relative item for move.
 * @param[in] val Optional value of relative item for move.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
sr_edit_item(sr_session_ctx_t *session, const char *path, const char *value, const char *operation,
        const char *def_operation, const sr_move_position_t *position, const char *keys, const char *val)
{
    sr_error_info_t *err_info = NULL;

    assert(session && path && operation);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0)) != SR_ERR_OK) {
        return err_info;
    }

    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    /* add the operation into edit */
    if ((err_info = sr_edit_add(session, path, value, operation, def_operation, position, keys, val))) {
        return err_info;
    }

    return NULL;
}

API int
sr_set_item_str(sr_session_ctx_t *session, const char *path, const char *value, const sr_edit_options_t opts)
{
    sr_error_info_t *err_info = NULL;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(session->ds) || !path, session, err_info);

    err_info = sr_edit_item(session, path, value, opts & SR_EDIT_STRICT ? "create" : "merge",
            opts & SR_EDIT_NON_RECURSIVE ? "none" : "merge", NULL, NULL, NULL);

    return sr_api_ret(session, err_info);
}

API int
sr_delete_item(sr_session_ctx_t *session, const char *path, const sr_edit_options_t opts)
{
    sr_error_info_t *err_info = NULL;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(session->ds) || !path, session, err_info);

    err_info = sr_edit_item(session, path, NULL, opts & SR_EDIT_STRICT ? "delete" : "remove",
            opts & SR_EDIT_STRICT ? "none" : "ether", NULL, NULL, NULL);

    return sr_api_ret(session, err_info);
}

API int
sr_move_item(sr_session_ctx_t *session, const char *path, const sr_move_position_t position, const char *list_keys,
        const char *leaflist_value)
{
    sr_error_info_t *err_info = NULL;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(session->ds) || !path, session, err_info);

    err_info = sr_edit_item(session, path, NULL, "merge", "none", &position, list_keys, leaflist_value);

    return sr_api_ret(session, err_info);
}

API int
sr_edit_batch(sr_session_ctx_t *session, const struct lyd_node *edit, const char *default_operation)
{
    sr_error_info_t *err_info = NULL;
    const char *attr_full_name;
    struct lyd_node *valid_edit = NULL, *node;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(session->ds) || !edit || !default_operation, session, err_info);
    SR_CHECK_ARG_APIRET(strcmp(default_operation, "merge") && strcmp(default_operation, "replace")
            && strcmp(default_operation, "none"), session, err_info);

    if (session->dt[session->ds].edit) {
        /* do not allow merging NETCONF edits into sysrepo ones, it can cause some unexpected results */
        sr_errinfo_new(&err_info, SR_ERR_UNSUPPORTED, NULL, "There are already some session changes.");
        return sr_api_ret(session, err_info);
    }

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    valid_edit = lyd_dup_withsiblings(edit, LYD_DUP_OPT_RECURSIVE);
    if (!valid_edit) {
        sr_errinfo_new_ly(&err_info, session->conn->ly_ctx);
        goto error;
    }

    /* validate the input data tree first */
    if (lyd_validate(&valid_edit, LYD_OPT_EDIT, NULL)) {
        SR_ERRINFO_VALID(&err_info);
        goto error;
    }

    /* add default operation */
    if (!strcmp(default_operation, "none")) {
        attr_full_name = SR_YANG_MOD ":operation";
    } else {
        attr_full_name = "ietf-netconf:operation";
    }
    LY_TREE_FOR(valid_edit, node) {
        if (!lyd_insert_attr(node, NULL, attr_full_name, default_operation)) {
            sr_errinfo_new_ly(&err_info, session->conn->ly_ctx);
            goto error;
        }
    }

    session->dt[session->ds].edit = valid_edit;
    return sr_api_ret(session, NULL);

error:
    lyd_free_withsiblings(valid_edit);
    return sr_api_ret(session, err_info);
}

API int
sr_validate(sr_session_ctx_t *session)
{
    sr_error_info_t *err_info = NULL, *cb_err_info = NULL;
    struct sr_mod_info_s mod_info;

    SR_CHECK_ARG_APIRET(!session, session, err_info);

    memset(&mod_info, 0, sizeof mod_info);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    /* collect all required modules */
    if (!IS_WRITABLE_DS(session->ds)) {
        /* all modules */
        err_info = sr_shmmod_collect_modules(session->conn, NULL, session->ds, 0, &mod_info);
    } else {
        /* only the ones modified (other modules must be valid) */
        err_info = sr_shmmod_collect_edit(session->conn, session->dt[session->ds].edit, session->ds, &mod_info);
    }
    if (err_info) {
        goto cleanup_shm_unlock;
    }

    /* MODULES READ LOCK */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 0, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* load all modules data (we need dependencies for validation) */
    if ((err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_TYPE_MASK, 0, NULL, NULL))) {
        goto cleanup_mods_unlock;
    }

    /* apply any changes */
    if (IS_WRITABLE_DS(session->ds)) {
        if ((err_info = sr_modinfo_edit_apply(&mod_info, session->dt[session->ds].edit, 0))) {
            goto cleanup_mods_unlock;
        }
    }

    /* validate the data trees */
    if ((err_info = sr_modinfo_validate(&mod_info, 0, &session->sid, &cb_err_info)) || cb_err_info) {
        goto cleanup_mods_unlock;
    }

    /* success */

cleanup_mods_unlock:
    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 0);

cleanup_shm_unlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    sr_modinfo_free(&mod_info);
    if (cb_err_info) {
        /* return callback error if some was generated */
        sr_errinfo_merge(&err_info, cb_err_info);
        err_info->err_code = SR_ERR_CALLBACK_FAILED;
    }
    return sr_api_ret(session, err_info);
}

API int
sr_apply_changes(sr_session_ctx_t *session)
{
    sr_error_info_t *err_info = NULL, *cb_err_info = NULL;
    struct lyd_node *update_edit;
    struct sr_mod_info_s mod_info;
    const char *err_msg = NULL, *err_xpath = NULL;
    int ret;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(session->ds), session, err_info);

    if (!session->dt[session->ds].edit) {
        return sr_api_ret(session, NULL);
    }

    memset(&mod_info, 0, sizeof mod_info);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    /* collect all required modules */
    if ((err_info = sr_shmmod_collect_edit(session->conn, session->dt[session->ds].edit, session->ds, &mod_info))) {
        goto cleanup_shm_unlock;
    }

    /* MODULES READ LOCK (but setting flag for guaranteed later upgrade success) */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 1, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* load all modules data (we need dependencies for validation) */
    if ((err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_TYPE_MASK, 0, NULL, NULL))) {
        goto cleanup_mods_unlock;
    }

    /* create diff */
    if ((err_info = sr_modinfo_edit_apply(&mod_info, session->dt[session->ds].edit, 1))) {
        goto cleanup_mods_unlock;
    }

    /* call connection diff callback */
    if (mod_info.diff && session->conn->diff_check_cb && (ret = session->conn->diff_check_cb(session, mod_info.diff))) {
        /* create cb_err_info */
        if (session->err_info && session->err_info->err_code == SR_ERR_OK) {
            err_msg = session->err_info->err[0].message;
        }
        if (!err_msg) {
            err_msg = sr_strerror(ret);
        }
        err_xpath = session->err_info->err[0].xpath;
        sr_errinfo_new(&cb_err_info, ret, err_xpath, err_msg);
        goto cleanup_mods_unlock;
    }

    /* validate new data trees */
    if ((session->ds != SR_DS_CANDIDATE) && (err_info = sr_modinfo_validate(&mod_info, 1, NULL, NULL))) {
        goto cleanup_mods_unlock;
    }

    /* check write perm (we must wait until after validation, some additional modules can be modified) */
    if ((err_info = sr_modinfo_perm_check(&mod_info, 1))) {
        goto cleanup_shm_unlock;
    }

    if (!mod_info.diff) {
        SR_LOG_INFMSG("No datastore changes to apply.");
        if (!mod_info.dflt_change) {
            goto cleanup_mods_unlock;
        }
        /* while there are no changes for callbacks, some default flags changed so we must store them */
    }

    if (mod_info.diff) {
        /* publish current diff in an "update" event for the subscribers to update it */
        if ((err_info = sr_shmsub_conf_notify_update(&mod_info, session->sid, &update_edit, &cb_err_info))) {
            goto cleanup_mods_unlock;
        }
        if (cb_err_info) {
            /* "update" event failed, just clear the sub SHM and finish */
            err_info = sr_shmsub_conf_notify_clear(&mod_info, SR_SUB_EV_UPDATE);
            goto cleanup_mods_unlock;
        }

        /* create new diff if we have an update edit */
        if (update_edit) {
            /* merge edits */
            if (lyd_merge(session->dt[session->ds].edit, update_edit, LYD_OPT_DESTRUCT)) {
                sr_errinfo_new_ly(&err_info, session->conn->ly_ctx);
                sr_errinfo_new(&err_info, SR_ERR_INTERNAL, NULL, "Merging edits failed.");
                goto cleanup_mods_unlock;
            }

            /* free current changed data */
            assert(!mod_info.data_cached);
            lyd_free_withsiblings(mod_info.data);
            mod_info.data = NULL;

            /* reload unchanged data back */
            if ((err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_REQ, 0, NULL, NULL))) {
                goto cleanup_mods_unlock;
            }

            /* get updated diff */
            lyd_free_withsiblings(mod_info.diff);
            mod_info.diff = NULL;
            mod_info.dflt_change = 0;
            if ((err_info = sr_modinfo_edit_apply(&mod_info, session->dt[session->ds].edit, 1))) {
                goto cleanup_mods_unlock;
            }

            /* validate updated data trees */
            if ((session->ds != SR_DS_CANDIDATE) && (err_info = sr_modinfo_validate(&mod_info, 1, NULL, NULL))) {
                goto cleanup_mods_unlock;
            }

            if (!mod_info.diff) {
                SR_LOG_INFMSG("No datastore changes to apply.");
                if (!mod_info.dflt_change) {
                    goto cleanup_mods_unlock;
                }
                /* while there are no changes for callbacks, some default flags changed so we must store them */
            }
        }

        if (mod_info.diff) {
            /* publish final diff in a "change" event for any subscribers and wait for them */
            if ((err_info = sr_shmsub_conf_notify_change(&mod_info, session->sid, &cb_err_info))) {
                goto cleanup_mods_unlock;
            }
            if (cb_err_info) {
                /* "change" event failed, publish "abort" event and finish */
                err_info = sr_shmsub_conf_notify_change_abort(&mod_info, session->sid);
                goto cleanup_mods_unlock;
            }
        }
    }

    /* MODULES WRITE LOCK (upgrade) */
    if ((err_info = sr_shmmod_modinfo_rdlock_upgrade(&mod_info, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* store updated datastore */
    if ((err_info = sr_modinfo_data_store(&mod_info))) {
        goto cleanup_mods_unlock;
    }

    if (mod_info.diff) {
        /* publish "done" event, all changes were applied */
        if ((err_info = sr_shmsub_conf_notify_change_done(&mod_info, session->sid))) {
            goto cleanup_mods_unlock;
        }

        /* generate netconf-config-change notification */
        if ((err_info = sr_modinfo_generate_config_change_notif(&mod_info, session))) {
            goto cleanup_mods_unlock;
        }
    }

    /* success */

cleanup_mods_unlock:
    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 1);

cleanup_shm_unlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    if (!err_info) {
        /* free applied edit */
        lyd_free_withsiblings(session->dt[session->ds].edit);
        session->dt[session->ds].edit = NULL;
    }

    sr_modinfo_free(&mod_info);
    if (cb_err_info) {
        /* return callback error if some was generated */
        sr_errinfo_merge(&err_info, cb_err_info);
        err_info->err_code = SR_ERR_CALLBACK_FAILED;
    }
    return sr_api_ret(session, err_info);
}

API int
sr_discard_changes(sr_session_ctx_t *session)
{
    sr_error_info_t *err_info = NULL;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(session->ds), session, err_info);

    if (!session->dt[session->ds].edit) {
        return sr_api_ret(session, NULL);
    }

    lyd_free_withsiblings(session->dt[session->ds].edit);
    session->dt[session->ds].edit = NULL;
    return sr_api_ret(session, NULL);
}

/**
 * @brief Replace configuration of all or some modules.
 *
 * @param[in] session Session to use.
 * @param[in] ly_mod Optional specific module.
 * @param[in] src_data Source data for the replace, they are spent.
 * @param[in] trg_datastore Destination datastore.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
_sr_replace_config(sr_session_ctx_t *session, const struct lys_module *ly_mod, struct lyd_node **src_data,
        sr_datastore_t trg_datastore)
{
    sr_error_info_t *err_info = NULL, *cb_err_info = NULL;
    struct sr_mod_info_s mod_info;
    const char *err_msg = NULL, *err_xpath = NULL;
    int ret;

    assert(!*src_data || !(*src_data)->prev->next);
    memset(&mod_info, 0, sizeof mod_info);

    /* collect all required modules */
    if ((err_info = sr_shmmod_collect_modules(session->conn, ly_mod, trg_datastore, MOD_INFO_DEP | MOD_INFO_INV_DEP, &mod_info))) {
        return err_info;
    }

    /* check write perm */
    if ((err_info = sr_modinfo_perm_check(&mod_info, 1))) {
        return err_info;
    }

    /* MODULES READ LOCK (but setting flag for guaranteed later upgrade success) */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 1, session->sid))) {
        return err_info;
    }

    /* load all current modules data */
    if ((err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_TYPE_MASK, 0, NULL, NULL))) {
        goto cleanup_mods_unlock;
    }

    /* update affected data and create corresponding diff */
    if ((err_info = sr_modinfo_replace(&mod_info, src_data))) {
        goto cleanup_mods_unlock;
    }

    /* call connection diff callback */
    if (mod_info.diff && session->conn->diff_check_cb && (ret = session->conn->diff_check_cb(session, mod_info.diff))) {
        /* create cb_err_info */
        if (session->err_info && session->err_info->err_code == SR_ERR_OK) {
            err_msg = session->err_info->err[0].message;
        }
        if (!err_msg) {
            err_msg = sr_strerror(ret);
        }
        err_xpath = session->err_info->err[0].xpath;
        sr_errinfo_new(&cb_err_info, ret, err_xpath, err_msg);
        goto cleanup_mods_unlock;
    }

    if (!mod_info.diff) {
        SR_LOG_INFMSG("No datastore changes to apply.");
        if (!mod_info.dflt_change) {
            goto cleanup_mods_unlock;
        }
        /* while there are no changes for callbacks, some default flags changed so we must store them */
    } else if (trg_datastore != SR_DS_CANDIDATE) {
        /* validate the new config */
        if ((err_info = sr_modinfo_validate(&mod_info, 1, NULL, NULL))) {
            goto cleanup_mods_unlock;
        }
    }

    if (mod_info.diff) {
        /* publish final diff in a "change" event for any subscribers and wait for them */
        if ((err_info = sr_shmsub_conf_notify_change(&mod_info, session->sid, &cb_err_info))) {
            goto cleanup_mods_unlock;
        }
        if (cb_err_info) {
            /* "change" event failed, publish "abort" event and finish */
            err_info = sr_shmsub_conf_notify_change_abort(&mod_info, session->sid);
            goto cleanup_mods_unlock;
        }
    }

    /* MODULES WRITE LOCK (upgrade) */
    if ((err_info = sr_shmmod_modinfo_rdlock_upgrade(&mod_info, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* store updated datastore */
    if ((err_info = sr_modinfo_data_store(&mod_info))) {
        goto cleanup_mods_unlock;
    }

    if (mod_info.diff) {
        /* publish "done" event, all changes were applied */
        if ((err_info = sr_shmsub_conf_notify_change_done(&mod_info, session->sid))) {
            goto cleanup_mods_unlock;
        }

        /* generate netconf-config-change notification */
        if ((err_info = sr_modinfo_generate_config_change_notif(&mod_info, session))) {
            goto cleanup_mods_unlock;
        }
    }

    /* success */

cleanup_mods_unlock:
    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 1);

    sr_modinfo_free(&mod_info);
    if (cb_err_info) {
        /* return callback error if some was generated */
        sr_errinfo_merge(&err_info, cb_err_info);
        err_info->err_code = SR_ERR_CALLBACK_FAILED;
    }
    return err_info;
}

API int
sr_replace_config(sr_session_ctx_t *session, const char *module_name, struct lyd_node *src_config,
        sr_datastore_t trg_datastore)
{
    sr_error_info_t *err_info = NULL;
    const struct lys_module *ly_mod = NULL;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(trg_datastore), session, err_info);

    /* find first sibling */
    for (; src_config && src_config->prev->next; src_config = src_config->prev);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    if (module_name) {
        /* try to find this module */
        ly_mod = ly_ctx_get_module(session->conn->ly_ctx, module_name, NULL, 1);
        if (!ly_mod) {
            sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
            goto cleanup_shm_unlock;
        }
    }

    /* replace the configuration */
    if ((err_info = _sr_replace_config(session, ly_mod, &src_config, trg_datastore))) {
        goto cleanup_shm_unlock;
    }

    /* success */

cleanup_shm_unlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    lyd_free_withsiblings(src_config);
    return sr_api_ret(session, err_info);
}

API int
sr_copy_config(sr_session_ctx_t *session, const char *module_name, sr_datastore_t src_datastore, sr_datastore_t trg_datastore)
{
    sr_error_info_t *err_info = NULL;
    struct sr_mod_info_s mod_info;
    const struct lys_module *ly_mod = NULL;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(src_datastore) || !IS_WRITABLE_DS(trg_datastore), session, err_info);

    if (src_datastore == trg_datastore) {
        /* nothing to do */
        return sr_api_ret(session, NULL);
    }

    memset(&mod_info, 0, sizeof mod_info);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    if (module_name) {
        /* try to find this module */
        ly_mod = ly_ctx_get_module(session->conn->ly_ctx, module_name, NULL, 1);
        if (!ly_mod) {
            sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
            goto cleanup_shm_unlock;
        }
    }

    /* collect all required modules (dependencies are not needed for a single module, otherwise all of them are there) */
    if ((err_info = sr_shmmod_collect_modules(session->conn, ly_mod, src_datastore, 0, &mod_info))) {
        goto cleanup_shm_unlock;
    }

    /* MODULES READ LOCK */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 0, session->sid))) {
        goto cleanup_shm_unlock;
    }

    if ((src_datastore == SR_DS_RUNNING) && (trg_datastore == SR_DS_CANDIDATE)) {
        /* special case, just reset candidate */
        err_info = sr_modinfo_candidate_reset(&mod_info);

        /* MODULES UNLOCK */
        sr_shmmod_modinfo_unlock(&mod_info, 0);
        goto cleanup_shm_unlock;
    }

    /* get their data */
    err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_REQ, 0, NULL, NULL);

    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 0);

    if (err_info) {
        goto cleanup_shm_unlock;
    }

    /* replace the configuration */
    if ((err_info = _sr_replace_config(session, ly_mod, &mod_info.data, trg_datastore))) {
        goto cleanup_shm_unlock;
    }

    if ((src_datastore == SR_DS_CANDIDATE) && (trg_datastore == SR_DS_RUNNING)) {
        /* reset candidate after it was applied in running */
        if ((err_info = sr_modinfo_candidate_reset(&mod_info))) {
            goto cleanup_shm_unlock;
        }
    }

    /* success */

cleanup_shm_unlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    sr_modinfo_free(&mod_info);
    return sr_api_ret(session, err_info);
}

/**
 * @brief (Un)lock datastore locks.
 *
 * @param[in] mod_info Mod info to use.
 * @param[in] lock Whether to lock or unlock.
 * @param[in] sid Sysrepo session ID.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
sr_change_dslock(struct sr_mod_info_s *mod_info, int lock, sr_sid_t sid)
{
    sr_error_info_t *err_info = NULL;
    uint32_t i, j;
    char *path;
    int r;
    struct sr_mod_info_mod_s *mod;
    struct sr_mod_lock_s *shm_lock;

    assert(IS_WRITABLE_DS(mod_info->ds));

    for (i = 0; i < mod_info->mod_count; ++i) {
        mod = &mod_info->mods[i];
        shm_lock = &mod->shm_mod->data_lock_info[mod_info->ds];

        assert(mod->state & MOD_INFO_REQ);

        /* we assume these modules are write-locked by this session */
        assert(shm_lock->write_locked && (shm_lock->sid.sr == sid.sr));

        /* it was successfully WRITE-locked, check that DS lock state is as expected */
        if (shm_lock->ds_locked && lock) {
            assert(shm_lock->sid.sr == sid.sr);
            sr_errinfo_new(&err_info, SR_ERR_LOCKED, NULL, "Module \"%s\" is already locked by this session %u (NC SID %u).",
                    mod->ly_mod->name, sid.sr, sid.nc);
            goto error;
        } else if (!shm_lock->ds_locked && !lock) {
            assert(shm_lock->sid.sr == sid.sr);
            sr_errinfo_new(&err_info, SR_ERR_OPERATION_FAILED, NULL, "Module \"%s\" was not locked by this session %u (NC SID %u).",
                    mod->ly_mod->name, sid.sr, sid.nc);
            goto error;
        } else if (lock && (mod_info->ds == SR_DS_CANDIDATE)) {
            /* candidate DS file cannot exist */
            if ((err_info = sr_path_ds_shm(mod->ly_mod->name, SR_DS_CANDIDATE, 1, &path))) {
                goto error;
            }
            r = access(path, F_OK);
            free(path);
            if ((r == -1) && (errno != ENOENT)) {
                SR_ERRINFO_SYSERRNO(&err_info, "access");
                goto error;
            } else if (!r) {
                sr_errinfo_new(&err_info, SR_ERR_UNSUPPORTED, NULL, "Module \"%s\" candidate datastore data have "
                        "already been modified.", mod->ly_mod->name);
                goto error;
            }
        }

        /* change DS lock state and remember the time */
        shm_lock->ds_locked = lock;
        if (lock) {
            shm_lock->ds_ts = time(NULL);
        } else {
            shm_lock->ds_ts = 0;
        }
    }

    return NULL;

error:
    /* reverse any DS lock state changes */
    for (j = 0; j < i; ++j) {
        shm_lock = &mod_info->mods[j].shm_mod->data_lock_info[mod_info->ds];

        assert((shm_lock->ds_locked && lock) || (!shm_lock->ds_locked && !lock));

        if (lock) {
            shm_lock->ds_locked = 0;
        } else {
            shm_lock->ds_locked = 1;
        }
    }
    return err_info;
}

/**
 * @brief (Un)lock a specific or all modules datastore locks.
 *
 * @param[in] session Session to use.
 * @param[in] module_name Optional specific module.
 * @param[in] lock Whether to lock or unlock.
 * @return err_code (SR_ERR_OK on success).
 */
static int
_sr_un_lock(sr_session_ctx_t *session, const char *module_name, int lock)
{
    sr_error_info_t *err_info = NULL;
    struct sr_mod_info_s mod_info;
    const struct lys_module *ly_mod = NULL;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(session->ds), session, err_info);

    memset(&mod_info, 0, sizeof mod_info);

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 1, 0))) {
        return sr_api_ret(session, err_info);
    }

    if (module_name) {
        /* try to find this module */
        ly_mod = ly_ctx_get_module(session->conn->ly_ctx, module_name, NULL, 1);
        if (!ly_mod) {
            sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
            goto cleanup_shm_unlock;
        }
    }

    /* collect all required modules */
    if ((err_info = sr_shmmod_collect_modules(session->conn, ly_mod, session->ds, 0, &mod_info))) {
        goto cleanup_shm_unlock;
    }

    /* check read perm */
    if (lock && (err_info = sr_modinfo_perm_check(&mod_info, 0))) {
        goto cleanup_shm_unlock;
    }

    /* MODULES READ LOCK (but setting flag to forbid write locking) */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 1, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* DS-(un)lock them */
    if ((err_info = sr_change_dslock(&mod_info, lock, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* success */

cleanup_mods_unlock:
    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 1);

cleanup_shm_unlock:
    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(session->conn, 1, 0);

    sr_modinfo_free(&mod_info);
    return sr_api_ret(session, err_info);
}

API int
sr_lock(sr_session_ctx_t *session, const char *module_name)
{
    return _sr_un_lock(session, module_name, 1);
}

API int
sr_unlock(sr_session_ctx_t *session, const char *module_name)
{
    return _sr_un_lock(session, module_name, 0);
}

API int
sr_get_lock(sr_conn_ctx_t *conn, sr_datastore_t datastore, const char *module_name, int *is_locked, uint32_t *id,
        uint32_t *nc_id, time_t *timestamp)
{
    sr_error_info_t *err_info = NULL;
    struct sr_mod_info_s mod_info;
    const struct lys_module *ly_mod = NULL;
    struct sr_mod_lock_s *shm_lock;
    uint32_t i;
    sr_sid_t sid;

    SR_CHECK_ARG_APIRET(!conn || !IS_WRITABLE_DS(datastore) || !is_locked, NULL, err_info);

    if (id) {
        *id = 0;
    }
    if (nc_id) {
        *nc_id = 0;
    }
    if (timestamp) {
        *timestamp = 0;
    }
    memset(&mod_info, 0, sizeof mod_info);
    memset(&sid, 0, sizeof sid);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 0, 0))) {
        return sr_api_ret(NULL, err_info);
    }

    if (module_name) {
        /* try to find this module */
        ly_mod = ly_ctx_get_module(conn->ly_ctx, module_name, NULL, 1);
        if (!ly_mod) {
            sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
            goto cleanup_unlock;
        }
    }

    /* collect all required modules */
    if ((err_info = sr_shmmod_collect_modules(conn, ly_mod, datastore, 0, &mod_info))) {
        goto cleanup_unlock;
    }

    /* check read perm */
    if ((err_info = sr_modinfo_perm_check(&mod_info, 0))) {
        goto cleanup_unlock;
    }

    /* check DS-lock of the module(s) */
    for (i = 0; i < mod_info.mod_count; ++i) {
        shm_lock = &mod_info.mods[i].shm_mod->data_lock_info[mod_info.ds];

        if (!shm_lock->ds_locked) {
            /* there is at least one module that is not DS-locked */
            break;
        }

        if (!sid.sr) {
            /* remember the first DS lock owner */
            sid = shm_lock->sid;
        } else if (sid.sr != shm_lock->sid.sr) {
            /* more DS module lock owners, not a full DS lock */
            break;
        }
    }

    if (i < mod_info.mod_count) {
        /* not full DS lock */
        *is_locked = 0;
    } else {
        /* the module or all modules is DS locked by a single SR session */
        *is_locked = 1;
        if (id) {
            *id = sid.sr;
        }
        if (nc_id) {
            *nc_id = sid.nc;
        }
        if (timestamp) {
            *timestamp = shm_lock->ds_ts;
        }
    }

    /* success */

cleanup_unlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(conn, 0, 0);

    sr_modinfo_free(&mod_info);
    return sr_api_ret(NULL, err_info);
}

API int
sr_get_event_pipe(sr_subscription_ctx_t *subscription, int *event_pipe)
{
    sr_error_info_t *err_info = NULL;

    SR_CHECK_ARG_APIRET(!subscription || !event_pipe, NULL, err_info);

    *event_pipe = subscription->evpipe;
    return SR_ERR_OK;
}

/**
 * @brief Process special notification events on a subscription involving
 * changing main SHM.
 *
 * @param[in] subscription Subscription to process.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
sr_process_events_notif_replay_stop(sr_subscription_ctx_t *subscription)
{
    sr_error_info_t *err_info = NULL;
    uint32_t i;
    int mod_finished;

    /* MAIN SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(subscription->conn, 1, 1))) {
        return err_info;
    }

    /* SUBS LOCK */
    if ((err_info = sr_mlock(&subscription->subs_lock, SR_SUB_SUBS_LOCK_TIMEOUT, __func__))) {
        sr_shmmain_unlock(subscription->conn, 1, 0);
        return err_info;
    }

    i = 0;
    while (i < subscription->notif_sub_count) {
        /* perform any replays requested */
        if ((err_info = sr_shmsub_notif_listen_module_replay(&subscription->notif_subs[i], subscription))) {
            goto cleanup_unlock;
        }

        /* check whether a subscription did not finish */
        if ((err_info = sr_shmsub_notif_listen_module_stop_time(&subscription->notif_subs[i], subscription,
                &mod_finished))) {
            goto cleanup_unlock;
        }
        if (mod_finished) {
            /* all subscriptions of this module have finished, try the next */
            continue;
        }

        /* next iteration */
        ++i;
    }

    /* success */

cleanup_unlock:
    /* SUBS UNLOCK */
    sr_munlock(&subscription->subs_lock);

    /* MAIN SHM WRITE UNLOCK */
    sr_shmmain_unlock(subscription->conn, 1, 1);
    return err_info;
}

API int
sr_process_events(sr_subscription_ctx_t *subscription, sr_session_ctx_t *session, time_t *stop_time_in)
{
    sr_error_info_t *err_info = NULL;
    int ret;
    char buf[1];
    uint32_t i;

    /* session does not have to be set */
    SR_CHECK_ARG_APIRET(!subscription, session, err_info);

    if (stop_time_in) {
        *stop_time_in = 0;
    }

process_events:
    /* SUBS LOCK */
    if ((err_info = sr_mlock(&subscription->subs_lock, SR_SUB_SUBS_LOCK_TIMEOUT, __func__))) {
        return sr_api_ret(session, err_info);
    }

    /* read all bytes from the pipe, there can be several events by now */
    do {
        ret = read(subscription->evpipe, buf, 1);
    } while (ret == 1);
    if ((ret == -1) && (errno != EAGAIN)) {
        SR_ERRINFO_SYSERRNO(&err_info, "read");
        sr_errinfo_new(&err_info, SR_ERR_INTERNAL, NULL, "Failed to read from an event pipe.");
        goto cleanup_unlock;
    }

    /* first check whether there are any pending replays or finished subscriptions ... */
    for (i = 0; i < subscription->notif_sub_count; ++i) {
        if (sr_shmsub_notif_listen_module_has_replay_or_stop(&subscription->notif_subs[i])) {
            break;
        }
    }
    if (i < subscription->notif_sub_count) {
        /* ... there are, prepare for handling them (keep lock order) */

        /* SUBS UNLOCK */
        sr_munlock(&subscription->subs_lock);

        if ((err_info = sr_process_events_notif_replay_stop(subscription))) {
            return sr_api_ret(session, err_info);
        }

        /* start from the beginning again, anything could have changed */
        goto process_events;
    }

    /* configuration subscriptions */
    for (i = 0; i < subscription->conf_sub_count; ++i) {
        if ((err_info = sr_shmsub_conf_listen_process_module_events(&subscription->conf_subs[i], subscription->conn))) {
            goto cleanup_unlock;
        }
    }

    /* operational subscriptions */
    for (i = 0; i < subscription->oper_sub_count; ++i) {
        if ((err_info = sr_shmsub_oper_listen_process_module_events(&subscription->oper_subs[i], subscription->conn))) {
            goto cleanup_unlock;
        }
    }

    /* RPC/action subscriptions */
    for (i = 0; i < subscription->rpc_sub_count; ++i) {
        if ((err_info = sr_shmsub_rpc_listen_process_rpc_events(&subscription->rpc_subs[i], subscription->conn))) {
            goto cleanup_unlock;
        }
    }

    /* notification subscriptions */
    for (i = 0; i < subscription->notif_sub_count; ++i) {
        if ((err_info = sr_shmsub_notif_listen_process_module_events(&subscription->notif_subs[i], subscription->conn))) {
            goto cleanup_unlock;
        }

        /* find nearest stop time */
        sr_shmsub_notif_listen_module_get_stop_time_in(&subscription->notif_subs[i], stop_time_in);
    }

cleanup_unlock:
    /* SUBS UNLOCK */
    sr_munlock(&subscription->subs_lock);
    return sr_api_ret(session, err_info);
}

/**
 * @brief Unsubscribe (free) a subscription (main SHM and remap lock is expected to be held).
 *
 * @param[in] subscription Subscription to free.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
_sr_unsubscribe(sr_subscription_ctx_t *subscription)
{
    sr_error_info_t *err_info = NULL, *tmp_err;
    char *path;
    int ret;

    assert(subscription);

    if (ATOMIC_LOAD_RELAXED(subscription->thread_running)) {
        /* signal the thread to quit */
        ATOMIC_STORE_RELAXED(subscription->thread_running, 0);

        /* generate a new event for the thread to wake up */
        err_info = sr_shmsub_notify_evpipe(subscription->evpipe_num);

        if (!err_info) {
            /* join the thread */
            ret = pthread_join(subscription->tid, NULL);
            if (ret) {
                sr_errinfo_new(&err_info, SR_ERR_SYS, NULL, "Joining the subscriber thread failed (%s).", strerror(ret));
            }
        }
    }

    /* delete all subscriptions (also removes this subscription from all the sessions) */
    if ((tmp_err = sr_subs_del_all(subscription))) {
        /* continue */
        sr_errinfo_merge(&err_info, tmp_err);
    }

    /* remove subscription from main SHM state */
    sr_shmmain_state_del_evpipe(subscription->conn, subscription->evpipe_num);

    /* defrag main SHM if needed */
    if ((tmp_err = sr_check_main_shm_defrag(subscription->conn))) {
        /* continue */
        sr_errinfo_merge(&err_info, tmp_err);
    }

    /* unlink event pipe */
    if ((tmp_err = sr_path_evpipe(subscription->evpipe_num, &path))) {
        /* continue */
        sr_errinfo_merge(&err_info, tmp_err);
    } else {
        ret = unlink(path);
        free(path);
        if (ret == -1) {
            SR_ERRINFO_SYSERRNO(&err_info, "unlink");
        }
    }

    /* free attributes */
    close(subscription->evpipe);
    pthread_mutex_destroy(&subscription->subs_lock);
    free(subscription);
    return err_info;
}

API int
sr_unsubscribe(sr_subscription_ctx_t *subscription)
{
    sr_error_info_t *err_info = NULL;
    sr_conn_ctx_t *conn;

    if (!subscription) {
        return sr_api_ret(NULL, NULL);
    }

    conn = subscription->conn;

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 1))) {
        return sr_api_ret(NULL, err_info);
    }

    err_info = _sr_unsubscribe(subscription);

    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 1);

    return sr_api_ret(NULL, err_info);
}

/**
 * @brief Perform enabled event on a subscription.
 *
 * @param[in] session Session to use.
 * @param[in] ly_mod Specific module.
 * @param[in] xpath Optional subscription xpath.
 * @param[in] callback Callback to call.
 * @param[in] private_data Arbitrary callback data.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
sr_module_change_subscribe_running_enable(sr_session_ctx_t *session, const struct lys_module *ly_mod, const char *xpath,
        sr_module_change_cb callback, void *private_data, int opts)
{
    sr_error_info_t *err_info = NULL;
    sr_conn_ctx_t *conn = session->conn;
    struct lyd_node *enabled_data = NULL, *node;
    struct sr_mod_info_s mod_info;
    sr_session_ctx_t tmp_sess;
    sr_error_t err_code;

    memset(&mod_info, 0, sizeof mod_info);
    memset(&tmp_sess, 0, sizeof tmp_sess);

    /* create mod_info structure with this module only */
    if ((err_info = sr_shmmod_collect_modules(conn, ly_mod, SR_DS_RUNNING, 0, &mod_info))) {
        return err_info;
    }

    /* MODULES READ LOCK */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 0, session->sid))) {
        sr_modinfo_free(&mod_info);
        return err_info;
    }

    /* get the current running datastore data */
    if ((err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_REQ, 1, NULL, NULL))) {
        goto cleanup_mods_unlock;
    }

    /* select only the subscribed-to subtree */
    if (mod_info.data) {
        if (xpath) {
            if ((err_info = sr_ly_data_dup_xpath_select(mod_info.data, (char **)&xpath, 1, &enabled_data))) {
                goto cleanup_mods_unlock;
            }
        } else {
            enabled_data = mod_info.data;
            mod_info.data = NULL;
        }
    }

    /* these data will be presented as newly created, make such a diff */
    if (enabled_data) {
        mod_info.diff = enabled_data;
        mod_info.mods[0].state |= MOD_INFO_CHANGED;

        LY_TREE_FOR(enabled_data, node) {
            /* top-level "create" operation that is inherited */
            if ((err_info = sr_edit_set_oper(node, "create"))) {
                goto cleanup_mods_unlock;
            }

            /* user-ordered lists need information about position */
            if ((err_info = sr_edit_created_subtree_apply_move(node))) {
                goto cleanup_mods_unlock;
            }
        }
    }

    tmp_sess.conn = conn;
    tmp_sess.ds = mod_info.ds;
    tmp_sess.dt[tmp_sess.ds].diff = mod_info.diff;

    if (!(opts & SR_SUBSCR_DONE_ONLY)) {
        tmp_sess.ev = SR_SUB_EV_ENABLED;
        SR_LOG_INF("Triggering \"%s\" \"%s\" event on enabled data.", ly_mod->name, sr_ev2str(tmp_sess.ev));

        /* present all changes in an "enabled" event */
        err_code = callback(&tmp_sess, ly_mod->name, xpath, sr_ev2api(tmp_sess.ev), private_data);
        if (err_code != SR_ERR_OK) {
            /* callback failed but it is the only one so no "abort" event is necessary */
            sr_errinfo_new(&err_info, SR_ERR_CALLBACK_FAILED, NULL, "Subscribing to \"%s\" changes failed.", ly_mod->name);
            if (tmp_sess.err_info && (tmp_sess.err_info->err_code == SR_ERR_OK)) {
                /* remember callback error info */
                sr_errinfo_merge(&err_info, tmp_sess.err_info);
            }
            goto cleanup_mods_unlock;
        }
    }

    /* finish with a "done" event just because this event should imitate a regular configuration change */
    tmp_sess.ev = SR_SUB_EV_DONE;
    SR_LOG_INF("Triggering \"%s\" \"%s\" event on enabled data.", ly_mod->name, sr_ev2str(tmp_sess.ev));
    callback(&tmp_sess, ly_mod->name, xpath, sr_ev2api(tmp_sess.ev), private_data);

    /* success */

cleanup_mods_unlock:
    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 0);

    sr_modinfo_free(&mod_info);
    return err_info;
}

/**
 * @brief Allocate and start listening on a new subscription.
 *
 * @param[in] conn Connection to use.
 * @param[in] opts Subscription options.
 * @param[out] subs_p Allocated subscription.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
sr_subs_new(sr_conn_ctx_t *conn, sr_subscr_options_t opts, sr_subscription_ctx_t **subs_p)
{
    sr_error_info_t *err_info = NULL;
    sr_main_shm_t *main_shm;
    char *path = NULL;
    int ret;

    /* allocate new subscription */
    *subs_p = calloc(1, sizeof **subs_p);
    SR_CHECK_MEM_RET(!*subs_p, err_info);
    sr_mutex_init(&(*subs_p)->subs_lock, 0);
    (*subs_p)->conn = conn;
    (*subs_p)->evpipe = -1;

    /* get new event pipe number and increment it */
    main_shm = (sr_main_shm_t *)(*subs_p)->conn->main_shm.addr;
    (*subs_p)->evpipe_num = ATOMIC_INC_RELAXED(main_shm->new_evpipe_num);
    if ((*subs_p)->evpipe_num == (uint32_t)(ATOMIC_T_MAX - 1)) {
        /* the value in the main SHM is actually ATOMIC_T_MAX and calling another INC would cause an overflow */
        ATOMIC_STORE_RELAXED(main_shm->new_evpipe_num, 1);
    }

    /* get event pipe name */
    if ((err_info = sr_path_evpipe((*subs_p)->evpipe_num, &path))) {
        goto error;
    }

    /* create the event pipe */
    if (mkfifo(path, SR_EVPIPE_PERM) == -1) {
        SR_ERRINFO_SYSERRNO(&err_info, "mkfifo");
        goto error;
    }

    /* open it for reading AND writing (just so that there always is a "writer", otherwise it is always ready
     * for reading by select() but returns just EOF on read) */
    (*subs_p)->evpipe = open(path, O_RDWR | O_NONBLOCK);
    if ((*subs_p)->evpipe == -1) {
        SR_ERRINFO_SYSERRNO(&err_info, "open");
        goto error;
    }

    /* add the new subscription into main SHM state */
    if ((err_info = sr_shmmain_state_add_evpipe(conn, (*subs_p)->evpipe_num))) {
        goto error;
    }

    if (!(opts & SR_SUBSCR_NO_THREAD)) {
        /* set thread_running to non-zero so that thread does not immediately quit */
        ATOMIC_STORE_RELAXED((*subs_p)->thread_running, 1);

        /* start the listen thread */
        ret = pthread_create(&(*subs_p)->tid, NULL, sr_shmsub_listen_thread, *subs_p);
        if (ret) {
            sr_errinfo_new(&err_info, SR_ERR_INTERNAL, NULL, "Creating a new thread failed (%s).", strerror(ret));
            goto error;
        }
    }

    free(path);
    return NULL;

error:
    free(path);
    if ((*subs_p)->evpipe > -1) {
        close((*subs_p)->evpipe);
    }
    free(*subs_p);
    return err_info;
}

API int
sr_module_change_subscribe(sr_session_ctx_t *session, const char *module_name, const char *xpath,
        sr_module_change_cb callback, void *private_data, uint32_t priority, sr_subscr_options_t opts,
        sr_subscription_ctx_t **subscription)
{
    sr_error_info_t *err_info = NULL;
    const struct lys_module *ly_mod;
    sr_conn_ctx_t *conn;
    sr_subscr_options_t sub_opts;
    sr_mod_t *shm_mod;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(session->ds) || !module_name || !callback ||
            ((opts & SR_SUBSCR_PASSIVE) && (opts & SR_SUBSCR_ENABLED)) || !subscription, session, err_info);

    conn = session->conn;
    /* only these options are relevant outside this function and will be stored */
    sub_opts = opts & (SR_SUBSCR_DONE_ONLY | SR_SUBSCR_PASSIVE | SR_SUBSCR_UPDATE);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    /* is the module name valid? */
    ly_mod = ly_ctx_get_module(conn->ly_ctx, module_name, NULL, 1);
    if (!ly_mod) {
        sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
        goto error_rdunlock;
    }

    /* check write/read perm */
    if ((err_info = sr_perm_check(module_name, (opts & SR_SUBSCR_PASSIVE) ? 0 : 1))) {
        goto error_rdunlock;
    }

    /* call the callback with the current running configuration so that it is properly applied */
    if ((session->ds == SR_DS_RUNNING) && (opts & SR_SUBSCR_ENABLED)) {
        /* do not hold write lock here, would block callback from calling API functions */
        if ((err_info = sr_module_change_subscribe_running_enable(session, ly_mod, xpath, callback, private_data, opts))) {
            goto error_rdunlock;
        }
    }

    /* SHM READ UNLOCK */
    sr_shmmain_unlock(conn, 0, 0);

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 1))) {
        return sr_api_ret(session, err_info);
    }

    if (!(opts & SR_SUBSCR_CTX_REUSE)) {
        /* create a new subscription */
        if ((err_info = sr_subs_new(conn, opts, subscription))) {
            goto error_wrunlock;
        }
    }

    /* find module */
    shm_mod = sr_shmmain_find_module(&conn->main_shm, conn->ext_shm.addr, module_name, 0);
    SR_CHECK_INT_GOTO(!shm_mod, err_info, error_wrunlock_unsub);

    /* add module subscription into main SHM */
    if ((err_info = sr_shmmod_conf_subscription_add(&conn->ext_shm, shm_mod, xpath, session->ds, priority, sub_opts,
            (*subscription)->evpipe_num))) {
        goto error_wrunlock_unsub;
    }

    /* add subscription into structure and create separate specific SHM segment */
    if ((err_info = sr_sub_conf_add(session, module_name, xpath, callback, private_data, priority, sub_opts,
            *subscription))) {
        goto error_wrunlock_unsub_unmod;
    }

    /* defrag main SHM if needed */
    if ((err_info = sr_check_main_shm_defrag(conn))) {
        goto error_wrunlock_unsub_unmod;
    }

    /* add the subscription into session */
    if ((err_info = sr_ptr_add(&session->ptr_lock, (void ***)&session->subscriptions, &session->subscription_count,
                *subscription))) {
        goto error_wrunlock_unsub_unmod;
    }

    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 1);

    return sr_api_ret(session, NULL);

error_wrunlock_unsub_unmod:
    sr_shmmod_conf_subscription_del(conn->ext_shm.addr, shm_mod, xpath, session->ds, priority, sub_opts,
            (*subscription)->evpipe_num, 0, NULL);

error_wrunlock_unsub:
    if (opts & SR_SUBSCR_CTX_REUSE) {
        sr_sub_conf_del(module_name, xpath, session->ds, callback, private_data, priority, sub_opts, *subscription);
    } else {
        _sr_unsubscribe(*subscription);
        *subscription = NULL;
    }

error_wrunlock:
    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 1);

    return sr_api_ret(session, err_info);

error_rdunlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(conn, 0, 0);

    return sr_api_ret(session, err_info);
}

API int
sr_get_changes_iter(sr_session_ctx_t *session, const char *xpath, sr_change_iter_t **iter)
{
    sr_error_info_t *err_info = NULL;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(session->ds) || (session->ev == SR_SUB_EV_NONE) || !xpath || !iter,
            session, err_info);

    if ((session->ev != SR_SUB_EV_ENABLED) && (session->ev != SR_SUB_EV_DONE) && !session->dt[session->ds].diff) {
        sr_errinfo_new(&err_info, SR_ERR_INVAL_ARG, NULL, "Session without changes.");
        return sr_api_ret(session, err_info);
    }

    *iter = malloc(sizeof **iter);
    if (!*iter) {
        SR_ERRINFO_MEM(&err_info);
        return sr_api_ret(session, err_info);
    }

    if (session->dt[session->ds].diff) {
        (*iter)->set = lyd_find_path(session->dt[session->ds].diff, xpath);
    } else {
        (*iter)->set = ly_set_new();
    }
    SR_CHECK_MEM_GOTO(!(*iter)->set, err_info, error);
    (*iter)->idx = 0;

    return sr_api_ret(session, NULL);

error:
    free(*iter);
    return sr_api_ret(session, err_info);
}

/**
 * @brief Transform libyang node into sysrepo value.
 *
 * @param[in] node libyang node.
 * @param[in] llist_value_str Optional value to override.
 * @param[in] keys_predicate Optional keys predicate to override.
 * @param[out] sr_val_p Transformed sysrepo value.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
sr_lyd_node2sr_val(const struct lyd_node *node, const char *llist_value_str, const char *keys_predicate, sr_val_t **sr_val_p)
{
    char *ptr;
    sr_error_info_t *err_info = NULL;
    uint32_t start, end;
    sr_val_t *sr_val;
    struct lyd_node_leaf_list *leaf;
    struct lys_node_list *slist;

    sr_val = calloc(1, sizeof *sr_val);
    SR_CHECK_MEM_GOTO(!sr_val, err_info, error);

    sr_val->xpath = lyd_path(node);
    SR_CHECK_MEM_GOTO(!sr_val->xpath, err_info, error);

    switch (node->schema->nodetype) {
    case LYS_LIST:
        /* fix the xpath if needed */
        if (keys_predicate) {
            slist = (struct lys_node_list *)node->schema;

            end = slist->keys_size;
            start = strlen(sr_val->xpath);
            do {
                --end;

                /* going backwards, skip the value */
                start -= 2;
                assert(sr_val->xpath[start + 1] == ']');
                for (ptr = sr_val->xpath + start - 1; ptr[0] != sr_val->xpath[start]; --ptr) {
                    SR_CHECK_INT_GOTO(ptr == sr_val->xpath, err_info, error);
                }
                start = (ptr - sr_val->xpath) - 2;
                assert(sr_val->xpath[start + 1] == '=');
                /* skip the key name */
                start -= strlen(slist->keys[end]->name);
                assert(sr_val->xpath[start] == '[');
            } while (end);
            assert(!strncmp((sr_val->xpath + start) - strlen(slist->name), slist->name, strlen(slist->name)));
            end = strlen(sr_val->xpath);

            /* enlarge string if needed */
            if (strlen(keys_predicate) > end - start) {
                /* original length + the difference + ending 0 */
                sr_val->xpath = sr_realloc(sr_val->xpath, end + (strlen(keys_predicate) - (end - start)) + 1);
                SR_CHECK_MEM_GOTO(!sr_val->xpath, err_info, error);
            }

            /* replace the predicates */
            strcpy(sr_val->xpath + start, keys_predicate);
        }
        sr_val->type = SR_LIST_T;
        break;
    case LYS_CONTAINER:
        if (((struct lys_node_container *)node->schema)->presence) {
            sr_val->type = SR_CONTAINER_PRESENCE_T;
        } else {
            sr_val->type = SR_CONTAINER_T;
        }
        break;
    case LYS_NOTIF:
        sr_val->type = SR_NOTIFICATION_T;
        break;
    case LYS_ANYXML:
        sr_val->type = SR_ANYXML_T;
        break;
    case LYS_ANYDATA:
        sr_val->type = SR_ANYDATA_T;
        break;
    case LYS_LEAFLIST:
        /* fix the xpath if needed */
        if (llist_value_str) {
            end = strlen(sr_val->xpath) - 1;
            assert(((sr_val->xpath[end - 1] == '\'') || (sr_val->xpath[end - 1] == '\"')) && (sr_val->xpath[end] == ']'));

            for (ptr = sr_val->xpath + end - 2; ptr[0] != sr_val->xpath[end - 1]; --ptr) {
                SR_CHECK_INT_GOTO(ptr == sr_val->xpath, err_info, error);
            }
            start = ptr - sr_val->xpath;

            /* enlarge string if needed */
            if (strlen(llist_value_str) + 2 > end - start) {
                /* original length + the difference + ending 0 */
                sr_val->xpath = sr_realloc(sr_val->xpath, (end + 1) + ((strlen(llist_value_str) + 2) - (end - start)) + 1);
                SR_CHECK_MEM_GOTO(!sr_val->xpath, err_info, error);
            }

            /* replace the value */
            if (strchr(llist_value_str, '\'')) {
                sprintf(sr_val->xpath + start, "\"%s\"]", llist_value_str);
            } else {
                sprintf(sr_val->xpath + start, "'%s']", llist_value_str);
            }
        }
        /* fallthrough */
    case LYS_LEAF:
        leaf = (struct lyd_node_leaf_list *)node;
        if (!llist_value_str) {
            llist_value_str = leaf->value_str;
        }

        switch (leaf->value_type) {
        case LY_TYPE_BINARY:
            sr_val->type = SR_BINARY_T;
            sr_val->data.binary_val = strdup(llist_value_str);
            break;
        case LY_TYPE_BITS:
            sr_val->type = SR_BITS_T;
            sr_val->data.bits_val = strdup(llist_value_str);
            break;
        case LY_TYPE_BOOL:
            sr_val->type = SR_BOOL_T;
            if (!strcmp(llist_value_str, "true")) {
                sr_val->data.bool_val = true;
            } else {
                sr_val->data.bool_val = false;
            }
            break;
        case LY_TYPE_DEC64:
            sr_val->type = SR_DECIMAL64_T;
            sr_val->data.decimal64_val = strtod(llist_value_str, &ptr);
            if (ptr[0]) {
                sr_errinfo_new(&err_info, SR_ERR_INTERNAL, NULL, "Conversion of \"%s\" to double failed (%s).",
                        llist_value_str, strerror(errno));
                goto error;
            }
            break;
        case LY_TYPE_EMPTY:
            sr_val->type = SR_LEAF_EMPTY_T;
            break;
        case LY_TYPE_ENUM:
            sr_val->type = SR_ENUM_T;
            sr_val->data.enum_val = strdup(llist_value_str);
            break;
        case LY_TYPE_IDENT:
            sr_val->type = SR_IDENTITYREF_T;
            sr_val->data.identityref_val = strdup(llist_value_str);
            break;
        case LY_TYPE_INST:
            sr_val->type = SR_INSTANCEID_T;
            sr_val->data.instanceid_val = strdup(llist_value_str);
            break;
        case LY_TYPE_STRING:
            sr_val->type = SR_STRING_T;
            sr_val->data.string_val = strdup(llist_value_str);
            break;
        case LY_TYPE_INT8:
            sr_val->type = SR_INT8_T;
            sr_val->data.int8_val = strtoll(llist_value_str, &ptr, 10);
            SR_CHECK_INT_GOTO(ptr[0], err_info, error);
            break;
        case LY_TYPE_INT16:
            sr_val->type = SR_INT16_T;
            sr_val->data.int16_val = strtoll(llist_value_str, &ptr, 10);
            SR_CHECK_INT_GOTO(ptr[0], err_info, error);
            break;
        case LY_TYPE_INT32:
            sr_val->type = SR_INT32_T;
            sr_val->data.int32_val = strtoll(llist_value_str, &ptr, 10);
            SR_CHECK_INT_GOTO(ptr[0], err_info, error);
            break;
        case LY_TYPE_INT64:
            sr_val->type = SR_INT64_T;
            sr_val->data.int64_val = strtoll(llist_value_str, &ptr, 10);
            SR_CHECK_INT_GOTO(ptr[0], err_info, error);
            break;
        case LY_TYPE_UINT8:
            sr_val->type = SR_UINT8_T;
            sr_val->data.uint8_val = strtoull(llist_value_str, &ptr, 10);
            SR_CHECK_INT_GOTO(ptr[0], err_info, error);
            break;
        case LY_TYPE_UINT16:
            sr_val->type = SR_UINT16_T;
            sr_val->data.uint16_val = strtoull(llist_value_str, &ptr, 10);
            SR_CHECK_INT_GOTO(ptr[0], err_info, error);
            break;
        case LY_TYPE_UINT32:
            sr_val->type = SR_UINT32_T;
            sr_val->data.uint32_val = strtoull(llist_value_str, &ptr, 10);
            SR_CHECK_INT_GOTO(ptr[0], err_info, error);
            break;
        case LY_TYPE_UINT64:
            sr_val->type = SR_UINT64_T;
            sr_val->data.uint64_val = strtoull(llist_value_str, &ptr, 10);
            SR_CHECK_INT_GOTO(ptr[0], err_info, error);
            break;
        default:
            SR_ERRINFO_INT(&err_info);
            goto error;
        }
        break;
    default:
        SR_ERRINFO_INT(&err_info);
            goto error;
    }

    sr_val->dflt = node->dflt;
    *sr_val_p = sr_val;
    return NULL;

error:
    if (sr_val) {
        free(sr_val->xpath);
    }
    free(sr_val);
    return err_info;
}

API int
sr_get_change_next(sr_session_ctx_t *session, sr_change_iter_t *iter, sr_change_oper_t *operation,
        sr_val_t **old_value, sr_val_t **new_value)
{
    sr_error_info_t *err_info = NULL;
    struct lyd_attr *attr, *attr2;
    struct lyd_node *node;
    const char *attr_name;
    sr_change_oper_t op;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(session->ds) || !iter || !operation || !old_value || !new_value,
            session, err_info);

    /* get next change */
    if ((err_info = sr_diff_set_getnext(iter->set, &iter->idx, &node, &op))) {
        return sr_api_ret(session, err_info);
    }

    if (!node) {
        /* no more changes */
        return SR_ERR_NOT_FOUND;
    }

    /* create values */
    switch (op) {
    case SR_OP_DELETED:
        if ((err_info = sr_lyd_node2sr_val(node, NULL, NULL, old_value))) {
            return sr_api_ret(session, err_info);
        }
        *new_value = NULL;
        break;
    case SR_OP_MODIFIED:
        /* "orig-value" attribute contains the previous value */
        for (attr = node->attr;
             attr && (strcmp(attr->annotation->module->name, SR_YANG_MOD) || strcmp(attr->name, "orig-value"));
             attr = attr->next);
        if (!attr) {
            SR_ERRINFO_INT(&err_info);
            return sr_api_ret(session, err_info);
        }

        /* "orig-dflt" is present only if the previous value was default */
        for (attr2 = node->attr;
             attr2 && (strcmp(attr2->annotation->module->name, SR_YANG_MOD) || strcmp(attr2->name, "orig-dflt"));
             attr2 = attr2->next);

        if ((err_info = sr_lyd_node2sr_val(node, attr->value_str, NULL, old_value))) {
            return sr_api_ret(session, err_info);
        }
        if (attr2) {
            (*old_value)->dflt = 1;
        } else {
            (*old_value)->dflt = 0;
        }
        if ((err_info = sr_lyd_node2sr_val(node, NULL, NULL, new_value))) {
            return sr_api_ret(session, err_info);
        }
        break;
    case SR_OP_CREATED:
        if (!sr_ly_is_userord(node)) {
            /* not a user-ordered list, so the operation is a simple creation */
            *old_value = NULL;
            if ((err_info = sr_lyd_node2sr_val(node, NULL, NULL, new_value))) {
                return sr_api_ret(session, err_info);
            }
            break;
        }
        /* fallthrough */
    case SR_OP_MOVED:
        if (node->schema->nodetype == LYS_LEAFLIST) {
            attr_name = "value";
        } else {
            assert(node->schema->nodetype == LYS_LIST);
            attr_name = "key";
        }
        /* attribute contains the value of the node before in the order */
        for (attr = node->attr;
             attr && (strcmp(attr->annotation->module->name, "yang") || strcmp(attr->name, attr_name));
             attr = attr->next);
        if (!attr) {
            SR_ERRINFO_INT(&err_info);
            return sr_api_ret(session, err_info);
        }

        if (attr->value_str[0]) {
            if (node->schema->nodetype == LYS_LEAFLIST) {
                err_info = sr_lyd_node2sr_val(node, attr->value_str, NULL, old_value);
            } else {
                err_info = sr_lyd_node2sr_val(node, NULL, attr->value_str, old_value);
            }
            if (err_info) {
                return sr_api_ret(session, err_info);
            }
        } else {
            /* inserted as the first item */
            *old_value = NULL;
        }
        if ((err_info = sr_lyd_node2sr_val(node, NULL, NULL, new_value))) {
            return sr_api_ret(session, err_info);
        }
        break;
    }

    *operation = op;
    return sr_api_ret(session, NULL);
}

API int
sr_get_change_tree_next(sr_session_ctx_t *session, sr_change_iter_t *iter, sr_change_oper_t *operation,
        const struct lyd_node **node, const char **prev_value, const char **prev_list, bool *prev_dflt)
{
    sr_error_info_t *err_info = NULL;
    struct lyd_attr *attr, *attr2;
    const char *attr_name;

    SR_CHECK_ARG_APIRET(!session || !IS_WRITABLE_DS(session->ds) || !iter || !operation || !node || !prev_value
            || !prev_list || !prev_dflt, session, err_info);

    *prev_value = NULL;
    *prev_list = NULL;
    *prev_dflt = 0;

    /* get next change */
    if ((err_info = sr_diff_set_getnext(iter->set, &iter->idx, (struct lyd_node **)node, operation))) {
        return sr_api_ret(session, err_info);
    }

    if (!*node) {
        /* no more changes */
        return SR_ERR_NOT_FOUND;
    }

    /* create values */
    switch (*operation) {
    case SR_OP_DELETED:
        /* nothing to do */
        break;
    case SR_OP_MODIFIED:
        /* "orig-value" attribute contains the previous value */
        for (attr = (*node)->attr;
             attr && (strcmp(attr->annotation->module->name, SR_YANG_MOD) || strcmp(attr->name, "orig-value"));
             attr = attr->next);
        if (!attr) {
            SR_ERRINFO_INT(&err_info);
            return sr_api_ret(session, err_info);
        }
        *prev_value = attr->value_str;

        /* "orig-dflt" is present only if the previous value was default */
        for (attr2 = (*node)->attr;
             attr2 && (strcmp(attr2->annotation->module->name, SR_YANG_MOD) || strcmp(attr2->name, "orig-dflt"));
             attr2 = attr2->next);
        if (attr2) {
            *prev_dflt = 1;
        }
        break;
    case SR_OP_CREATED:
        if (!sr_ly_is_userord(*node)) {
            /* nothing to do */
            break;
        }
        /* fallthrough */
    case SR_OP_MOVED:
        if ((*node)->schema->nodetype == LYS_LEAFLIST) {
            attr_name = "value";
        } else {
            assert((*node)->schema->nodetype == LYS_LIST);
            attr_name = "key";
        }

        /* attribute contains the value (predicates) of the preceding instance in the order */
        for (attr = (*node)->attr;
             attr && (strcmp(attr->annotation->module->name, "yang") || strcmp(attr->name, attr_name));
             attr = attr->next);
        if (!attr) {
            SR_ERRINFO_INT(&err_info);
            return sr_api_ret(session, err_info);
        }
        if ((*node)->schema->nodetype == LYS_LEAFLIST) {
            *prev_value = attr->value_str;
        } else {
            assert((*node)->schema->nodetype == LYS_LIST);
            *prev_list = attr->value_str;
        }
        break;
    }

    return sr_api_ret(session, NULL);
}

API void
sr_free_change_iter(sr_change_iter_t *iter)
{
    if (!iter) {
        return;
    }

    ly_set_free(iter->set);
    free(iter);
}

/**
 * @brief Subscribe to an RPC/action.
 *
 * @param[in] session Session to use.
 * @param[in] path Path to subscribe to.
 * @param[in] callback Callback.
 * @param[in] tree_callback Tree callback.
 * @param[in] private_data Arbitrary callback data.
 * @param[in] opts Subscription options.
 * @param[out] subscription Subscription structure.
 * @return err_code (SR_ERR_OK on success).
 */
static int
_sr_rpc_subscribe(sr_session_ctx_t *session, const char *xpath, sr_rpc_cb callback, sr_rpc_tree_cb tree_callback,
        void *private_data, uint32_t priority, sr_subscr_options_t opts, sr_subscription_ctx_t **subscription)
{
    sr_error_info_t *err_info = NULL;
    char *module_name = NULL, *op_path = NULL;
    const struct lys_node *op;
    const struct lys_module *ly_mod;
    sr_conn_ctx_t *conn;
    sr_rpc_t *shm_rpc;
    off_t shm_rpc_off;
    int last_removed;

    SR_CHECK_ARG_APIRET(!session || !xpath || (!callback && !tree_callback) || !subscription, session, err_info);

    conn = session->conn;

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 1))) {
        return sr_api_ret(session, err_info);
    }

    module_name = sr_get_first_ns(xpath);
    if (!module_name) {
        sr_errinfo_new(&err_info, SR_ERR_INVAL_ARG, NULL, "Invalid xpath \"%s\".", xpath);
        goto error_unlock;
    }

    /* is the module name valid? */
    ly_mod = ly_ctx_get_module(conn->ly_ctx, module_name, NULL, 1);
    if (!ly_mod) {
        sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
        goto error_unlock;
    }

    /* check write perm */
    if ((err_info = sr_perm_check(module_name, 1))) {
        goto error_unlock;
    }

    /* is the xpath valid? */
    if ((err_info = sr_get_trim_predicates(xpath, &op_path))) {
        goto error_unlock;
    }

    if (!(op = ly_ctx_get_node(conn->ly_ctx, NULL, op_path, 0))) {
        sr_errinfo_new_ly(&err_info, conn->ly_ctx);
        goto error_unlock;
    }
    if (!(op->nodetype & (LYS_RPC | LYS_ACTION))) {
        sr_errinfo_new(&err_info, SR_ERR_INVAL_ARG, NULL, "Path \"%s\" does not identify an RPC nor an action.", op_path);
        goto error_unlock;
    }

    if (!(opts & SR_SUBSCR_CTX_REUSE)) {
        /* create a new subscription */
        if ((err_info = sr_subs_new(conn, opts, subscription))) {
            goto error_unlock;
        }
    }

    /* find RPC struct or add a new one */
    shm_rpc = sr_shmmain_find_rpc((sr_main_shm_t *)conn->main_shm.addr, conn->ext_shm.addr, op_path, 0);
    if (!shm_rpc && (err_info = sr_shmmain_add_rpc(conn, op_path, &shm_rpc))) {
        goto error_unlock_unsub;
    }
    shm_rpc_off = ((char *)shm_rpc) - conn->ext_shm.addr;

    /* add RPC/action subscription into main SHM (which may be remapped) */
    if ((err_info = sr_shmmain_rpc_subscription_add(&conn->ext_shm, shm_rpc_off, xpath, priority, (*subscription)->evpipe_num))) {
        goto error_unlock_unsub;
    }
    shm_rpc = (sr_rpc_t *)(conn->ext_shm.addr + shm_rpc_off);

    /* add subscription into structure and create separate specific SHM segment */
    if ((err_info = sr_sub_rpc_add(session, op_path, xpath, callback, tree_callback, private_data, priority, *subscription))) {
        goto error_unlock_unsub_unrpc;
    }

    /* defrag main SHM if needed */
    if ((err_info = sr_check_main_shm_defrag(conn))) {
        goto error_unlock_unsub_unrpc;
    }

    /* add the subscription into session */
    if ((err_info = sr_ptr_add(&session->ptr_lock, (void ***)&session->subscriptions, &session->subscription_count,
                *subscription))) {
        goto error_unlock_unsub_unrpc;
    }

    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 1);

    free(module_name);
    free(op_path);
    return sr_api_ret(session, NULL);

error_unlock_unsub_unrpc:
    sr_shmmain_rpc_subscription_del(conn->ext_shm.addr, shm_rpc, xpath, priority, (*subscription)->evpipe_num, 0, &last_removed);
    if (last_removed) {
        sr_shmmain_del_rpc((sr_main_shm_t *)conn->main_shm.addr, conn->ext_shm.addr, NULL, shm_rpc->op_path);
    }

error_unlock_unsub:
    if (opts & SR_SUBSCR_CTX_REUSE) {
        sr_sub_rpc_del(op_path, xpath, callback, tree_callback, private_data, priority, *subscription);
    } else {
        _sr_unsubscribe(*subscription);
        *subscription = NULL;
    }

error_unlock:
    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 1);

    free(module_name);
    free(op_path);
    return sr_api_ret(session, err_info);
}

API int
sr_rpc_subscribe(sr_session_ctx_t *session, const char *xpath, sr_rpc_cb callback, void *private_data,
        uint32_t priority, sr_subscr_options_t opts, sr_subscription_ctx_t **subscription)
{
    return _sr_rpc_subscribe(session, xpath, callback, NULL, private_data, priority, opts, subscription);
}

API int
sr_rpc_subscribe_tree(sr_session_ctx_t *session, const char *xpath, sr_rpc_tree_cb callback, void *private_data,
        uint32_t priority, sr_subscr_options_t opts, sr_subscription_ctx_t **subscription)
{
    return _sr_rpc_subscribe(session, xpath, NULL, callback, private_data, priority, opts, subscription);
}

API int
sr_rpc_send(sr_session_ctx_t *session, const char *path, const sr_val_t *input, const size_t input_cnt,
        sr_val_t **output, size_t *output_cnt)
{
    sr_error_info_t *err_info = NULL;
    struct lyd_node *input_tree = NULL, *output_tree = NULL, *next, *elem;
    char *val_str, buf[22];
    size_t i;
    int ret;

    SR_CHECK_ARG_APIRET(!session || !output || !output_cnt, session, err_info);

    *output = NULL;
    *output_cnt = 0;

    /* create the container */
    if ((err_info = sr_val_sr2ly(session->conn->ly_ctx, path, NULL, 0, 0, &input_tree))) {
        goto cleanup;
    }

    /* transform input into a data tree */
    for (i = 0; i < input_cnt; ++i) {
        val_str = sr_val_sr2ly_str(session->conn->ly_ctx, &input[i], buf);
        if ((err_info = sr_val_sr2ly(session->conn->ly_ctx, input[i].xpath, val_str, input[i].dflt, 0, &input_tree))) {
            goto cleanup;
        }
    }

    /* API function */
    if ((ret = sr_rpc_send_tree(session, input_tree, &output_tree)) != SR_ERR_OK) {
        lyd_free_withsiblings(input_tree);
        return ret;
    }

    /* transform data tree into an output */
    assert(output_tree && (output_tree->schema->nodetype & (LYS_RPC | LYS_ACTION)));
    *output_cnt = 0;
    *output = NULL;
    LY_TREE_DFS_BEGIN(output_tree, next, elem) {
        if (elem != output_tree) {
            /* allocate new sr_val */
            *output = sr_realloc(*output, (*output_cnt + 1) * sizeof **output);
            SR_CHECK_MEM_GOTO(!*output, err_info, cleanup);

            /* fill it */
            if ((err_info = sr_val_ly2sr(elem, &(*output)[*output_cnt]))) {
                goto cleanup;
            }

            /* now the new value is valid */
            ++(*output_cnt);
        }

        LY_TREE_DFS_END(output_tree, next, elem);
    }

    /* success */

cleanup:
    lyd_free_withsiblings(input_tree);
    lyd_free_withsiblings(output_tree);
    if (err_info) {
        sr_free_values(*output, *output_cnt);
    }
    return sr_api_ret(session, err_info);
}

API int
sr_rpc_send_tree(sr_session_ctx_t *session, struct lyd_node *input, struct lyd_node **output)
{
    sr_error_info_t *err_info = NULL, *cb_err_info = NULL;
    struct sr_mod_info_s mod_info;
    struct lyd_node *input_op;
    sr_mod_data_dep_t *shm_deps;
    uint16_t shm_dep_count;
    char *op_path = NULL, *str;
    uint32_t event_id = 0;

    SR_CHECK_ARG_APIRET(!session || !input || !output, session, err_info);

    *output = NULL;
    memset(&mod_info, 0, sizeof mod_info);

    /* check input data tree */
    switch (input->schema->nodetype) {
    case LYS_ACTION:
        for (input_op = input; input->parent; input = input->parent);
        break;
    case LYS_RPC:
        input_op = input;
        break;
    case LYS_CONTAINER:
    case LYS_LIST:
        /* find the action */
        input_op = input;
        if ((err_info = sr_ly_find_last_parent(&input_op, LYS_ACTION))) {
            return sr_api_ret(session, err_info);
        }
        if (input_op->schema->nodetype == LYS_ACTION) {
            break;
        }
        /* fallthrough */
    default:
        sr_errinfo_new(&err_info, SR_ERR_INVAL_ARG, NULL, "Provided input is not a valid RPC or action invocation.");
        return sr_api_ret(session, err_info);
    }

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    /* check read perm */
    if ((err_info = sr_perm_check(lyd_node_module(input)->name, 0))) {
        goto cleanup_shm_unlock;
    }

    /* get operation path (without predicates) */
    str = lyd_path(input_op);
    SR_CHECK_INT_GOTO(!str, err_info, cleanup_shm_unlock);
    err_info = sr_get_trim_predicates(str, &op_path);
    free(str);
    if (err_info) {
        goto cleanup_shm_unlock;
    }

    /* collect all required modules for input validation (including checking that the nested action
     * can be invoked meaning its parent data node exists) */
    if ((err_info = sr_shmmod_collect_op(session->conn, op_path, input_op, 0, &shm_deps, &shm_dep_count, &mod_info))) {
        goto cleanup_shm_unlock;
    }

    /* MODULES READ LOCK */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 0, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* load all input dependency modules data */
    if ((err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_TYPE_MASK, 1, &session->sid, &cb_err_info)) || cb_err_info) {
        goto cleanup_mods_unlock;
    }

    /* validate the operation */
    if ((err_info = sr_modinfo_op_validate(&mod_info, input_op, shm_deps, shm_dep_count, 0, &session->sid, &cb_err_info))) {
        goto cleanup_mods_unlock;
    }
    if (cb_err_info) {
        goto cleanup_mods_unlock;
    }

    /* publish RPC in an event and wait for a reply from the last subscriber */
    if ((err_info = sr_shmsub_rpc_notify(mod_info.conn, op_path, input, session->sid, &event_id, output, &cb_err_info))) {
        goto cleanup_mods_unlock;
    }

    if (cb_err_info) {
        /* "rpc" event failed, publish "abort" event and finish */
        err_info = sr_shmsub_rpc_notify_abort(mod_info.conn, op_path, input, session->sid, event_id);
        goto cleanup_mods_unlock;
    }

    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 0);

    /* find operation */
    if ((err_info = sr_ly_find_last_parent(output, LYS_RPC | LYS_ACTION))) {
        goto cleanup_shm_unlock;
    }

    /* collect all required modules for output validation */
    sr_modinfo_free(&mod_info);
    memset(&mod_info, 0, sizeof mod_info);
    if ((err_info = sr_shmmod_collect_op(session->conn, op_path, *output, 1, &shm_deps, &shm_dep_count, &mod_info))) {
        goto cleanup_shm_unlock;
    }

    /* MODULES READ LOCK */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 0, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* load all output dependency modules data */
    if ((err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_TYPE_MASK, 1, &session->sid, &cb_err_info)) || cb_err_info) {
        goto cleanup_mods_unlock;
    }

    /* validate the operation */
    if ((err_info = sr_modinfo_op_validate(&mod_info, *output, shm_deps, shm_dep_count, 1, &session->sid, &cb_err_info))) {
        goto cleanup_mods_unlock;
    }
    if (cb_err_info) {
        goto cleanup_mods_unlock;
    }

    /* success */

cleanup_mods_unlock:
    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 0);

cleanup_shm_unlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    free(op_path);
    sr_modinfo_free(&mod_info);
    if (cb_err_info) {
        /* return callback error if some was generated */
        sr_errinfo_merge(&err_info, cb_err_info);
        err_info->err_code = SR_ERR_CALLBACK_FAILED;
    }
    if (err_info) {
        /* free any received output in case of an error */
        lyd_free_withsiblings(*output);
        *output = NULL;
    }
    return sr_api_ret(session, err_info);
}

/**
 * @brief Subscribe to a notification.
 *
 * @param[in] session Session subscription.
 * @param[in] ly_mod Notification module.
 * @param[in] xpath XPath to subscribe to.
 * @param[in] start_time Optional subscription start time.
 * @param[in] stop_time Optional subscription stop time.
 * @param[in] callback Callback.
 * @param[in] tree_callback Tree callback.
 * @param[in] private_data Arbitrary callback data.
 * @param[in] opts Subscription options.
 * @param[out] subscription Subscription structure.
 * @return err_info, NULL on success.
 */
static sr_error_info_t *
_sr_event_notif_subscribe(sr_session_ctx_t *session, const struct lys_module *ly_mod, const char *xpath, time_t start_time,
        time_t stop_time, sr_event_notif_cb callback, sr_event_notif_tree_cb tree_callback, void *private_data,
        sr_subscr_options_t opts, sr_subscription_ctx_t **subscription)
{
    sr_error_info_t *err_info = NULL;
    struct ly_set *set;
    const struct lys_node *ctx_node;
    sr_conn_ctx_t *conn;
    uint32_t i;
    sr_mod_t *shm_mod;

    assert((callback && !tree_callback) || (!callback && tree_callback));

    conn = session->conn;

    /* is the xpath valid, if any? */
    if (xpath) {
        ctx_node = lys_getnext(NULL, NULL, ly_mod, 0);
        if (!ctx_node) {
            sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" does not define any notifications.", ly_mod->name);
            return err_info;
        }

        set = lys_xpath_atomize(ctx_node, LYXP_NODE_ELEM, xpath, 0);
    } else {
        set = lys_find_path(ly_mod, NULL, "//.");
    }
    if (!set) {
        SR_ERRINFO_INT(&err_info);
        return err_info;
    }

    /* there must be some notifications selected */
    for (i = 0; i < set->number; ++i) {
        if (set->set.s[i]->nodetype == LYS_NOTIF) {
            break;
        }
    }
    if (i == set->number) {
        if (xpath) {
            sr_errinfo_new(&err_info, SR_ERR_INVAL_ARG, NULL, "XPath \"%s\" does not select any notifications.", xpath);
        } else {
            sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" does not define any notifications.", ly_mod->name);
        }
        ly_set_free(set);
        return err_info;
    }
    ly_set_free(set);

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 1))) {
        return err_info;
    }

    if (!(opts & SR_SUBSCR_CTX_REUSE)) {
        /* create a new subscription */
        if ((err_info = sr_subs_new(conn, opts, subscription))) {
            goto error_unlock;
        }
    }

    /* find module */
    shm_mod = sr_shmmain_find_module(&conn->main_shm, conn->ext_shm.addr, ly_mod->name, 0);
    SR_CHECK_INT_GOTO(!shm_mod, err_info, error_unlock_unsub);

    if (!start_time) {
        /* add notification subscription into main SHM now if replay was not requested */
        if ((err_info = sr_shmmod_notif_subscription_add(&conn->ext_shm, shm_mod, (*subscription)->evpipe_num))) {
            goto error_unlock_unsub;
        }
    }

    /* add subscription into structure and create separate specific SHM segment */
    if ((err_info = sr_sub_notif_add(session, ly_mod->name, xpath, start_time, stop_time, callback, tree_callback,
                private_data, *subscription))) {
        goto error_unlock_unsub_unmod;
    }

    if (start_time) {
        /* notify subscription there are already some events (replay needs to be performed) */
        if ((err_info = sr_shmsub_notify_evpipe((*subscription)->evpipe_num))) {
            goto error_unlock_unsub;
        }
    }

    /* add the subscription into session */
    if ((err_info = sr_ptr_add(&session->ptr_lock, (void ***)&session->subscriptions, &session->subscription_count,
                *subscription))) {
        goto error_unlock_unsub_unmod;
    }

    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 1);

    return NULL;

error_unlock_unsub_unmod:
    if (!start_time) {
        sr_shmmod_notif_subscription_del(conn->ext_shm.addr, shm_mod, (*subscription)->evpipe_num, 0, NULL);
    }

error_unlock_unsub:
    if (opts & SR_SUBSCR_CTX_REUSE) {
        sr_sub_notif_del(ly_mod->name, xpath, start_time, stop_time, callback, tree_callback, private_data, *subscription, 0);
    } else {
        _sr_unsubscribe(*subscription);
        *subscription = NULL;
    }

error_unlock:
    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 1);

    return err_info;
}

API int
sr_event_notif_subscribe(sr_session_ctx_t *session, const char *module_name, const char *xpath, time_t start_time,
        time_t stop_time, sr_event_notif_cb callback, void *private_data, sr_subscr_options_t opts,
        sr_subscription_ctx_t **subscription)
{
    sr_error_info_t *err_info = NULL;
    time_t cur_ts = time(NULL);
    const struct lys_module *ly_mod;

    SR_CHECK_ARG_APIRET(!session || !module_name || (start_time && (start_time > cur_ts))
            || (stop_time && (!start_time || (stop_time < start_time))) || !callback || !subscription, session, err_info);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    /* is the module name valid? */
    ly_mod = ly_ctx_get_module(session->conn->ly_ctx, module_name, NULL, 1);
    if (!ly_mod) {
        sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
        sr_shmmain_unlock(session->conn, 0, 0);
        return sr_api_ret(session, err_info);
    }

    /* check write perm */
    if ((err_info = sr_perm_check(module_name, 1))) {
        sr_shmmain_unlock(session->conn, 0, 0);
        return sr_api_ret(session, err_info);
    }

    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    /* subscribe */
    err_info = _sr_event_notif_subscribe(session, ly_mod, xpath, start_time, stop_time, callback, NULL, private_data,
            opts, subscription);
    return sr_api_ret(session, err_info);
}

API int
sr_event_notif_subscribe_tree(sr_session_ctx_t *session, const char *module_name, const char *xpath, time_t start_time,
        time_t stop_time, sr_event_notif_tree_cb callback, void *private_data, sr_subscr_options_t opts,
        sr_subscription_ctx_t **subscription)
{
    sr_error_info_t *err_info = NULL;
    time_t cur_ts = time(NULL);
    const struct lys_module *ly_mod;

    SR_CHECK_ARG_APIRET(!session || !module_name || (start_time && (start_time > cur_ts))
            || (stop_time && (!start_time || (stop_time < start_time))) || !callback || !subscription, session, err_info);

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    /* is the module name valid? */
    ly_mod = ly_ctx_get_module(session->conn->ly_ctx, module_name, NULL, 1);
    if (!ly_mod) {
        sr_errinfo_new(&err_info, SR_ERR_NOT_FOUND, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
        sr_shmmain_unlock(session->conn, 0, 0);
        return sr_api_ret(session, err_info);
    }

    /* check write perm */
    if ((err_info = sr_perm_check(module_name, 1))) {
        sr_shmmain_unlock(session->conn, 0, 0);
        return sr_api_ret(session, err_info);
    }

    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    /* subscribe */
    err_info = _sr_event_notif_subscribe(session, ly_mod, xpath, start_time, stop_time, NULL, callback, private_data,
            opts, subscription);
    return sr_api_ret(session, err_info);
}

API int
sr_event_notif_send(sr_session_ctx_t *session, const char *path, const sr_val_t *values, const size_t values_cnt)
{
    sr_error_info_t *err_info = NULL;
    struct lyd_node *notif_tree = NULL;
    char *val_str, buf[22];
    size_t i;
    int ret;

    SR_CHECK_ARG_APIRET(!session || !path, session, err_info);

    /* create the container */
    if ((err_info = sr_val_sr2ly(session->conn->ly_ctx, path, NULL, 0, 0, &notif_tree))) {
        goto cleanup;
    }

    /* transform values into a data tree */
    for (i = 0; i < values_cnt; ++i) {
        val_str = sr_val_sr2ly_str(session->conn->ly_ctx, &values[i], buf);
        if ((err_info = sr_val_sr2ly(session->conn->ly_ctx, values[i].xpath, val_str, values[i].dflt, 0, &notif_tree))) {
            goto cleanup;
        }
    }

    /* API function */
    if ((ret = sr_event_notif_send_tree(session, notif_tree)) != SR_ERR_OK) {
        lyd_free_withsiblings(notif_tree);
        return ret;
    }

    /* success */

cleanup:
    lyd_free_withsiblings(notif_tree);
    return sr_api_ret(session, err_info);
}

API int
sr_event_notif_send_tree(sr_session_ctx_t *session, struct lyd_node *notif)
{
    sr_error_info_t *err_info = NULL, *cb_err_info = NULL, *tmp_err_info = NULL;
    struct sr_mod_info_s mod_info;
    struct lyd_node *notif_op;
    sr_mod_data_dep_t *shm_deps;
    sr_mod_t *shm_mod;
    time_t notif_ts;
    uint16_t shm_dep_count;
    sr_mod_notif_sub_t *notif_subs;
    uint32_t notif_sub_count;
    char *xpath = NULL;

    SR_CHECK_ARG_APIRET(!session || !notif, session, err_info);

    memset(&mod_info, 0, sizeof mod_info);

    /* remember when the notification was generated */
    notif_ts = time(NULL);

    /* check notif data tree */
    switch (notif->schema->nodetype) {
    case LYS_NOTIF:
        for (notif_op = notif; notif->parent; notif = notif->parent);
        break;
    case LYS_CONTAINER:
    case LYS_LIST:
        /* find the notification */
        notif_op = notif;
        if ((err_info = sr_ly_find_last_parent(&notif_op, LYS_NOTIF))) {
            return sr_api_ret(session, err_info);
        }
        if (notif_op->schema->nodetype == LYS_NOTIF) {
            break;
        }
        /* fallthrough */
    default:
        sr_errinfo_new(&err_info, SR_ERR_INVAL_ARG, NULL, "Provided tree is not a valid notification invocation.");
        return sr_api_ret(session, err_info);
    }

    /* SHM READ LOCK */
    if ((err_info = sr_shmmain_lock_remap(session->conn, 0, 0))) {
        return sr_api_ret(session, err_info);
    }

    /* check write/read perm */
    shm_mod = sr_shmmain_find_module(&session->conn->main_shm, session->conn->ext_shm.addr, lyd_node_module(notif)->name, 0);
    SR_CHECK_INT_GOTO(!shm_mod, err_info, cleanup_shm_unlock);
    if ((err_info = sr_perm_check(lyd_node_module(notif)->name, (shm_mod->flags & SR_MOD_REPLAY_SUPPORT) ? 1 : 0))) {
        goto cleanup_shm_unlock;
    }

    /* collect all required modules for validation (including checking that the nested notification
     * can be invoked meaning its parent data node exists) */
    xpath = lys_data_path(notif_op->schema);
    SR_CHECK_MEM_GOTO(!xpath, err_info, cleanup_shm_unlock);
    if ((err_info = sr_shmmod_collect_op(session->conn, xpath, notif_op, 0, &shm_deps, &shm_dep_count, &mod_info))) {
        goto cleanup_shm_unlock;
    }

    /* MODULES READ LOCK */
    if ((err_info = sr_shmmod_modinfo_rdlock(&mod_info, 0, session->sid))) {
        goto cleanup_mods_unlock;
    }

    /* load all input dependency modules data */
    if ((err_info = sr_modinfo_data_load(&mod_info, MOD_INFO_TYPE_MASK, 1, &session->sid, &cb_err_info)) || cb_err_info) {
        goto cleanup_mods_unlock;
    }

    /* validate the operation */
    if ((err_info = sr_modinfo_op_validate(&mod_info, notif_op, shm_deps, shm_dep_count, 0, &session->sid, &cb_err_info))) {
        goto cleanup_mods_unlock;
    }
    if (cb_err_info) {
        goto cleanup_mods_unlock;
    }

    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 0);

    /* store the notification for a replay, we continue on failure */
    err_info = sr_replay_store(session->conn, notif, notif_ts);

    /* check that there is a subscriber */
    if ((tmp_err_info = sr_notif_find_subscriber(session->conn, lyd_node_module(notif)->name, &notif_subs, &notif_sub_count))) {
        goto cleanup_shm_unlock;
    }

    if (notif_sub_count) {
        /* publish notif in an event, do not wait for subscribers */
        if ((tmp_err_info = sr_shmsub_notif_notify(notif, notif_ts, session->sid, (uint32_t *)notif_subs, notif_sub_count))) {
            goto cleanup_shm_unlock;
        }
    } else {
        SR_LOG_INF("There are no subscribers for \"%s\" notifications.", lyd_node_module(notif)->name);
    }

    /* success */
    goto cleanup_shm_unlock;

cleanup_mods_unlock:
    /* MODULES UNLOCK */
    sr_shmmod_modinfo_unlock(&mod_info, 0);

cleanup_shm_unlock:
    /* SHM READ UNLOCK */
    sr_shmmain_unlock(session->conn, 0, 0);

    free(xpath);
    sr_modinfo_free(&mod_info);
    if (tmp_err_info) {
        sr_errinfo_merge(&err_info, tmp_err_info);
    }
    if (cb_err_info) {
        /* return callback error if some was generated */
        sr_errinfo_merge(&err_info, cb_err_info);
        err_info->err_code = SR_ERR_CALLBACK_FAILED;
    }
    return sr_api_ret(session, err_info);
}

API int
sr_oper_get_items_subscribe(sr_session_ctx_t *session, const char *module_name, const char *path,
        sr_oper_get_items_cb callback, void *private_data, sr_subscr_options_t opts, sr_subscription_ctx_t **subscription)
{
    sr_error_info_t *err_info = NULL;
    sr_conn_ctx_t *conn;
    char *schema_path = NULL;
    const struct lys_module *mod;
    struct ly_set *set = NULL;
    sr_mod_oper_sub_type_t sub_type;
    sr_mod_t *shm_mod;
    uint16_t i;

    SR_CHECK_ARG_APIRET(!session || !module_name || !path || !callback || !subscription, session, err_info);

    conn = session->conn;

    /* SHM WRITE LOCK */
    if ((err_info = sr_shmmain_lock_remap(conn, 1, 1))) {
        return sr_api_ret(session, err_info);
    }

    mod = ly_ctx_get_module(conn->ly_ctx, module_name, NULL, 1);
    if (!mod) {
        sr_errinfo_new(&err_info, SR_ERR_INVAL_ARG, NULL, "Module \"%s\" was not found in sysrepo.", module_name);
        goto error_unlock;
    }

    /* check write perm */
    if ((err_info = sr_perm_check(module_name, 1))) {
        goto error_unlock;
    }

    schema_path = ly_path_data2schema(conn->ly_ctx, path);
    set = lys_find_path(mod, NULL, schema_path);
    if (!set) {
        sr_errinfo_new_ly(&err_info, conn->ly_ctx);
        goto error_unlock;
    } else if (!set->number) {
        sr_errinfo_new(&err_info, SR_ERR_INVAL_ARG, NULL, "XPath \"%s\" does not point to any nodes.", path);
        goto error_unlock;
    }

    /* find out what kinds of nodes are provided */
    sub_type = SR_OPER_SUB_NONE;
    for (i = 0; i < set->number; ++i) {
        switch (set->set.s[i]->flags & LYS_CONFIG_MASK) {
        case LYS_CONFIG_R:
            if (sub_type == SR_OPER_SUB_CONFIG) {
                sub_type = SR_OPER_SUB_MIXED;
            } else {
                sub_type = SR_OPER_SUB_STATE;
            }
            break;
        case LYS_CONFIG_W:
            if (sub_type == SR_OPER_SUB_STATE) {
                sub_type = SR_OPER_SUB_MIXED;
            } else {
                sub_type = SR_OPER_SUB_CONFIG;
            }
            break;
        default:
            SR_ERRINFO_INT(&err_info);
            goto error_unlock;
        }

        if (sub_type == SR_OPER_SUB_MIXED) {
            /* we found both config type nodes, nothing more to look for */
            break;
        }
    }

    if (!(opts & SR_SUBSCR_CTX_REUSE)) {
        /* create a new subscription */
        if ((err_info = sr_subs_new(conn, opts, subscription))) {
            goto error_unlock;
        }
    }

    /* find module */
    shm_mod = sr_shmmain_find_module(&conn->main_shm, conn->ext_shm.addr, module_name, 0);
    SR_CHECK_INT_GOTO(!shm_mod, err_info, error_unlock_unsub);

    /* add oper subscription into main SHM */
    if ((err_info = sr_shmmod_oper_subscription_add(&conn->ext_shm, shm_mod, path, sub_type, (*subscription)->evpipe_num))) {
        goto error_unlock_unsub;
    }

    /* add subscription into structure and create separate specific SHM segment */
    if ((err_info = sr_sub_oper_add(session, module_name, path, callback, private_data, *subscription))) {
        goto error_unlock_unsub_unmod;
    }

    /* defrag main SHM if needed */
    if ((err_info = sr_check_main_shm_defrag(conn))) {
        goto error_unlock_unsub_unmod;
    }

    /* add the subscription into session */
    if ((err_info = sr_ptr_add(&session->ptr_lock, (void ***)&session->subscriptions, &session->subscription_count,
                *subscription))) {
        goto error_unlock_unsub_unmod;
    }

    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 1);

    free(schema_path);
    ly_set_free(set);
    return sr_api_ret(session, NULL);

error_unlock_unsub_unmod:
    sr_shmmod_oper_subscription_del(conn->ext_shm.addr, shm_mod, path, (*subscription)->evpipe_num, 0);

error_unlock_unsub:
    if (opts & SR_SUBSCR_CTX_REUSE) {
        sr_sub_oper_del(module_name, path, *subscription);
    } else {
        _sr_unsubscribe(*subscription);
        *subscription = NULL;
    }

error_unlock:
    /* SHM WRITE UNLOCK */
    sr_shmmain_unlock(conn, 1, 1);

    free(schema_path);
    ly_set_free(set);
    return sr_api_ret(session, err_info);
}
