#include <boost/interprocess/managed_shared_memory.hpp>
// demangle test
// #include <boost/core/demangle.hpp>

#ifndef SYSREPO
#define SYSREPO
#include <sysrepo.h>
#include <sysrepo/plugins.h>
#include <sysrepo/values.h>
#endif

#ifndef TSN_CUC_DECT
#define TSN_CUC_DECT
#include "tsn-cuc-dect.hpp"
#include "tsn-cuc-dect.cpp"
#endif

/* session of our plugin, can be used until cleanup is called */
sr_session_ctx_t *sess;
/* thread ID of the reading (thread) */
volatile pthread_t reading_tid;
/* structure holding all the subscriptions */
sr_subscription_ctx_t *subscription;
/* Identifier for shared memory */
int shmid;
/* Pointer for shared memory */
struct device_t *ptr;

// make sure there is no name-mangeling when linking
extern "C" {
    int sr_plugin_init_cb(sr_session_ctx_t *session, void **private_ctx);
    void sr_plugin_cleanup_cb(sr_session_ctx_t *session, void *private_ctx);
};

static int request_devices(const char *xpath, sr_val_t **values, size_t *values_cnt, uint64_t request_id, const char *original_xpath, void *private_ctx);
static int request_talkers(const char *xpath, sr_val_t **values, size_t *values_cnt, uint64_t request_id, const char *original_xpath, void *private_ctx);
static int request_listeners(const char *xpath, sr_val_t **values, size_t *values_cnt, uint64_t request_id, const char *original_xpath, void *private_ctx);
static int change_cb(sr_session_ctx_t *session, const char *module_name, sr_notif_event_t event, void *private_ctx);
static void *reading(void *arg);
// void sr_print_type(sr_type_t type);

/*
    @brief perform all initialization tasks
    Read and apply the current startup configuration
    opening subscriptions to data modules of interest
    @param[in] *session Session context
    @return error code (SR_ERR_OK on success)
*/
int sr_plugin_init_cb(sr_session_ctx_t *session, void **private_ctx)
{
    SRP_LOG_INF_MSG("dect ule in initialization");
    // initialize return code
    int rc = -1;
    /* session of our plugin, can be used until cleanup is called */
    sess = session;

    /*
        Shared Memory: create the shared memory object
    */
    boost::interprocess::shared_memory_object::remove("cuc-dect");

    // create the shared memory
    boost::interprocess::managed_shared_memory managed_shm{boost::interprocess::create_only, "cuc-dect", 1024};
    // module_t *module = managed_shm.construct<module_t>("tsn-cuc-dect-module")();
    device_t *i = managed_shm.construct<device_t>("device")("asd", 1234);
    std::cout << (*i).getName() << "\n";
    std::pair<device_t*, std::size_t> p = managed_shm.find_no_lock<device_t>("device");
    if (p.first)
    {
        // (p.first)->name2 = "test";
        // std::cout << (p.first)->getName() << '\n';
        std::cout << &((p.first)->id) << "\n";
    }
    while(true)
    {
        // SRP_LOG_INF_MSG("Reading from shared memory");
        // std::pair<module_t*, std::size_t> p = managed_shm.find<module_t>("tsn-cuc-dect-module");
        // if (p.first)
        // {
        //     SRP_LOG_INF_MSG("found!");
        //     std::cout << "The id: " << (*(p.first->devicesList.begin())).getPmid() << "\n";
        //     break;
        // }
    }
    
    // std::list<int> list;
    // list.push_back(9);
    // demangle?

    // device_t device("test", 12345);

    // std::pair<module_t*, std::size_t> p = managed_shm.find<module_t>("tsn-cuc-dect-module");

    // module->devicesList.push_back(device);
    // int *i = managed_shm.construct<int>("Integer")(22);
    SRP_LOG_DBG_MSG("Shared Memory available"); 


    // Threading
    // create the reading threat once shared memory is open
    rc = pthread_create((pthread_t *)&reading_tid, NULL, reading, NULL);
    if (rc != 0) {
        return SR_ERR_INIT_FAILED;
    }

    // // Subscriptions
    // /* subscribe for module changes - also causes startup data to be copied into running and enabling the module */
    // rc = sr_module_change_subscribe(session, "dect-cuc", change_cb, NULL, 0,SR_SUBSCR_EV_ENABLED | SR_SUBSCR_APPLY_ONLY, &subscription);
    // if (rc != SR_ERR_OK) {
    //     return SR_ERR_INIT_FAILED;
    // }
    // /* subscribe as state data provider for the cuc state data from devices*/
    // rc = sr_dp_get_items_subscribe(session, "/dect-cuc:devices-list", request_devices, NULL, SR_SUBSCR_CTX_REUSE, &subscription);
    // if (rc != SR_ERR_OK) {
    //     return SR_ERR_INIT_FAILED;
    // }
    // /* subscribe as state data provider for the cuc state data from talkers*/
    // rc = sr_dp_get_items_subscribe(session, "/dect-cuc:talkers-list", request_talkers, NULL, SR_SUBSCR_CTX_REUSE, &subscription);
    // if (rc != SR_ERR_OK) {
    //     return SR_ERR_INIT_FAILED;
    // }
    // /* subscribe as state data provider for the cuc state data from listeners*/
    // rc = sr_dp_get_items_subscribe(session, "/dect-cuc:listeners-list", request_listeners, NULL, SR_SUBSCR_CTX_REUSE, &subscription);
    // if (rc != SR_ERR_OK) {
    //     return SR_ERR_INIT_FAILED;
    // }
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
        // shmat to attach to shared memory 
        ptr = (struct device_t*) shmat(shmid,(void*)0,0);
        //int id = ptr->id;
        //pmid_t pmid = ptr->pmid;
        //printf("Data read from memory: %d, %d\n", id, pmid); 
        fflush(NULL);
        sleep(5);
    }
    return NULL;
}

