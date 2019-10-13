#ifndef SYSREPO
#define SYSREPO
#include <sysrepo.h>
#include <sysrepo/plugins.h>
#include <sysrepo/values.h>
#include "sysrepo/xpath.h"
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
        // for (auto it = shmModule.first->talkersList.begin(); it != (shmModule.first->talkersList.end()); it++)
        //     it->printData();
    }
    else
        cout << "Does not work!\n";

    // initialize return code
    int rc = -1;
    /* session of our plugin, can be used until cleanup is called */
    sess = session;



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

    /* subscribe as state data provider for the cuc state data from talkers*/
    rc = sr_dp_get_items_subscribe(session, "/tsn-cuc-dect:talkers-list", request_talkers, NULL, SR_SUBSCR_CTX_REUSE, &subscription);
    if (rc != SR_ERR_OK) {
        return SR_ERR_INIT_FAILED;
    }
    /* subscribe as state data provider for the cuc state data from listeners*/
    rc = sr_dp_get_items_subscribe(session, "/tsn-cuc-dect:listeners-list", request_listeners, NULL, SR_SUBSCR_CTX_REUSE, &subscription);
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
    std::string path;
    path.assign(xpath);

    // Create shared memory
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_read_only, "SYSREPO_SHM");

    // An allocator convertible to any allocator<T, segment_manager_t> type
    void_allocator alloc(segment.get_segment_manager());

    // Find the module in the managed shared memory
    std::pair<module_t*, std::size_t> module_shm = segment.find<module_t>("dect_cuc_tsn");

    // if all requested:
    if (path.compare("/tsn-cuc-dect:devices-list") == 0)
    {
        SRP_LOG_INF_MSG("Original path!");
        // allocate the values
        sr_val_t *devices;
        int counter = (module_shm.first->devicesList.size() * 2);
        rc = sr_new_values(counter, &devices);
            if (SR_ERR_OK != rc) {
        return rc;
        }

        // give the values from the shm to the sysrepo
        for (auto it = module_shm.first->devicesList.begin(); it != module_shm.first->devicesList.end(); it++)
        {
            int i = (std::distance(module_shm.first->devicesList.begin(), it)) * 2;
            // get the id for the key
            int id = it->getId();
            // set the types: 1. name, 2. pmid
            devices[i].type = SR_STRING_T;
            devices[i+1].type = SR_STRING_T;
            // set the data
            sr_val_set_str_data(&devices[i], SR_STRING_T, it->getName().c_str());
            sr_val_set_str_data(&devices[i+1], SR_STRING_T, it->getPmid());
            // set the xpaths
            sr_val_build_xpath(&devices[i], "/tsn-cuc-dect:devices-list/device[id='%i']/name", id);
            sr_val_build_xpath(&devices[i+1], "/tsn-cuc-dect:devices-list/device[id='%i']/pmid", id);
        }
        // sr_xpath_key_value(xpath, )
        // if (sr_xpath_node_name_eq(xpath, )) {

        // set the param[out] of the callback
        *values = devices;
        *values_cnt = counter;
        return SR_ERR_OK;
    }
    else
    {
        // for path /tsn-cuc-dect:devices-list/device with filter!
        // if (path.compare("/tsn-cuc-dect:devices-list/device[id='%i']/") == 0)
        // SRP_LOG_INF_MSG("Different path needed!");
        // cout << "the path: " << xpath << "\n";
        // cout.flush();
        return SR_ERR_OK;
    }
    
}

