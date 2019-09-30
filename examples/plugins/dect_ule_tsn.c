#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// for shared memory ipc
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <stdio.h> 

// for sysrepo
#include <sysrepo.h>
#include <sysrepo/plugins.h>
#include <sysrepo/values.h>

/* session of our plugin, can be used until cleanup is called */
sr_session_ctx_t *sess;
/* thread ID of the reading (thread) */
volatile pthread_t reading_tid;
/* structure holding all the subscriptions */
sr_subscription_ctx_t *subscription;
/* Identifier for shared memory */
int shmid;
/* Structure for the Data */
struct cuc_t {
    int a;
    int b;
} cuc;
/* Pointer for shared memory */
struct cuc_t *ptr;

static int request_cb(const char *xpath, sr_val_t **values, size_t *values_cnt, uint64_t request_id, const char *original_xpath, void *private_ctx);
static int change_cb(sr_session_ctx_t *session, const char *module_name, sr_notif_event_t event, void *private_ctx);
static void *reading(void *arg);
void sr_print_type(sr_type_t type);

/*
    @brief perform all initialization tasks
    Read and apply the current startup configuration
    opening subscriptions to data modules of interest
    @param[in] *session Session context
    @return error code (SR_ERR_OK on success)
*/
int sr_plugin_init_cb(sr_session_ctx_t *session, void **private_ctx)
{
    printf("dect ule in initialization\n");
    // initialize return code
    int rc = -1;
    /* session of our plugin, can be used until cleanup is called */
    sess = session;

    // Shared Memory:
    // ftok to generate unique key 
    shmid = -1;
    key_t key = ftok("shmfile",65); 
    SRP_LOG_INF_MSG("key ok"); 
    // shmget returns an identifier in shmid
    //shmid = shmget(key,1024,0666|IPC_CREAT); 
    shmid = shmget(key, 1024, IPC_CREAT|IPC_EXCL|0666);
    printf("shmid:%d", shmid);
    SRP_LOG_INF_MSG("shmget ok");

    // Threading
    // create the reading threat once shared memory is open
    rc = pthread_create((pthread_t *)&reading_tid, NULL, reading, NULL);
    if (rc != 0) {
        return SR_ERR_INIT_FAILED;
    }

    // Subscriptions
    /* subscribe for module changes - also causes startup data to be copied into running and enabling the module */
    rc = sr_module_change_subscribe(session, "turing-machine", change_cb, NULL, 0,
            SR_SUBSCR_EV_ENABLED | SR_SUBSCR_APPLY_ONLY, &subscription);
    if (rc != SR_ERR_OK) {
        return SR_ERR_INIT_FAILED;
    }
    /* subscribe as state data provider for the cuc state data */
    rc = sr_dp_get_items_subscribe(session, "/turing-machine:turing-machine", request_cb, NULL, SR_SUBSCR_CTX_REUSE, &subscription);
    if (rc != SR_ERR_OK) {
        return SR_ERR_INIT_FAILED;
    }
    SRP_LOG_INF_MSG("Subscriptions OK");

    return SR_ERR_OK;
}

/*
    @brief Thread for Input Read from shared memory
*/
static void *reading(void *arg)
{
    SRP_LOG_INF_MSG("In the Thread.");
    while (reading_tid) {
        /* buffer for shared memory */
        // struct cuc_t *ptr;
        // shmat to attach to shared memory 
        ptr = (struct cuc_t*) shmat(shmid,(void*)0,0);
        cuc.a = ptr->a;
        cuc.b = ptr->b;
        printf("Data read from memory: %d, %d\n", cuc.a, cuc.b); 
        fflush(NULL);
        sleep(5);
    }
    return NULL;
}