/*
    @brief Callback function when state data from devices is requested
    @return error code (SR_ERR_OK on success)
*/
static int request_devices(const char *xpath, sr_val_t **values, size_t *values_cnt, uint64_t request_id, const char *original_xpath, void *private_ctx)
{
    SRP_LOG_DBG_MSG("In Callback for Request.");
    sr_val_t *vals = NULL;
    int rc = SR_ERR_OK;
   
    rc = sr_new_values(1, &vals);
    if (SR_ERR_OK != rc) {
        return rc;
    }

    sr_free_val(vals);
    *values = NULL; 
    *values_cnt = 0;

    return SR_ERR_OK;
}

/*
    @brief Callback function when state data from devices is requested
    @return error code (SR_ERR_OK on success)
*/
static int request_talkers(const char *xpath, sr_val_t **values, size_t *values_cnt, uint64_t request_id, const char *original_xpath, void *private_ctx)
{
    SRP_LOG_DBG_MSG("In Callback for talkers.");
    sr_val_t *vals = NULL;
    int rc = SR_ERR_OK;
   
    rc = sr_new_values(1, &vals);
    if (SR_ERR_OK != rc) {
        return rc;
    }

    sr_free_val(vals);
    *values = NULL; 
    *values_cnt = 0;

    return SR_ERR_OK;
}

/*
    @brief Callback function when state data from devices is requested
    @return error code (SR_ERR_OK on success)
*/
static int request_listeners(const char *xpath, sr_val_t **values, size_t *values_cnt, uint64_t request_id, const char *original_xpath, void *private_ctx)
{
    SRP_LOG_DBG_MSG("In Callback for listeners.");
    sr_val_t *vals = NULL;
    int rc = SR_ERR_OK;
   
    rc = sr_new_values(1, &vals);
    if (SR_ERR_OK != rc) {
        return rc;
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
    //rc = sr_get_items(session, "/turing-machine:turing-machine/transition-function/delta[label='go home']//*", &vals, &count);
    rc = sr_get_items(session, "/dect-cuc:devices//*", &vals, &count);
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