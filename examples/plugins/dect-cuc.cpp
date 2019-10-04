

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

    boost::interprocess::shared_memory_object::remove("MySharedMemory");

    //Create shared memory
    boost::interprocess::managed_shared_memory segment(boost::interprocess::create_only,"MySharedMemory", 65536);

    //An allocator convertible to any allocator<T, segment_manager_t> type
    void_allocator alloc_inst (segment.get_segment_manager());

    void_allocator alloc(segment.get_segment_manager());
    end_station_t_shm* asd = segment.construct<end_station_t_shm>("testing")(2, alloc);
    std::pair<end_station_t_shm*, std::size_t> wtf = segment.find<end_station_t_shm>("testing");

    if(wtf.first)
    {   
        end_station_interface_t test;
        test.interface_name = "test1";
        test.mac_address = "test1mac";
        end_station_interface_t test2;
        test2.interface_name = "test2interfacename";
        test2.mac_address = "test2macaddress";
        wtf.first->end_station_interface_list.push_front(test);
        wtf.first->end_station_interface_list.push_front(test2);
        (wtf.first)->interface_capabilities.cb_stream_iden_type_list.push_back(1);
        (wtf.first)->interface_capabilities.cb_stream_iden_type_list.push_back(2);
        (wtf.first)->interface_capabilities.cb_stream_iden_type_list.push_back(3);
        (wtf.first)->interface_capabilities.cb_stream_iden_type_list.push_back(4);
        (wtf.first)->interface_capabilities.cb_stream_iden_type_list.push_back(5);
        (wtf.first)->interface_capabilities.cb_sequence_type_list.push_back(6);
        (wtf.first)->interface_capabilities.cb_sequence_type_list.push_back(7);
        (wtf.first)->interface_capabilities.cb_sequence_type_list.push_back(8);
        (wtf.first)->interface_capabilities.cb_sequence_type_list.push_back(9);
        (wtf.first)->interface_capabilities.cb_sequence_type_list.push_back(10);
        auto it = wtf.first->end_station_interface_list.begin();
            cout << wtf.first->end_station_interface_list.size() << "is the size\n";
            cout << (*it).interface_name << " and " << (*it).mac_address << "\n";
            it++;
            cout << (*it).interface_name << " and " << (*it).mac_address << "\n";
    }
    else
        cout << "NOT FOUND!\n";

    // traffic_specification_t ts;
    // ts.interval.denominator = 1;
    // ts.interval.numerator = 2;
    // ts.max_frame_size = 3;
    // ts.max_frames_per_interval = 4;
    // ts.time_aware.earliest_transmit_offset = 5;
    // ts.time_aware.jitter = 6;
    // ts.time_aware.latest_transmit_offset = 7;
    // ts.transmission_selection = 123;
    // user_to_network_requirements_t utnr;
    // utnr.max_latency = 8;
    // utnr.num_seamless_trees = 9;

    talker_t_shm* asd2 = segment.construct<talker_t_shm>("testing2")(2, alloc);

        // talker_t_shm(int id, int rank, traffic_specification_t traffic_specification,
        // user_to_network_requirements_t user_to_network_requirements, 
        // interface_capabilities_t interface_capabilities, const void_allocator &alloc);

    std::pair<talker_t_shm*, std::size_t> wtf2 = segment.find<talker_t_shm>("testing2");

    if(wtf2.first)
    {   
        end_station_interface_t test;
        test.interface_name = "test1";
        test.mac_address = "test1mac";
        end_station_interface_t test2;
        test2.interface_name = "test2interfacename";
        test2.mac_address = "test2macaddress";
        wtf2.first->end_station_interface_list.push_front(test);
        wtf2.first->end_station_interface_list.push_front(test2);
        auto it = wtf2.first->end_station_interface_list.begin();
            cout << wtf2.first->end_station_interface_list.size() << "is the size\n";
            cout << (*it).interface_name << " and " << (*it).mac_address << "\n";
            it++;
            cout << (*it).interface_name << " and " << (*it).mac_address << "\n";
        choice_t_shm c1;
        c1.str1 = "test";
        c1.str2 = "test2";
        c1.val1 = 1;
        c1.val2 = 2;
        c1.val3 = 3;
        c1.val4 = 4;  
        c1.field = 1;      
        data_frame_specification_t_shm dfs1(c1);
        choice_t_shm c2;
        c2.str1 = "testing";
        c2.str2 = "testing2";
        c2.val1 = 10;
        c2.val2 = 20;
        c2.val3 = 30;
        c2.val4 = 40;   
        c2.field = 2;     
        data_frame_specification_t_shm dfs2(c2);
        wtf2.first->data_frame_specification_list.push_back(dfs1);
        wtf2.first->data_frame_specification_list.push_back(dfs2);
        wtf2.first->interface_capabilities.cb_sequence_type_list.push_back(1);
        wtf2.first->interface_capabilities.cb_sequence_type_list.push_back(2);
        wtf2.first->interface_capabilities.cb_sequence_type_list.push_back(3);
        wtf2.first->interface_capabilities.cb_sequence_type_list.push_back(4);
        wtf2.first->interface_capabilities.cb_sequence_type_list.push_back(5);
        wtf2.first->interface_capabilities.cb_stream_iden_type_list.push_back(7);
        wtf2.first->interface_capabilities.cb_stream_iden_type_list.push_back(9);
        wtf2.first->interface_capabilities.cb_stream_iden_type_list.push_back(10);
    }
    else
        cout << "NOT FOUND!\n";

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
    boost::interprocess::managed_shared_memory::segment_manager *mgr = managed_shm.get_segment_manager();
    SRP_LOG_DBG_MSG("Shared Memory available"); 

    List<device_t> *list = managed_shm.construct<List<device_t>>("devicelist")(mgr);

    // List<device_t> *devicesList = managed_shm.construct<List<device_t>>("devicesList")(mgr);
    // List<talker_t> *talkersList = managed_shm.construct<List<talker_t>>("talkersList")(mgr);
    // module_t *module = managed_shm.construct<module_t>("module")(devicesList, talkersList);
    // module_t *module = managed_shm.construct<module_t>("module")(managed_shm, mgr);
    talker_t *talker;

    device_t test("device1", 1);
    device_t test2("device2", 2);
    device_t test3("device3", 3);
    device_t test4("device4", 4);

    // std::pair<module_t*, std::size_t> p = managed_shm.find<module_t>("module");
    std::pair<List<device_t>*, std::size_t> p = managed_shm.find<List<device_t>>("devicelist");

    // *(p.first)->devicesList = *devicesList;
    // module->devicesList = devicesList;

    if (p.first)
    {
        (*(p.first)).push_back(test);
        (*(p.first)).push_back(test2);
        (*(p.first)).push_back(test3);
        (*(p.first)).push_back(test4);
        cout << "Devices in List: " << (*(p.first)).size() << "\n";
        // (*(p.first)).test = 5;
        // cout << "address of module " << &(*(p.first)) << "\n";
        // cout << "address of devices list" << &(*(p.first)->devicesList) << "\n";
        // cout << "test " << (*(p.first)).test << "\n";
        // module->addDevice(test);
        // module->addDevice(test2);
        // module->addDevice(test3);
        // module->addDevice(test4);
        // cout << "size of lists: " << ((*(p.first)->devicesList).size()) << " and " << module->devicesList->size() << "\n";
    }


    // std::cout << "here comes the segmentation fault!\n";

    // List<end_station_interface_t> *end_station_interface_list = managed_shm.construct<List<end_station_interface_t>>("es1")();
    // List<data_frame_specification_t> *data_frame_specification_list = managed_shm.construct<List<data_frame_specification_t>>("df1")();    
    // *talker = talker_t(data_frame_specification_list, end_station_interface_list);
    // std::cout << "or not?\n";
    // talker->id = 0;
    // module->addTalker(*talker);
    // std::pair<List<device_t>*, std::size_t> p = managed_shm.find<List<device_t>>("devicelist");


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
        // ptr = (struct device_t*) shmat(shmid,(void*)0,0);
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
    // shmdt(ptr); 

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