/*
    @brief Callback function when state data is requested
    @return error code (SR_ERR_OK on success)
*/
static int request_cb(const char *xpath, sr_val_t **values, size_t *values_cnt, uint64_t request_id, const char *original_xpath, void *private_ctx)
{
    SRP_LOG_DBG_MSG("In Callback for Request.");
    sr_val_t *vals = NULL;
    int rc = SR_ERR_OK;
   
    rc = sr_new_values(1, &vals);
    if (SR_ERR_OK != rc) {
        return rc;
    }

    // check for requested xpath
    if (strstr(xpath, "state") != NULL)
    {
        vals[0].type = SR_UINT16_T;
        vals[0].data.uint16_val = 1;
        rc = sr_val_set_xpath(&vals[0], "/turing-machine:turing-machine/state");  
        *values = vals;  
        *values_cnt = 1;
        SRP_LOG_INF_MSG("state changed"); 
        return SR_ERR_OK;   
    }
    else if (strstr(xpath, "head-position") != NULL)
    {
        vals[0].type = SR_INT64_T;
        vals[0].data.int64_val = 0;
        rc = sr_val_set_xpath(&vals[0], "/turing-machine:turing-machine/head-position");  
        SRP_LOG_INF_MSG("head-position changed"); 
        *values = vals;  
        *values_cnt = 1;
        return SR_ERR_OK;          
    }
    else
    {
        SRP_LOG_DBG_MSG("No hit for xpath");
    }

    sr_free_val(vals);
    *values = NULL; 
    *values_cnt = 0;

    return SR_ERR_OK;
}

/*
    @brief callback when something is changed (also at startup)
    @return error code (SR_ERR_OK on success)
*/
static int change_cb(sr_session_ctx_t *session, const char *module_name, sr_notif_event_t event, void *private_ctx)
{
    SRP_LOG_INF_MSG("In modify callback.");
    sr_val_t *vals = NULL;
    int rc = SR_ERR_OK;
    size_t count = 0;

    // Read values
    rc = sr_get_items(session, "/turing-machine:turing-machine/transition-function/delta[label='go home']//*", &vals, &count);
    if (SR_ERR_OK != rc) {
        SRP_LOG_ERR("Error by sr_get_items: %s", sr_strerror(rc));
        return SR_ERR_DATA_MISSING;
    }
    for (size_t i = 0; i < count; i++){
        //sr_print_val(&vals[i]);
        //sr_print_type(vals[i].type);
        //printf("\n");
    }
    sr_free_values(vals, count);

    return SR_ERR_OK;
}

/*
    @brief Cleanup all resources allocated in sr_plugin_init_cb
    Close all subscriptions
*/
void sr_plugin_cleanup_cb(sr_session_ctx_t *session, void *private_ctx) 
{
    // detach from shared memory  
    shmdt(ptr); 

    // destroy the shared memory 
    shmctl(shmid,IPC_RMID,NULL); 

    // unsubscribe
    sr_unsubscribe(session, subscription);
}

/*
    @brief Optional health check
    @return error code, SR_ERR_OK when success
*/
int sr_plugin_health_check_cb(sr_session_ctx_t *session, void **private_ctx)
{
    return SR_ERR_OK;
}

/*
    TODO: notification method to indicate changes in shared memory
*/

/*
    @brief convert enum to appropriate string to describe data type
    @param[in] type that needs to be decoded
*/
void sr_print_type(sr_type_t type) {
    switch (type) {
        case SR_CONTAINER_T:
            printf("Container Type\n");
            break;
        case SR_INT8_T:
            printf("int Type 8 \n");
            break;
        case SR_INT16_T:
            printf("int Type 16\n");
            break;
        case SR_UINT16_T:
            printf("int Type u16\n");
            break;
        case SR_INT32_T:
            printf("int Type 32\n");
            break;
        case SR_INT64_T:
            printf("int Type 64\n");
            break;
        case SR_LIST_T:
            printf("List Type\n");
            break;
        case SR_ENUM_T:
            printf("Enum Type\n");
            break;
        
        case SR_STRING_T:
            printf("string type\n");
            break;
        default:
            printf("Data Type enum: %d\n", type);
    }
}