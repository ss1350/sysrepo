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
/* Shared memory */
module_t* moduleptr;
boost::interprocess::managed_shared_memory *segptr;

module_t* testing;

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

    boost::interprocess::shared_memory_object::remove("SYSREPO_SHM");

    //Create shared memory
    boost::interprocess::managed_shared_memory segment(boost::interprocess::create_only,"SYSREPO_SHM", 65536);

    segptr = &segment;
    //An allocator convertible to any allocator<T, segment_manager_t> type
    void_allocator alloc(segment.get_segment_manager());

    // whole module:
    module_t* module = segment.construct<module_t>("dect_cuc_tsn")(alloc);

    std::pair<module_t*, std::size_t> shmModule = segment.find<module_t>("dect_cuc_tsn");

    if (shmModule.first)
    {
       fillData(shmModule.first, alloc);
    }
    else
        cout << "Does not work!\n";

    // initialize return code
    int rc = -1;
    /* session of our plugin, can be used until cleanup is called */
    sess = session;

    device_t test("device1", "11110000000000001111", alloc);
    device_t test2("device2", "11110000000000001110", alloc);
    device_t test3("device3", "11110100000000001111", alloc);
    device_t test4("device4", "11110000001000001111", alloc);

    shmModule.first->devicesList.push_back(test);
    shmModule.first->devicesList.push_back(test2);
    shmModule.first->devicesList.push_back(test3);
    shmModule.first->devicesList.push_back(test4);

    // Threading
    // create the reading threat once shared memory is open
    // rc = pthread_create((pthread_t *)&reading_tid, NULL, reading, NULL);
    // if (rc != 0) {
    //     return SR_ERR_INIT_FAILED;
    // }

    // Subscriptions
    /* subscribe for module changes - also causes startup data to be copied into running and enabling the module */
    rc = sr_module_change_subscribe(session, "tsn-cuc-dect", change_cb, NULL, 0, SR_SUBSCR_EV_ENABLED | SR_SUBSCR_APPLY_ONLY, &subscription);
    // rc = sr_module_change_subscribe(session, "tsn-cuc-dect", change_cb, NULL, 0, SR_SUBSCR_DEFAULT, &subscription);
    if (rc != SR_ERR_OK) {
        return SR_ERR_INIT_FAILED;
    }
    /* subscribe as state data provider for the cuc state data from devices*/
    rc = sr_dp_get_items_subscribe(session, "/tsn-cuc-dect:devices-list", request_devices, NULL, SR_SUBSCR_CTX_REUSE, &subscription);
    if (rc != SR_ERR_OK) {
        return SR_ERR_INIT_FAILED;
    }
    // /* subscribe as state data provider for the cuc state data from talkers*/
    // rc = sr_dp_get_items_subscribe(session, "/tsn-cuc-dect:talkers-list", request_talkers, NULL, SR_SUBSCR_CTX_REUSE, &subscription);
    // if (rc != SR_ERR_OK) {
    //     return SR_ERR_INIT_FAILED;
    // }
    // /* subscribe as state data provider for the cuc state data from listeners*/
    // rc = sr_dp_get_items_subscribe(session, "/tsn-cuc-dect:listeners-list", request_listeners, NULL, SR_SUBSCR_CTX_REUSE, &subscription);
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
    SRP_LOG_INF_MSG("In Callback for Devices.");
    int rc = SR_ERR_OK;

    // Create shared memory
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_read_only, "SYSREPO_SHM");

    // An allocator convertible to any allocator<T, segment_manager_t> type
    void_allocator alloc(segment.get_segment_manager());

    // Find the module in the managed shared memory
    std::pair<module_t*, std::size_t> module_shm = segment.find<module_t>("dect_cuc_tsn");

    cout << "does it work? " << module_shm.first->devicesList.begin()->getName() << "\n";
    cout.flush();
    // should this be done?
    segment.destroy_ptr(&module_shm);

    // SHM DOES NOT WORK?
    // std::pair<module_t*, std::size_t> shmModule = segptr->find<module_t>("dect_cuc_tsn");

    // if (shmModule.first)
    // does shm not work here?
    // cout << "device name: " << shmModule.first->devicesList.begin()->getName() << "\n";
    // cout << "device name: " << testing->devicesList.begin()->getName() << "\n";

    sr_val_t *device;
    int counter = 3;
    rc = sr_new_values(counter, &device);
        if (SR_ERR_OK != rc) {
    return rc;
    }
    // device_t deviceptr = *(moduleptr->devicesList.begin());
    // id
    device[0].type = SR_UINT8_T;
    // name
    device[1].type = SR_STRING_T;
    // pmid
    device[2].type = SR_STRING_T;

    // device[0].data.uint8_val = deviceptr.getId();
    // sr_val_set_str_data(&device[1], SR_STRING_T, deviceptr.getName().c_str());
    // device[2].data.uint8_val = deviceptr.getPmid();

    // test for stream
    // sr_val_t* stream;
    // sr_new_values(1, &stream);
    // stream[0].type = SR_UINT8_T;
    // stream[0].data.uint8_val = 5;
    // sr_val_set_xpath(&stream[0], "tsn-cuc-dect:streams-list/status-info/failure-code");

    device[0].data.uint8_val = 1;
    sr_val_set_str_data(&device[1], SR_STRING_T, "test");
    sr_val_set_str_data(&device[2], SR_STRING_T, "11110000111100000000");

    sr_val_build_xpath(&device[0], "/tsn-cuc-dect:devices-list[id='%i']/id", 1);
    sr_val_build_xpath(&device[1], "/tsn-cuc-dect:devices-list[id='%i']/name", 1);
    sr_val_build_xpath(&device[2], "/tsn-cuc-dect:devices-list[id='%i']/pmid", 1);

    *values = device;
    *values_cnt = counter;

    // rc = sr_set_item(sess, stream[0].xpath, &stream[0], SR_EDIT_DEFAULT);
    // rc = sr_set_item(sess, "tsn-cuc-dect:devices-list[id='0']/", NULL, SR_EDIT_DEFAULT);
    // rc = sr_set_item(sess, device[0].xpath, &device[0], SR_EDIT_DEFAULT);
    // rc = sr_set_item(sess, device[1].xpath, &device[1], SR_EDIT_DEFAULT);
    // rc = sr_set_item(sess, device[2].xpath, &device[2], SR_EDIT_DEFAULT);
    // sr_commit(sess);

    return SR_ERR_OK;
}

