

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
    void_allocator alloc(segment.get_segment_manager());


    // whole module:

    module_t* module = segment.construct<module_t>("dect_cuc_tsn")(alloc);

    std::pair<module_t*, std::size_t> moduleptr = segment.find<module_t>("dect_cuc_tsn");

    if (moduleptr.first)
    {
        cout << "WORKS!\n";
        talker_t testtalker(1, alloc);
        // rank
        testtalker.stream_rank.rank = 0;
        // end station interface list
        end_station_interface_t esi(alloc);
        esi.interface_name.assign("eth0");
        esi.mac_address.assign("AA:AA:AA:AA:AA:AA"); 
        end_station_interface_t esi2(alloc);
        esi2.interface_name.assign("dect0");
        esi2.mac_address.assign("CC:CC:CC:CC:CC:CC");
        testtalker.end_station_interface_list.push_back(esi);
        testtalker.end_station_interface_list.push_back(esi2);
        // interface capabilities
        testtalker.interface_capabilities.cb_sequence_type_list.push_back(1234);
        testtalker.interface_capabilities.cb_sequence_type_list.push_back(5678);
        testtalker.interface_capabilities.cb_stream_iden_type_list.push_back(9012);
        testtalker.interface_capabilities.cb_stream_iden_type_list.push_back(3456);
        testtalker.interface_capabilities.vlan_tag_capable = true;
        // user to network requirements
        testtalker.user_to_network_requirements.max_latency = 5;
        testtalker.user_to_network_requirements.num_seamless_trees = 2;
        // traffic specification
        testtalker.traffic_specification.interval.numerator = 1;
        testtalker.traffic_specification.interval.denominator = 2;
        testtalker.traffic_specification.max_frame_size = 1024;
        testtalker.traffic_specification.max_frames_per_interval = 20;
        testtalker.traffic_specification.time_aware.earliest_transmit_offset = 1;
        testtalker.traffic_specification.time_aware.jitter = 5;
        testtalker.traffic_specification.time_aware.latest_transmit_offset = 10;
        testtalker.traffic_specification.transmission_selection = 1234;
        // data frame specifications
        choice_t c1(alloc);
        c1.field = VLAN;
        *c1.vlan_id = 80;
        *c1.pcp = 4;
        data_frame_specification_t dfs1(alloc);
        dfs1.choice = c1;
        dfs1.index = 0;
        testtalker.data_frame_specification_list.push_back(dfs1);
        choice_t c2(alloc);
        c2.field = MAC;
        c2.source_mac_address->assign("AA:AA:AA:AA:AA:AA");
        c2.destination_mac_address->assign("BB:BB:BB:BB:BB:BB");
        data_frame_specification_t dfs2(alloc);
        dfs2.choice = c2;
        dfs2.index = 1;
        testtalker.data_frame_specification_list.push_back(dfs2);
        moduleptr.first->talkersList.push_back(testtalker);
        // talker 2
        talker_t testtalker2(2, alloc);
        // rank
        testtalker2.stream_rank.rank = 1;
        // end station interface list
        esi.interface_name.assign("eth1");
        esi.mac_address.assign("11:11:11:11:11:11");
        esi2.interface_name.assign("dect1");
        esi2.mac_address.assign("22:22:22:22:22:22");
        testtalker2.end_station_interface_list.push_back(esi);
        testtalker2.end_station_interface_list.push_back(esi2);
        // interface capabilities
        testtalker2.interface_capabilities.cb_sequence_type_list.push_back(0001);
        testtalker2.interface_capabilities.cb_sequence_type_list.push_back(0002);
        testtalker2.interface_capabilities.cb_stream_iden_type_list.push_back(0003);
        testtalker2.interface_capabilities.cb_stream_iden_type_list.push_back(0004);
        testtalker2.interface_capabilities.vlan_tag_capable = false;
        // user to network requirements
        testtalker2.user_to_network_requirements.max_latency = 80;
        testtalker2.user_to_network_requirements.num_seamless_trees = 90;
        // traffic specification
        testtalker2.traffic_specification.interval.numerator = 10;
        testtalker2.traffic_specification.interval.denominator = 20;
        testtalker2.traffic_specification.max_frame_size = 10240;
        testtalker2.traffic_specification.max_frames_per_interval = 200;
        testtalker2.traffic_specification.time_aware.earliest_transmit_offset = 10;
        testtalker2.traffic_specification.time_aware.jitter = 50;
        testtalker2.traffic_specification.time_aware.latest_transmit_offset = 100;
        testtalker2.traffic_specification.transmission_selection = 12340;
        // data frame specifications
        choice_t c3(alloc);
        c3.field = IPV4;
        c3.ipv4_source_ip_address->assign("192.168.1.1");
        c3.ipv4_destination_ip_address->assign("127.0.0.1");
        *c3.dscp = 1234;
        *c3.protocol = 5678;
        *c3.source_port = 70;
        *c3.destination_port = 90;
        data_frame_specification_t dfs3(alloc);
        dfs3.choice = c3;
        dfs3.index = 0;
        testtalker2.data_frame_specification_list.push_back(dfs3);
        c2.field = IPV6;
        c2.ipv6_source_ip_address->assign("0001:0db8:85a3:0005:0000:8a2e:0370:7334");
        c2.ipv6_destination_ip_address->assign("1234:1234:1234:1234:0000:1234:1234:1234");
        *c2.dscp = 5678;
        *c2.protocol = 9012;
        *c2.source_port = 50;
        *c2.destination_port = 30;
        data_frame_specification_t dfs4(alloc);
        dfs4.choice = c2;
        dfs4.index = 1;
        testtalker2.data_frame_specification_list.push_back(dfs4);
        moduleptr.first->talkersList.push_back(testtalker2);
        // print the talkers
        // moduleptr.first->talkersList.begin()->printData();
        // (++moduleptr.first->talkersList.begin())->printData();
    }
    else
        cout << "Does not work!\n";

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