/*
    @brief Callback function when state data from devices is requested
    @return error code (SR_ERR_OK on success)
*/
static int request_talkers(const char *xpath, sr_val_t **values, size_t *values_cnt, uint64_t request_id, const char *original_xpath, void *private_ctx)
{
    SRP_LOG_INF_MSG("In Callback for Talkers.");
    int rc = SR_ERR_OK;

    // Create shared memory
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_read_only, "SYSREPO_SHM");

    // An allocator convertible to any allocator<T, segment_manager_t> type
    void_allocator alloc(segment.get_segment_manager());

    // Find the module in the managed shared memory
    std::pair<module_t*, std::size_t> module_shm = segment.find<module_t>("dect_cuc_tsn");

    // print all talker data
    // for (auto it = module_shm.first->talkersList.begin(); it != (--module_shm.first->talkersList.end()); it++)
    //     it->printData();

    // allocate the values
    sr_val_t *talkers;
    int counter = 200;
    rc = sr_new_values(counter, &talkers);
        if (SR_ERR_OK != rc) {
    return rc;
    }

    // keep track of the position
    int i = 0;
    // give the values from the shm to the sysrepo
    for (auto it = module_shm.first->talkersList.begin(); it != module_shm.first->talkersList.end(); it++)
    {
        cout << "in first loop\n";
        cout.flush();
        // get the id for the key
        int talker_id = it->getId();
        talkers[i].type = SR_UINT8_T;
        talkers[i].data.uint8_val = it->stream_rank.rank;
        sr_val_build_xpath(&talkers[i], "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/stream-rank/rank", talker_id);
        i++;
        // end-station-interfaces
        for (auto it2 = it->end_station_interface_list.begin(); it2 != it->end_station_interface_list.end(); it2++)
        {
            cout << "in second loop for end station interfaces\n";
            talkers[i].type = SR_STRING_T;
            sr_val_set_str_data(&talkers[i], SR_STRING_T, it2->mac_address.c_str());
            sr_val_build_xpath(&talkers[i], "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/end-station-interfaces[mac-address interface-name='%s %s']/mac-address", 
                talker_id, it2->mac_address.c_str(), it2->interface_name.c_str());
                i++;
            talkers[i].type = SR_STRING_T;
            sr_val_set_str_data(&talkers[i], SR_STRING_T, it2->interface_name.c_str());
            sr_val_build_xpath(&talkers[i], "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/end-station-interfaces[mac-address interface-name='%s %s']/interface-name", 
                talker_id, it2->mac_address.c_str(), it2->interface_name.c_str());
                i++;
        }
        // data-frame-specification
        for (auto it2 = it->data_frame_specification_list.begin(); it2 != it->data_frame_specification_list.end(); it2++)
        {
            cout << "in third loop for data frame specification\n";
            int index;
            index = it2->index;
            // choice
            cout << "choice is: " << it2->choice.field << "\n";
            cout.flush();
            switch (it2->choice.field)
            {
                case MAC:
                {
                    // MAC and VLAn do not get printed!
                    cout << "choice is: " << it2->choice.field << "\n";
                    cout.flush();
                    talkers[i].type = SR_STRING_T;
                    // sr_val_set_str_data(&talkers[i], SR_STRING_T, it2->choice.destination_mac_address->c_str());
                    sr_val_set_str_data(&talkers[i], SR_STRING_T, it2->choice.str1.c_str());
                    sr_val_build_xpath(&talkers[i], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/ieee802-mac-addresses/destination-mac-address", 
                        talker_id, index);
                    i++;
                    talkers[i].type = SR_STRING_T;
                    // sr_val_set_str_data(&talkers[i], SR_STRING_T, it2->choice.source_mac_address->c_str());
                    sr_val_set_str_data(&talkers[i], SR_STRING_T, it2->choice.str2.c_str());
                    sr_val_build_xpath(&talkers[i], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/ieee802-mac-addresses/source-mac-address", 
                        talker_id, index);
                    i++;
                    break;
                }
                case VLAN:
                {
                    talkers[i].type = SR_UINT8_T;
                    talkers[i].data.uint8_val = *it2->choice.pcp;
                    sr_val_build_xpath(&talkers[i], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/ieee802-vlan-tag/priority-code-point", 
                        talker_id, index);
                    i++;
                    talkers[i].type = SR_UINT8_T;
                    talkers[i].data.uint8_val = *it2->choice.vlan_id;
                    sr_val_build_xpath(&talkers[i], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/ieee802-vlan-tag/vlan-id", 
                        talker_id, index);
                    i++;
                    break;
                }
                default:
                {
                    std::string tuple;
                    if (it2->choice.field == IPV6)
                        tuple.assign("ipv6");
                    else
                        tuple.assign("ipv4");
                    talkers[i].type = SR_STRING_T;
                    sr_val_set_str_data(&talkers[i], SR_STRING_T, it2->choice.str1.c_str());
                    sr_val_build_xpath(&talkers[i], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/%s-tuple/source-ip-address", 
                        talker_id, index, tuple.c_str());
                    i++;
                    talkers[i].type = SR_STRING_T;
                    sr_val_set_str_data(&talkers[i], SR_STRING_T, it2->choice.str2.c_str());
                    sr_val_build_xpath(&talkers[i], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/%s-tuple/destination-ip-address", 
                        talker_id, index, tuple.c_str());
                    i++;
                    talkers[i].type = SR_UINT8_T;
                    talkers[i].data.uint8_val = *it2->choice.dscp;
                    sr_val_build_xpath(&talkers[i], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/%s-tuple/dscp", 
                        talker_id, index, tuple.c_str());
                    i++;
                    talkers[i].type = SR_UINT16_T;
                    talkers[i].data.uint16_val = *it2->choice.protocol;
                    sr_val_build_xpath(&talkers[i], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/%s-tuple/protocol", 
                        talker_id, index, tuple.c_str());
                    i++;
                    talkers[i].type = SR_UINT16_T;
                    talkers[i].data.uint16_val = *it2->choice.source_port;
                    sr_val_build_xpath(&talkers[i], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/%s-tuple/source-port", 
                        talker_id, index, tuple.c_str());
                    i++;
                    talkers[i].type = SR_UINT16_T;
                    talkers[i].data.uint16_val = *it2->choice.destination_port;
                    sr_val_build_xpath(&talkers[i], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/%s-tuple/destination-port", 
                        talker_id, index, tuple.c_str());
                    i++;
                }
            }
        }
        // traffic specification
        talkers[i].type = SR_UINT32_T;
        talkers[i].data.uint32_val = it->traffic_specification.interval.numerator;
        sr_val_build_xpath(&talkers[i], 
            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/traffic-specification/interval/numerator", talker_id);
        i++;
        talkers[i].type = SR_UINT32_T;
        talkers[i].data.uint32_val = it->traffic_specification.interval.denominator;
        sr_val_build_xpath(&talkers[i], 
            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/traffic-specification/interval/denominator", talker_id);
        i++;
        talkers[i].type = SR_UINT16_T;
        talkers[i].data.uint16_val = it->traffic_specification.max_frames_per_interval;
        sr_val_build_xpath(&talkers[i], 
            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/traffic-specification/max-frames-per-interval", talker_id);
        i++;
        talkers[i].type = SR_UINT16_T;
        talkers[i].data.uint16_val = it->traffic_specification.max_frame_size;
        sr_val_build_xpath(&talkers[i], 
            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/traffic-specification/max-frame-size", talker_id);
        i++;
        talkers[i].type = SR_UINT8_T;
        talkers[i].data.uint8_val = it->traffic_specification.transmission_selection;
        sr_val_build_xpath(&talkers[i], 
            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/traffic-specification/transmission-selection", talker_id);
        i++;
        talkers[i].type = SR_UINT32_T;
        talkers[i].data.uint32_val = it->traffic_specification.time_aware.earliest_transmit_offset;
        sr_val_build_xpath(&talkers[i], 
            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/traffic-specification/time-aware/earliest-transmit-offset", talker_id);
        i++;
        talkers[i].type = SR_UINT32_T;
        talkers[i].data.uint32_val = it->traffic_specification.time_aware.latest_transmit_offset;
        sr_val_build_xpath(&talkers[i], 
            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/traffic-specification/time-aware/latest-transmit-offset", talker_id);
        i++;
        talkers[i].type = SR_UINT32_T;
        talkers[i].data.uint32_val = it->traffic_specification.time_aware.jitter;
        sr_val_build_xpath(&talkers[i], 
            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/traffic-specification/time-aware/jitter", talker_id);
        i++;
        // user to network requirements
        talkers[i].type = SR_UINT8_T;
        talkers[i].data.uint8_val = it->user_to_network_requirements.num_seamless_trees;
        sr_val_build_xpath(&talkers[i], 
            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/user-to-network-requirements/num-seamless-trees", talker_id);
        i++;
        talkers[i].type = SR_UINT32_T;
        talkers[i].data.uint32_val = it->user_to_network_requirements.max_latency;
        sr_val_build_xpath(&talkers[i], 
            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/user-to-network-requirements/max-latency", talker_id);
        i++;
        // interface capabilities
        talkers[i].type = SR_BOOL_T;
        talkers[i].data.bool_val = it->interface_capabilities.vlan_tag_capable;
        sr_val_build_xpath(&talkers[i], 
            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/interface-capabilities", talker_id);
        i++;
        for (auto it2 = it->interface_capabilities.cb_stream_iden_type_list.begin(); it2 != it->interface_capabilities.cb_stream_iden_type_list.end();
            it2++)
        {
            cout << "in forth loop for cb stream type\n";
            talkers[i].type = SR_UINT32_T;
            talkers[i].data.uint32_val = *it2;
            sr_val_build_xpath(&talkers[i], 
                "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/interface-capabilities/cb-stream-iden-type-list", talker_id);
            i++;
        }
        for (auto it2 = it->interface_capabilities.cb_sequence_type_list.begin(); it2 != it->interface_capabilities.cb_sequence_type_list.end();
            it2++)
        {
            cout << "in fifth loop for cb sequence type\n";
            talkers[i].type = SR_UINT32_T;
            talkers[i].data.uint32_val = *it2;
            sr_val_build_xpath(&talkers[i], 
                "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/interface-capabilities/cb-sequence-type-list", talker_id);
            i++;
        }
    }
    cout << "current i: " << i << "\n";
    cout.flush();
    for (auto it = 0; it != i; it++)
        sr_print_val(&talkers[it]);
    // set the param[out] of the callback
    *values = talkers;
    *values_cnt = i - 1;
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