/*
    @brief Callback function when state data from devices is requested
    @return error code (SR_ERR_OK on success)
*/
static int request_talkers(const char *xpath, sr_val_t **values, size_t *values_cnt, uint64_t request_id, const char *original_xpath, void *private_ctx)
{
    SRP_LOG_INF_MSG("In Callback for talkers.");
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
    SRP_LOG_INF_MSG("In Callback for listeners.");
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
    @brief callback when something is changed (also at startup) This saves the stream info into the shared memory
    @return error code (SR_ERR_OK on success)
*/
static int change_cb(sr_session_ctx_t *session, const char *module_name, sr_notif_event_t event, void *private_ctx)
{
    int rc = SR_ERR_OK;

    // if (moduleptr)
    // {
    //     SRP_LOG_INF_MSG("SHM available");
    //     // does shm not work here?
    //     // cout << "device name: " << moduleptr->devicesList.begin()->getName() << "\n";
    //     SRP_LOG_INF_MSG("SHM accessible");

    //     sr_val_t *device;
    //     int counter = 3;
    //     sr_new_values(counter, &device);
    //         if (SR_ERR_OK != rc) {
    //     return rc;
    //     }

    //     SRP_LOG_INF_MSG("allocation okay");
    //     // device_t deviceptr = *(moduleptr->devicesList.begin());
    //     // id
    //     device[0].type = SR_UINT8_T;
    //     // name
    //     device[1].type = SR_STRING_T;
    //     // pmid
    //     device[2].type = SR_UINT8_T;

    //     device[0].data.uint8_val = 1;
    //     sr_val_set_str_data(&device[1], SR_STRING_T, "test");
    //     device[2].data.uint8_val = 123;

    //     sr_val_set_xpath(&device[0], "tsn-cuc-dect:devices-list[id='1']/id");
    //     sr_val_set_xpath(&device[1], "tsn-cuc-dect:devices-list[id='1']/name");
    //     sr_val_set_xpath(&device[2], "tsn-cuc-dect:devices-list[id='1']/pmid");        
    //     // sr_val_build_xpath(&device[0], "tsn-cuc-dect:devices-list[id='%i']/id", 1);
    //     // sr_val_build_xpath(&device[1], "tsn-cuc-dect:devices-list[id='%i']/name", 1);
    //     // sr_val_build_xpath(&device[2], "tsn-cuc-dect:devices-list[id='%i']/pmid", 1);

    //     *values = vals;
    //     *values_cnt = 2;
        // device[0].data.uint8_val = deviceptr.getId();
        // sr_val_set_str_data(&device[1], SR_STRING_T, deviceptr.getName().c_str());
        // device[2].data.uint8_val = deviceptr.getPmid();

        // test for stream
        // sr_val_t* stream;
        // sr_new_values(1, &stream);
        // stream[0].type = SR_UINT8_T;
        // stream[0].data.uint8_val = 5;
        // sr_val_set_xpath(&stream[0], "tsn-cuc-dect:streams-list/status-info/failure-code");



        // sr_print_val(&device[0]);
        // sr_print_val(&device[1]);
        // sr_print_val(&device[2]);

        // create

    //     SRP_LOG_INF_MSG("Trying to set items!");
    //     // rc = sr_set_item(sess, stream[0].xpath, &stream[0], SR_EDIT_DEFAULT);
    //     // rc = sr_set_item(sess, "tsn-cuc-dect:devices-list[id='0']/", NULL, SR_EDIT_DEFAULT);
    //     rc = sr_set_item(session, device[0].xpath, &device[0], SR_EDIT_DEFAULT);
    //     rc = sr_set_item(session, device[1].xpath, &device[1], SR_EDIT_DEFAULT);
    //     rc = sr_set_item(session, device[2].xpath, &device[2], SR_EDIT_DEFAULT);
    //     sr_commit(session);

    //     if (SR_ERR_OK != rc) {
    //         SRP_LOG_ERR("Error by sr_set_items: %s", sr_strerror(rc));
    //         return SR_ERR_DATA_MISSING;
    //     }
    // }
    return SR_ERR_OK;
}

/*
    @brief Cleanup all resources allocated in sr_plugin_init_cb
    Close all subscriptions
*/
void sr_plugin_cleanup_cb(sr_session_ctx_t *session, void *private_ctx) 
{
    // destroy the shared memory 
    boost::interprocess::shared_memory_object::remove("SYSREPO_SHM");

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