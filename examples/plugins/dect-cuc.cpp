#ifndef SYSREPO
#define SYSREPO
#include <sysrepo.h>
#include <sysrepo/plugins.h>
#include <sysrepo/values.h>
#include "sysrepo/xpath.h"
#endif

#ifndef IPC
#define IPC
// #include <boost/thread.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/list.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/vector.hpp>
template <typename T> using Alloc = boost::interprocess::allocator<T, boost::interprocess::managed_shared_memory::segment_manager>;
template <typename K> using List = boost::interprocess::list<K, Alloc<K>>;
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
    {
        cout << "Shared Memory could not be initialized!\n";
        cout.flush();
        return SR_ERR_INTERNAL;
    }

    // try {
    //     SRP_LOG_INF_MSG("Try the mutex");
    //     boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> lock(shmModule.first->mutex, boost::interprocess::try_to_lock);
    //     if(lock){
    //         cout << "locked the mutex!\n";
    //         cout.flush();
    //         std::string test;
    //         cin >> test;
    //     }
    // } catch (boost::interprocess::interprocess_exception &ex) {
    //     std::cout << ex.what() << std::endl;
    //     return 0;
    // }

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
    rc = sr_dp_get_items_subscribe(session, "/tsn-cuc-dect:talkers-list/talker", request_talkers, NULL, SR_SUBSCR_CTX_REUSE, &subscription);
    if (rc != SR_ERR_OK) {
        return SR_ERR_INIT_FAILED;
    }
    /* subscribe as state data provider for the cuc state data from listeners*/
    rc = sr_dp_get_items_subscribe(session, "/tsn-cuc-dect:listeners-list/listener", request_listeners, NULL, SR_SUBSCR_CTX_REUSE, &subscription);
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
    SRP_LOG_DBG_MSG("In Callback for Devices.");
    int rc = SR_ERR_OK;
    std::string path;
    path.assign(xpath);

    // Create shared memory
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_only, "SYSREPO_SHM");

    // An allocator convertible to any allocator<T, segment_manager_t> type
    void_allocator alloc(segment.get_segment_manager());

    // Find the module in the managed shared memory
    std::pair<module_t*, std::size_t> module_shm = segment.find<module_t>("dect_cuc_tsn");

    try {
        SRP_LOG_DBG_MSG("Getting the mutex.");
        boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> lock(module_shm.first->mutex, boost::interprocess::try_to_lock);
        if(lock)
        {
            // if all requested:
            // if (path.compare("/tsn-cuc-dect:devices-list") == 0)
            if (sr_xpath_node_name_eq(xpath, "device"))
            {
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
                *values = devices;
                *values_cnt = counter;
                return SR_ERR_OK;
            }
        }
        else
        {
            SRP_LOG_INF_MSG("Data locked. Try again later.");
            return SR_ERR_LOCKED;
        }
    } catch (boost::interprocess::interprocess_exception &ex) {
        std::cout << ex.what() << std::endl;
        return SR_ERR_INTERNAL;
    }
}

/*
    @brief Callback function when state data from talkers is requested
    @return error code (SR_ERR_OK on success)
*/
static int request_talkers(const char *xpath, sr_val_t **values, size_t *values_cnt, uint64_t request_id, const char *original_xpath, void *private_ctx)
{
    SRP_LOG_DBG_MSG("In Callback for Talkers.");
    int rc = SR_ERR_OK;

    // Create shared memory
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_only, "SYSREPO_SHM");

    // An allocator convertible to any allocator<T, segment_manager_t> type
    void_allocator alloc(segment.get_segment_manager());

    // Find the module in the managed shared memory
    std::pair<module_t*, std::size_t> module_shm = segment.find<module_t>("dect_cuc_tsn");

    // keep track of the position
    int i = 0;

    try {
        SRP_LOG_DBG_MSG("Getting the mutex.");
        boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> lock(module_shm.first->mutex, boost::interprocess::try_to_lock);
        if(lock)
        {
            // only when container requested and not a specific node
            if ((strcmp(original_xpath, "/tsn-cuc-dect:*//")))
            if (sr_xpath_node_name_eq(xpath, "talker"))
            {
                // allocate the values
                sr_val_t *talkers;
                int counter = 200;
                rc = sr_new_values(counter, &talkers);
                    if (SR_ERR_OK != rc) {
                return rc;
                }
                // give the values from the shm to the sysrepo
                for (auto it = module_shm.first->talkersList.begin(); it != module_shm.first->talkersList.end(); it++)
                {
                    // get the id for the key
                    int talker_id = it->getId();
                    for (auto it2 = it->data_frame_specification_list.begin(); it2 != it->data_frame_specification_list.end(); it2++)
                    {
                        int index;
                        index = it2->index;
                        switch (it2->choice.field)
                        {
                            case MAC:
                            {
                                talkers[i].type = SR_STRING_T;
                                sr_val_set_str_data(&talkers[i], SR_STRING_T, it2->choice.str1.c_str());
                                sr_val_build_xpath(&talkers[i], 
                                    "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/ieee802-mac-addresses/destination-mac-address", 
                                    talker_id, index);
                                i++;
                                talkers[i].type = SR_STRING_T;
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
                                talkers[i].data.uint8_val = it2->choice.val1;
                                sr_val_build_xpath(&talkers[i], 
                                    "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/ieee802-vlan-tag/priority-code-point", 
                                    talker_id, index);
                                i++;
                                talkers[i].type = SR_UINT16_T;
                                talkers[i].data.uint16_val = it2->choice.val2;
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
                                else if (it2->choice.field == IPV4)
                                    tuple.assign("ipv4");
                                else
                                    break;
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
                }
                *values = talkers;
                *values_cnt = i;
                return SR_ERR_OK;
            }

            // state of xpath
            sr_xpath_ctx_t state;
            // needs a non-const char*
            std::string xpathStr;
            xpathStr.assign(xpath);
            char *xpathNonConst = &xpathStr[0];
            // get the key of the data-frame-specification
            char* indexStr = sr_xpath_key_value(xpathNonConst, "data-frame-specification", "index", &state);
            int index;
            if (indexStr)
            {
                try{
                    index = stoi(indexStr);
                }
                catch (const std::invalid_argument& ia) {
                    std::cerr << "Invalid Input: " << ia.what() << '\n';
                    return SR_ERR_INVAL_ARG;
                }
            }

            // get the key of talker-id
            char* id = sr_xpath_key_value(xpathNonConst, "talker", "talker-id", &state);
            if (!(id))
            {
                SRP_LOG_DBG_MSG("No talker identification - skip");
                return SR_ERR_OK;
            }
            int talkerId;
            try{
                talkerId = stoi(id);
            }
            catch (const std::invalid_argument& ia) {
                std::cerr << "Invalid Input: " << ia.what() << '\n';
                return SR_ERR_INVAL_ARG;
            }

            if (sr_xpath_node_name_eq(xpath, "stream-rank"))
            {
                // allocate one value
                sr_val_t *data;
                rc = sr_new_values(1, &data);
                    if (SR_ERR_OK != rc) {
                return rc;
                }
                // search for index
                for (auto it = module_shm.first->talkersList.begin(); it != module_shm.first->talkersList.end(); it++)
                {
                    if (it->getId() != talkerId)
                        continue;
                    // set the value
                    data[0].type = SR_UINT8_T;
                    data[0].data.uint8_val = it->stream_rank.rank;
                    sr_val_build_xpath(&data[0], "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/stream-rank/rank", id);
                    *values = data;
                    *values_cnt = 1;
                    return SR_ERR_OK;
                }
            }
            if (sr_xpath_node_name_eq(xpath, "end-station-interfaces"))
            {
                for (auto it = module_shm.first->talkersList.begin(); it != module_shm.first->talkersList.end(); it++)
                {
                    if (it->getId() != talkerId)
                        continue;
                    // allocate the values
                    sr_val_t *data;
                    rc = sr_new_values(it->end_station_interface_list.size() * 2, &data);
                    if (SR_ERR_OK != rc) {
                        return rc;
                    }
                    int i = 0;
                    for (auto it2 = it->end_station_interface_list.begin(); it2 != it->end_station_interface_list.end(); it2++)
                    {
                        data[i].type = SR_STRING_T;
                        sr_val_set_str_data(&data[i], SR_STRING_T, it2->interface_name.c_str());
                        sr_val_build_xpath(&data[i], "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/end-station-interfaces[mac-address='%s'][interface-name='%s']/interface-name", 
                            id, it2->mac_address.c_str(), it2->interface_name.c_str());
                        i++;
                        data[i].type = SR_STRING_T;
                        sr_val_set_str_data(&data[i], SR_STRING_T, it2->mac_address.c_str());
                        sr_val_build_xpath(&data[i], "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/end-station-interfaces[mac-address='%s'][interface-name='%s']/mac-address", 
                            id, it2->mac_address.c_str(), it2->interface_name.c_str());
                        i++;
                    }
                    *values = data;
                    *values_cnt = i;
                    return SR_ERR_OK;
                }
            }
            if (sr_xpath_node_name_eq(xpath, "traffic-specification"))
            {
                for (auto it = module_shm.first->talkersList.begin(); it != module_shm.first->talkersList.end(); it++)
                {
                    // check for the right talker-id
                    if (it->getId() != talkerId)
                        continue;
                    // allocate the values
                    sr_val_t *data;
                    rc = sr_new_values(8, &data);
                    if (SR_ERR_OK != rc) {
                        return rc;
                    }
                    data[0].type = SR_UINT32_T;
                    data[0].data.uint32_val = it->traffic_specification.interval.numerator;
                    sr_val_build_xpath(&data[0], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/interval/numerator", id);
                    data[1].type = SR_UINT32_T;
                    data[1].data.uint32_val = it->traffic_specification.interval.denominator;
                    sr_val_build_xpath(&data[1], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/interval/denominator", id);
                    data[2].type = SR_UINT16_T;
                    data[2].data.uint16_val = it->traffic_specification.max_frames_per_interval;
                    sr_val_build_xpath(&data[2], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/max-frames-per-interval", id);
                    data[3].type = SR_UINT16_T;
                    data[3].data.uint16_val = it->traffic_specification.max_frame_size;
                    sr_val_build_xpath(&data[3], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/max-frame-size", id);
                    data[4].type = SR_UINT8_T;
                    data[4].data.uint8_val = it->traffic_specification.transmission_selection;
                    sr_val_build_xpath(&data[4], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/transmission-selection", id);
                    data[5].type = SR_UINT32_T;
                    data[5].data.uint32_val = it->traffic_specification.time_aware.earliest_transmit_offset;
                    sr_val_build_xpath(&data[5], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/time-aware/earliest-transmit-offset", id);
                    data[6].type = SR_UINT32_T;
                    data[6].data.uint32_val = it->traffic_specification.time_aware.latest_transmit_offset;
                    sr_val_build_xpath(&data[6], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/time-aware/latest-transmit-offset", id);
                    data[7].type = SR_UINT32_T;
                    data[7].data.uint32_val = it->traffic_specification.time_aware.jitter;
                    sr_val_build_xpath(&data[7], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/time-aware/jitter", id);
                    *values = data;
                    *values_cnt = 8;
                    return SR_ERR_OK;
                }
            }
            if (sr_xpath_node_name_eq(xpath, "user-to-network-requirements"))
            {
                for (auto it = module_shm.first->talkersList.begin(); it != module_shm.first->talkersList.end(); it++)
                {
                    // check for the right talker-id
                    if (it->getId() != talkerId)
                        continue;
                    // allocate the values
                    sr_val_t *data;
                    rc = sr_new_values(2, &data);
                    if (SR_ERR_OK != rc) {
                        return rc;
                    }
                    data[0].type = SR_UINT8_T;
                    data[0].data.uint8_val = it->user_to_network_requirements.num_seamless_trees;
                    sr_val_build_xpath(&data[0], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/user-to-network-requirements/num-seamless-trees", talkerId);
                    data[1].type = SR_UINT32_T;
                    data[1].data.uint32_val = it->user_to_network_requirements.max_latency;
                    sr_val_build_xpath(&data[1], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/user-to-network-requirements/max-latency", talkerId);
                    *values = data;
                    *values_cnt = 2;
                    return SR_ERR_OK;
                }
            }
            if (sr_xpath_node_name_eq(xpath, "interval"))
            {
                for (auto it = module_shm.first->talkersList.begin(); it != module_shm.first->talkersList.end(); it++)
                {
                    // check for the right talker-id
                    if (it->getId() != talkerId)
                        continue;
                    // allocate the values
                    sr_val_t *data;
                    rc = sr_new_values(2, &data);
                    if (SR_ERR_OK != rc) {
                        return rc;
                    }
                    data[0].type = SR_UINT32_T;
                    data[0].data.uint32_val = it->traffic_specification.interval.numerator;
                    sr_val_build_xpath(&data[0], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/interval/numerator", id);
                    data[1].type = SR_UINT32_T;
                    data[1].data.uint32_val = it->traffic_specification.interval.denominator;
                    sr_val_build_xpath(&data[1], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/interval/denominator", id);
                    *values = data;
                    *values_cnt = 2;
                    return SR_ERR_OK;
                }
            }
            if (sr_xpath_node_name_eq(xpath, "time-aware"))
            {
                for (auto it = module_shm.first->talkersList.begin(); it != module_shm.first->talkersList.end(); it++)
                {
                    // check for the right talker-id
                    if (it->getId() != talkerId)
                        continue;
                    // allocate the values
                    sr_val_t *data;
                    rc = sr_new_values(3, &data);
                    if (SR_ERR_OK != rc) {
                        return rc;
                    }
                    data[0].type = SR_UINT32_T;
                    data[0].data.uint32_val = it->traffic_specification.time_aware.earliest_transmit_offset;
                    sr_val_build_xpath(&data[0], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/time-aware/earliest-transmit-offset", id);
                    data[1].type = SR_UINT32_T;
                    data[1].data.uint32_val = it->traffic_specification.time_aware.latest_transmit_offset;
                    sr_val_build_xpath(&data[1], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/time-aware/latest-transmit-offset", id);
                    data[2].type = SR_UINT32_T;
                    data[2].data.uint32_val = it->traffic_specification.time_aware.jitter;
                    sr_val_build_xpath(&data[2], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%s']/traffic-specification/time-aware/jitter", id);
                    *values = data;
                    *values_cnt = 3;
                    return SR_ERR_OK;
                }
            }
            if (sr_xpath_node_name_eq(xpath, "interface-capabilities"))
            {
                for (auto it = module_shm.first->talkersList.begin(); it != module_shm.first->talkersList.end(); it++)
                {
                    // check for the right talker-id
                    if (it->getId() != talkerId)
                        continue;
                    // allocate the values
                    sr_val_t *data;
                    rc = sr_new_values((it->interface_capabilities.cb_stream_iden_type_list.size() + 
                        it->interface_capabilities.cb_stream_iden_type_list.size() + 1), &data);
                    if (SR_ERR_OK != rc) {
                        return rc;
                    }
                    data[0].type = SR_BOOL_T;
                    data[0].data.bool_val = it->interface_capabilities.vlan_tag_capable;
                    sr_val_build_xpath(&data[0], 
                        "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/interface-capabilities/vlan-tag-capable", talkerId);
                    int i = 1;
                    for (auto it2 = it->interface_capabilities.cb_stream_iden_type_list.begin(); it2 != it->interface_capabilities.cb_stream_iden_type_list.end();
                        it2++)
                    {
                        data[i].type = SR_UINT32_T;
                        data[i].data.uint32_val = *it2;
                        sr_val_build_xpath(&data[i], 
                            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/interface-capabilities/cb-stream-iden-type-list", talkerId);
                        i++;
                    }
                    for (auto it2 = it->interface_capabilities.cb_sequence_type_list.begin(); it2 != it->interface_capabilities.cb_sequence_type_list.end();
                        it2++)
                    {
                        data[i].type = SR_UINT32_T;
                        data[i].data.uint32_val = *it2;
                        sr_val_build_xpath(&data[i], 
                            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/interface-capabilities/cb-sequence-type-list", talkerId);
                        i++;
                    }
                    *values = data;
                    *values_cnt = i;
                    return SR_ERR_OK;
                }
                return SR_ERR_OK;
            }
            if (sr_xpath_node_name_eq(xpath, "data-frame-specification"))
            {
                SRP_LOG_DBG_MSG("requesting data frame specification");
                return SR_ERR_OK;
            }
            if (sr_xpath_node_name_eq(xpath, "ieee802-mac-addresses"))
            {
                for (auto it = module_shm.first->talkersList.begin(); it != module_shm.first->talkersList.end(); it++)
                {
                    // check for the right talker-id
                    if (it->getId() != talkerId)
                        continue;
                    // allocate the values
                    sr_val_t *data;
                    rc = sr_new_values(2, &data);
                    if (SR_ERR_OK != rc) {
                        return rc;
                    }
                    for (auto it2 = it->data_frame_specification_list.begin(); it2 != it->data_frame_specification_list.end(); it2++)
                    {
                        if(it2->index != index)
                            continue;
                        if(it2->choice.field != MAC)
                            continue;
                        data[0].type = SR_STRING_T;
                        sr_val_set_str_data(&data[0], SR_STRING_T, it2->choice.str1.c_str());
                        sr_val_build_xpath(&data[0], 
                            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/ieee802-mac-addresses/destination-mac-address", 
                            talkerId, index);
                        data[1].type = SR_STRING_T;
                        sr_val_set_str_data(&data[1], SR_STRING_T, it2->choice.str2.c_str());
                        sr_val_build_xpath(&data[1], 
                            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/ieee802-mac-addresses/source-mac-address", 
                            talkerId, index);
                        *values = data;
                        *values_cnt = 2;
                        return SR_ERR_OK;
                    }
                }
                return SR_ERR_OK;
            }
            if (sr_xpath_node_name_eq(xpath, "ieee802-vlan-tag"))
            {
                for (auto it = module_shm.first->talkersList.begin(); it != module_shm.first->talkersList.end(); it++)
                {
                    // check for the right talker-id
                    if (it->getId() != talkerId)
                        continue;
                    // allocate the values
                    sr_val_t *data;
                    rc = sr_new_values(2, &data);
                    if (SR_ERR_OK != rc) {
                        return rc;
                    }
                    for (auto it2 = it->data_frame_specification_list.begin(); it2 != it->data_frame_specification_list.end(); it2++)
                    {
                        if(it2->index != index)
                            continue;
                        if(it2->choice.field != VLAN)
                            continue;
                        data[0].type = SR_UINT8_T;
                        data[0].data.uint8_val = it2->choice.val1;
                        sr_val_build_xpath(&data[0], 
                            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/ieee802-vlan-tag/priority-code-point", 
                            talkerId, index);
                        data[1].type = SR_UINT16_T;
                        data[1].data.uint16_val = it2->choice.val2;
                        sr_val_build_xpath(&data[1], 
                            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/ieee802-vlan-tag/vlan-id", 
                            talkerId, index);
                        break;
                        *values = data;
                        *values_cnt = 2;
                        return SR_ERR_OK;
                    }
                }
                return SR_ERR_OK;
            }    
            // only when specific node is requrested and not talker container
            if (!strcmp(original_xpath, "/tsn-cuc-dect:*//"))
            if ((sr_xpath_node_name_eq(xpath, "ipv4-tuple")) || (sr_xpath_node_name_eq(xpath, "ipv6-tuple")))
            {
                for (auto it = module_shm.first->talkersList.begin(); it != module_shm.first->talkersList.end(); it++)
                {
                    // check for the right talker-id
                    if (it->getId() != talkerId)
                        continue;
                    // allocate the values
                    sr_val_t *data;
                    rc = sr_new_values(6, &data);
                    if (SR_ERR_OK != rc) {
                        return rc;
                    }
                    for (auto it2 = it->data_frame_specification_list.begin(); it2 != it->data_frame_specification_list.end(); it2++)
                    {
                        if(it2->index != index)
                            continue;
                        if((it2->choice.field != IPV4) && (it2->choice.field != IPV6))
                            continue;
                        std::string tuple;
                        if (it2->choice.field == IPV6)
                            tuple.assign("ipv6");
                        else
                            tuple.assign("ipv4");
                        char *tupleChar;
                        tupleChar = &tuple[0];

                        data[0].type = SR_STRING_T;
                        sr_val_set_str_data(&data[0], SR_STRING_T, it2->choice.str1.c_str());
                        sr_val_build_xpath(&data[0], 
                            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/%s-tuple/source-ip-address", 
                            talkerId, index, tupleChar);
                        data[1].type = SR_STRING_T;
                        sr_val_set_str_data(&data[1], SR_STRING_T, it2->choice.str2.c_str());
                        sr_val_build_xpath(&data[1], 
                            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/%s-tuple/destination-ip-address", 
                            talkerId, index, tupleChar);
                        data[2].type = SR_UINT8_T;
                        data[2].data.uint8_val = *it2->choice.dscp;
                        sr_val_build_xpath(&data[2], 
                            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/%s-tuple/dscp", 
                            talkerId, index, tupleChar);
                        data[3].type = SR_UINT16_T;
                        data[3].data.uint16_val = *it2->choice.protocol;
                        sr_val_build_xpath(&data[3], 
                            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/%s-tuple/protocol", 
                            talkerId, index, tupleChar);
                        data[4].type = SR_UINT16_T;
                        data[4].data.uint16_val = *it2->choice.source_port;
                        sr_val_build_xpath(&data[4], 
                            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/%s-tuple/source-port", 
                            talkerId, index, tupleChar);
                        data[5].type = SR_UINT16_T;
                        data[5].data.uint16_val = *it2->choice.destination_port;
                        sr_val_build_xpath(&data[5], 
                            "/tsn-cuc-dect:talkers-list/talker[talker-id='%i']/data-frame-specification[index='%i']/%s-tuple/destination-port", 
                            talkerId, index, tupleChar);

                        *values = data;
                        *values_cnt = 6;
                        return SR_ERR_OK;
                    }
                }
                return SR_ERR_OK;
            }
        }
        else
        {
            SRP_LOG_INF_MSG("Data locked. Try again later.");
            return SR_ERR_LOCKED;
        }
    } catch (boost::interprocess::interprocess_exception &ex) {
        std::cout << ex.what() << std::endl;
        return SR_ERR_INTERNAL;
    }
    return SR_ERR_OK;
}

/*
    @brief Callback function when state data from listeners is requested
    @return error code (SR_ERR_OK on success)
*/
static int request_listeners(const char *xpath, sr_val_t **values, size_t *values_cnt, uint64_t request_id, const char *original_xpath, void *private_ctx)
{
    SRP_LOG_DBG_MSG("In Callback for listeners.");
    int rc = SR_ERR_OK;

    // Create shared memory
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_only, "SYSREPO_SHM");

    // An allocator convertible to any allocator<T, segment_manager_t> type
    void_allocator alloc(segment.get_segment_manager());

    // Find the module in the managed shared memory
    std::pair<module_t*, std::size_t> module_shm = segment.find<module_t>("dect_cuc_tsn");

    int listenerId;
    try {
        SRP_LOG_DBG_MSG("Getting the mutex.");
        boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> lock(module_shm.first->mutex, boost::interprocess::try_to_lock);
        if(lock)
        {
            // only when no specific node is requrested but the listener container
            if (strcmp(original_xpath, "/tsn-cuc-dect:*//"))
            if (sr_xpath_node_name_eq(xpath, "listener"))
            {
                // keep track of the position
                int i = 0;
                int dataSize = 200;
                sr_val_t *data;
                rc = sr_new_values(dataSize, &data);
                if (SR_ERR_OK != rc) {
                    return rc;
                }
                for (auto it = module_shm.first->listenersList.begin(); it != module_shm.first->listenersList.end(); it++)
                {
                    listenerId = it->getId();
                    for (auto it2 = it->end_station_interface_list.begin(); it2 != it->end_station_interface_list.end(); it2++)
                    {
                        data[i].type = SR_STRING_T;
                        sr_val_set_str_data(&data[i], SR_STRING_T, it2->interface_name.c_str());
                        sr_val_build_xpath(&data[i], "/tsn-cuc-dect:listeners-list/listener[listener-id='%i']/end-station-interfaces[mac-address='%s'][interface-name='%s']/interface-name", 
                            listenerId, it2->mac_address.c_str(), it2->interface_name.c_str());
                        i++;
                        data[i].type = SR_STRING_T;
                        sr_val_set_str_data(&data[i], SR_STRING_T, it2->mac_address.c_str());
                        sr_val_build_xpath(&data[i], "/tsn-cuc-dect:listeners-list/listener[listener-id='%i']/end-station-interfaces[mac-address='%s'][interface-name='%s']/mac-address", 
                            listenerId, it2->mac_address.c_str(), it2->interface_name.c_str());
                        i++;
                    }
                }
                *values = data;
                *values_cnt = i;
                return SR_ERR_OK;
            }

            // state of xpath
            sr_xpath_ctx_t state;
            // needs a non-const char*
            std::string xpathStr;
            xpathStr.assign(xpath);
            char *xpathNonConst = &xpathStr[0];
            // get the key of listener-id
            char* id = sr_xpath_key_value(xpathNonConst, "listener", "listener-id", &state);
            if (!(id))
            {
                SRP_LOG_DBG_MSG("No listener identification - skip");
                return SR_ERR_OK;
            }
            // int listenerId;
            try{
                listenerId = stoi(id);
            }
            catch (const std::invalid_argument& ia) {
                std::cerr << "Invalid Input: " << ia.what() << '\n';
                return SR_ERR_INVAL_ARG;
            }

            // only when specific node is requrested and not listener container
            if (!strcmp(original_xpath, "/tsn-cuc-dect:*//"))
            if (sr_xpath_node_name_eq(xpath, "end-station-interfaces"))
            {
                for (auto it = module_shm.first->listenersList.begin(); it != module_shm.first->listenersList.end(); it++)
                {
                    if (it->getId() != listenerId)
                        continue;
                    // allocate the values
                    sr_val_t *data;
                    rc = sr_new_values(it->end_station_interface_list.size() * 2, &data);
                    if (SR_ERR_OK != rc) {
                        return rc;
                    }
                    int i = 0;
                    for (auto it2 = it->end_station_interface_list.begin(); it2 != it->end_station_interface_list.end(); it2++)
                    {
                        data[i].type = SR_STRING_T;
                        sr_val_set_str_data(&data[i], SR_STRING_T, it2->interface_name.c_str());
                        sr_val_build_xpath(&data[i], "/tsn-cuc-dect:listeners-list/listener[listener-id='%s']/end-station-interfaces[mac-address='%s'][interface-name='%s']/interface-name", 
                            id, it2->mac_address.c_str(), it2->interface_name.c_str());
                        i++;
                        data[i].type = SR_STRING_T;
                        sr_val_set_str_data(&data[i], SR_STRING_T, it2->mac_address.c_str());
                        sr_val_build_xpath(&data[i], "/tsn-cuc-dect:listeners-list/listener[listener-id='%s']/end-station-interfaces[mac-address='%s'][interface-name='%s']/mac-address", 
                            id, it2->mac_address.c_str(), it2->interface_name.c_str());
                        i++;
                    }
                    *values = data;
                    *values_cnt = i;
                    return SR_ERR_OK;
                }
            }
            if (sr_xpath_node_name_eq(xpath, "user-to-network-requirements"))
            {
                for (auto it = module_shm.first->listenersList.begin(); it != module_shm.first->listenersList.end(); it++)
                {
                    // check for the right talker-id
                    if (it->getId() != listenerId)
                        continue;
                    // allocate the values
                    sr_val_t *data;
                    rc = sr_new_values(2, &data);
                    if (SR_ERR_OK != rc) {
                        return rc;
                    }
                    data[0].type = SR_UINT8_T;
                    data[0].data.uint8_val = it->user_to_network_requirements.num_seamless_trees;
                    sr_val_build_xpath(&data[0], 
                        "/tsn-cuc-dect:listeners-list/listener[listener-id='%i']/user-to-network-requirements/num-seamless-trees", listenerId);
                    data[1].type = SR_UINT32_T;
                    data[1].data.uint32_val = it->user_to_network_requirements.max_latency;
                    sr_val_build_xpath(&data[1], 
                        "/tsn-cuc-dect:listeners-list/listener[listener-id='%i']/user-to-network-requirements/max-latency", listenerId);
                    *values = data;
                    *values_cnt = 2;
                    return SR_ERR_OK;
                }
            }
            if (sr_xpath_node_name_eq(xpath, "interface-capabilities"))
            {
                for (auto it = module_shm.first->listenersList.begin(); it != module_shm.first->listenersList.end(); it++)
                {
                    // check for the right talker-id
                    if (it->getId() != listenerId)
                        continue;
                    // allocate the values
                    sr_val_t *data;
                    rc = sr_new_values((it->interface_capabilities.cb_stream_iden_type_list.size() + 
                        it->interface_capabilities.cb_stream_iden_type_list.size() + 1), &data);
                    if (SR_ERR_OK != rc) {
                        return rc;
                    }
                    data[0].type = SR_BOOL_T;
                    data[0].data.bool_val = it->interface_capabilities.vlan_tag_capable;
                    sr_val_build_xpath(&data[0], 
                        "/tsn-cuc-dect:listeners-list/listener[listener-id='%i']/interface-capabilities/vlan-tag-capable", listenerId);
                    int i = 1;
                    for (auto it2 = it->interface_capabilities.cb_stream_iden_type_list.begin(); it2 != it->interface_capabilities.cb_stream_iden_type_list.end();
                        it2++)
                    {
                        data[i].type = SR_UINT32_T;
                        data[i].data.uint32_val = *it2;
                        sr_val_build_xpath(&data[i], 
                            "/tsn-cuc-dect:listeners-list/listener[listener-id='%i']/interface-capabilities/cb-stream-iden-type-list", listenerId);
                        i++;
                    }
                    for (auto it2 = it->interface_capabilities.cb_sequence_type_list.begin(); it2 != it->interface_capabilities.cb_sequence_type_list.end();
                        it2++)
                    {
                        data[i].type = SR_UINT32_T;
                        data[i].data.uint32_val = *it2;
                        sr_val_build_xpath(&data[i], 
                            "/tsn-cuc-dect:listeners-list/listener[listener-id='%i']/interface-capabilities/cb-sequence-type-list", listenerId);
                        i++;
                    }
                    *values = data;
                    *values_cnt = i;
                    return SR_ERR_OK;
                }
                return SR_ERR_OK;
            }
        }
        else
        {
            SRP_LOG_INF_MSG("Data locked. Try again later.");
            return SR_ERR_LOCKED;
        }
    } catch (boost::interprocess::interprocess_exception &ex) {
        std::cout << ex.what() << std::endl;
        return SR_ERR_INTERNAL;
    }            
    return SR_ERR_OK;
}

/*
    @brief callback when something is changed (also at startup) This saves the stream info into the shared memory
    @return error code (SR_ERR_OK on success)
*/
static int change_cb(sr_session_ctx_t *session, const char *module_name, sr_notif_event_t event, void *private_ctx)
{
    int rc = SR_ERR_OK;

    // state of xpath
    sr_xpath_ctx_t state;

    // Create shared memory
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_only, "SYSREPO_SHM");

    // An allocator convertible to any allocator<T, segment_manager_t> type
    void_allocator alloc(segment.get_segment_manager());

    // Find the module in the managed shared memory
    std::pair<module_t*, std::size_t> module_shm = segment.find<module_t>("dect_cuc_tsn");

    if (!module_shm.first)
        return SR_ERR_INTERNAL;

    // get the iterator and data val to cycle through config data
    sr_val_iter_t *it;
    sr_get_items_iter(session, "/tsn-cuc-dect:streams-list//*", &it);
    sr_val_t *val;

    // for construction of node name
    char node [50];

    try {
        SRP_LOG_DBG_MSG("Getting the mutex.");
        boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> lock(module_shm.first->mutex, boost::interprocess::try_to_lock);
        if(lock)
        {
            // process every single entry
            // process only changes: sr_get_changes_iter()?
            while ((sr_get_item_next(session, it, &val)) != SR_ERR_NOT_FOUND)
            {
                char* streamId = sr_xpath_key_value(strdup(val->xpath), "stream", "stream-id", &state);
                // No streamId: no useful data
                if (!(streamId))
                    continue;

                char* charListenerId = sr_xpath_key_value(strdup(val->xpath), "listeners-status-list", "listener-id", &state);
                int listenerId = -1;
                if (charListenerId)
                    try {
                        listenerId = stoi(charListenerId);
                    } catch (const std::invalid_argument& ia) {
                        std::cerr << "Invalid Input: " << ia.what() << '\n';
                        return 0;
                    }

                char* charIndex = sr_xpath_key_value(strdup(val->xpath), "config-list", "index", &state);
                int index = -1;
                if (charIndex)
                    try {
                        index = stoi(charIndex);
                    } catch (const std::invalid_argument& ia) {
                        std::cerr << "Invalid Input: " << ia.what() << '\n';
                        return 0;
                    }

                // interface configuration
                char* macAddress = sr_xpath_key_value(strdup(val->xpath), "interface-list", "mac-address", &state);
                char* interfaceName = sr_xpath_key_value(strdup(val->xpath), "interface-list", "interface-name", &state);
                
                // failed interfaces
                char* macAddressFailed = sr_xpath_key_value(strdup(val->xpath), "failed-interfaces", "mac-address", &state);
                char* interfaceNameFailed = sr_xpath_key_value(strdup(val->xpath), "failed-interfaces", "interface-name", &state);

                // cycle through streams for stream id
                for (auto it = module_shm.first->streamsList.begin(); it != module_shm.first->streamsList.end(); it++)
                {
                    // skip until stream id matches
                    if (strcmp(it->stream_id.c_str(), streamId) != 0)
                        continue;

                    // talker id
                    // set the talker id
                    if (sr_xpath_node_name_eq(strdup(val->xpath), "talker-id"))
                    {
                        it->talkerStatus.talker_id = val->data.uint8_val;
                        break;
                    }

                    // set accumulated latency for talker when no listener id is given
                    if (!(charListenerId) && (sr_xpath_node_name_eq(strdup(val->xpath), "accumulated-latency")))
                    {
                        it->talkerStatus.accumulated_latency = val->data.uint32_val;
                        break;
                    }

                    // talker interface Configuration 
                    // when macAddress and interface name is given and listener id is not
                    if ((macAddress) && (interfaceName) && (!(charListenerId)))
                    {
                        // cycle through interface list for combination of mac address and interface name
                        for (auto it2 = it->talkerStatus.interface_configuration.begin(); it2 != it->talkerStatus.interface_configuration.end(); it2++)
                        {
                            // Skip to next until combination of mac address and interface name is found
                            if (!((strcmp(macAddress, it2->mac_address.c_str()) == 0) && 
                                (strcmp(interfaceName, it2->interface_name.c_str()) == 0)))
                                continue;

                            // mac address and interface name already there!
                            // if no index value given, no need to cycle through config list
                            if (!(charIndex))
                            {
                                // optional?: update the values and exit loop
                                if (sr_xpath_node_name_eq(strdup(val->xpath), "mac-address"))
                                {
                                    it2->mac_address = val->data.string_val;
                                    break;
                                }
                                // optional?: update the values and exit loop
                                if (sr_xpath_node_name_eq(strdup(val->xpath), "interface-name"))
                                {
                                    it2->interface_name = val->data.string_val;
                                    break;
                                }
                                break;
                            }
                            // cycle through config lists for index -> talker may have time aware offset!
                            for (auto it3 = it2->config_list.begin(); it3 != it2->config_list.end(); it3++)
                            {
                                // skip until index is found
                                if (it3->index != index)
                                    continue;

                                // index found
                                // set the index -> redundant?
                                if (sr_xpath_node_name_eq(strdup(val->xpath), "index"))
                                {
                                    it3->index = val->data.uint8_val;
                                    break;
                                }
                                // create the choices when container type
                                if (val->type == SR_CONTAINER_T)
                                {
                                    if (sr_xpath_node_name_eq(strdup(val->xpath), "ieee802-vlan-tag"))
                                    {
                                        it3->choice.field = VLAN;
                                        break;
                                    }
                                    else if (sr_xpath_node_name_eq(strdup(val->xpath), "ieee802-mac-addresses"))
                                    {
                                        it3->choice.field = MAC;
                                        break;
                                    }
                                    else if (sr_xpath_node_name_eq(strdup(val->xpath), "ipv4-tuple"))
                                    {
                                        it3->choice.field = IPV4;
                                        break;
                                    }
                                    else if (sr_xpath_node_name_eq(strdup(val->xpath), "ipv6-tuple"))
                                    {
                                        it3->choice.field = IPV6;
                                        break;
                                    }
                                    else
                                    {
                                        break;
                                    }
                                }
                                // if string: add the source or destination info
                                if (val->type == SR_STRING_T)
                                {
                                    if ((sr_xpath_node_name_eq(strdup(val->xpath), "source-mac-address")) ||
                                        (sr_xpath_node_name_eq(strdup(val->xpath), "source-ip-address")))
                                        it3->choice.str1.assign(val->data.string_val);
                                    else if ((sr_xpath_node_name_eq(strdup(val->xpath), "destination-mac-address")) ||
                                        (sr_xpath_node_name_eq(strdup(val->xpath), "destination-ip-address")))
                                        it3->choice.str2.assign(val->data.string_val);
                                    break;
                                }
                                // time aware offset as uint32
                                if (sr_xpath_node_name_eq(strdup(val->xpath), "time-aware-offset"))
                                {
                                    it3->choice.field = TAO;
                                    it3->choice.val1 = val->data.uint32_val;
                                    break;
                                }
                                // if uint 8: add pcp or dscp
                                if (val->type == SR_UINT8_T)
                                {
                                    it3->choice.val1 = val->data.uint8_val;
                                    break;
                                }
                                // SR_UINT16_t from here to end
                                // add protocol or vlan id
                                if ((sr_xpath_node_name_eq(strdup(val->xpath), "protocol")) || 
                                    (sr_xpath_node_name_eq(strdup(val->xpath), "vlan-id")))
                                {
                                    it3->choice.val2 = val->data.uint16_val;
                                    break;
                                }
                                // add source port
                                if (sr_xpath_node_name_eq(strdup(val->xpath), "source-port"))
                                {
                                    it3->choice.val3 = val->data.uint16_val;
                                    break;
                                }
                                // add destination port
                                if (sr_xpath_node_name_eq(strdup(val->xpath), "destination-port"))
                                {
                                    it3->choice.val4 = val->data.uint16_val;
                                    break;
                                }
                            }
                            // index not found -> add it and set index!
                            if (!charListenerId && charIndex && macAddress && interfaceName && !macAddressFailed && !interfaceNameFailed)
                            {
                                sprintf(node, "config-list[index='%s']", charIndex);
                                if ((val->type = SR_LIST_T) && (sr_xpath_node_name_eq(strdup(val->xpath), node)))
                                {
                                    it2->addConfig(config_t(data_frame_specification_t(alloc)));
                                    it2->config_list.end()->index = val->data.uint8_val;
                                    break;
                                }
                            }
                        }
                        // combination of mac address and interface name not found -> add!
                        if (!charListenerId && !charIndex && macAddress && interfaceName && !macAddressFailed && !interfaceNameFailed)
                            if (val->type = SR_LIST_T)
                            {
                                it->talkerStatus.addInterfaceConfiguration(interface_configuration_t(macAddress, interfaceName, alloc));
                                break;
                            }
                        break;
                    }

                    // listeners status
                    // ListenerId: Listener Status
                    if (charListenerId)
                    {
                        // cycle through listener status list for listener id
                        for (auto it2 = it->listenerStatusList.begin(); it2 != it->listenerStatusList.end(); it2++)
                        {
                            // skip until listener id matches
                            if(it2->listener_id != listenerId)
                                continue;

                            // when the val sets accumulated latency
                            if (sr_xpath_node_name_eq(strdup(val->xpath), "accumulated-latency"))
                            {
                                it2->accumulated_latency = val->data.uint32_val;
                                break;
                            }
                            // only carry on when interface name and mac address are given!
                            if (!(macAddress) || !(interfaceName))
                                break;

                            // cycle through interface list for interface name and mac address
                            for (auto it3 = it2->interface_configuration.begin(); it3 != it2->interface_configuration.end(); it3++)
                            {
                                // Skip to next until combination of mac address and interface name is found
                                if (!((strcmp(macAddress, it3->mac_address.c_str()) == 0) && 
                                    (strcmp(interfaceName, it3->interface_name.c_str()) == 0)))
                                    continue;

                                // mac address and interface name already there!
                                // if no index value given, no need to cycle through config list
                                if (!(charIndex))
                                {
                                    // optional?: update the values and exit loop
                                    if (sr_xpath_node_name_eq(strdup(val->xpath), "mac-address"))
                                    {
                                        it3->mac_address = val->data.string_val;
                                        break;
                                    }
                                    // optional?: update the values and exit loop
                                    if (sr_xpath_node_name_eq(strdup(val->xpath), "interface-name"))
                                    {
                                        it3->interface_name = val->data.string_val;
                                        break;
                                    }
                                    break;
                                }

                                // cycle through config lists for index -> listener must have no time aware offset!
                                for (auto it4 = it3->config_list.begin(); it4 != it3->config_list.end(); it4++)
                                {
                                    // skip until index is found
                                    if (it4->index != index)
                                        continue;

                                    // index found
                                    // set the index -> redundant?
                                    if (sr_xpath_node_name_eq(strdup(val->xpath), "index"))
                                    {
                                        it4->index = val->data.uint8_val;
                                        break;
                                    }
                                    // if container type: set the type
                                    if (val->type == SR_CONTAINER_T)
                                    {
                                        if (sr_xpath_node_name_eq(strdup(val->xpath), "ieee802-vlan-tag"))
                                        {
                                            it4->choice.field = VLAN;
                                            break;
                                        }
                                        else if (sr_xpath_node_name_eq(strdup(val->xpath), "ieee802-mac-addresses"))
                                        {
                                            it4->choice.field = MAC;
                                            break;
                                        }
                                        else if (sr_xpath_node_name_eq(strdup(val->xpath), "ipv4-tuple"))
                                        {
                                            it4->choice.field = IPV4;
                                            break;
                                        }
                                        else if (sr_xpath_node_name_eq(strdup(val->xpath), "ipv6-tuple"))
                                        {
                                            it4->choice.field = IPV6;
                                            break;
                                        }
                                        else
                                            break;
                                    }
                                    // if string: add the source or destination info
                                    if (val->type == SR_STRING_T)
                                    {
                                        // does not work!
                                        if ((sr_xpath_node_name_eq(strdup(val->xpath), "source-mac-address")) ||
                                            (sr_xpath_node_name_eq(strdup(val->xpath), "source-ip-address")))
                                            it4->choice.str1.assign(val->data.string_val);
                                        else if ((sr_xpath_node_name_eq(strdup(val->xpath), "destination-mac-address")) ||
                                            (sr_xpath_node_name_eq(strdup(val->xpath), "destination-ip-address")))
                                            it4->choice.str2.assign(val->data.string_val);
                                        break;
                                    }
                                    // if uint 8: add pcp or dscp info
                                    if (val->type == SR_UINT8_T)
                                    {
                                        it4->choice.val1 = val->data.uint8_val;
                                        break;
                                    }
                                    // SR_UINT32_t from here to end
                                    // add protocol or vlan id
                                    if ((sr_xpath_node_name_eq(strdup(val->xpath), "protocol")) || 
                                        (sr_xpath_node_name_eq(strdup(val->xpath), "vlan-id")))
                                    {
                                        it4->choice.val2 = val->data.uint16_val;
                                        break;
                                    }
                                    // add source port
                                    if (sr_xpath_node_name_eq(strdup(val->xpath), "source-port"))
                                    {
                                        it4->choice.val3 = val->data.uint16_val;
                                        break;
                                    }
                                    // add destination port
                                    if (sr_xpath_node_name_eq(strdup(val->xpath), "destination-port"))
                                    {
                                        it4->choice.val4 = val->data.uint16_val;
                                        break;
                                    }
                                }
                                // index was not found: add config_t and set index
                                if (charListenerId && charIndex && macAddress && interfaceName && !macAddressFailed && !interfaceNameFailed)
                                {
                                    char node [50];
                                    sprintf(node, "config-list[index='%s']", charIndex);
                                    if ((val->type = SR_LIST_T) && (sr_xpath_node_name_eq(strdup(val->xpath), node)))
                                    {
                                        it3->addConfig(config_t(alloc));
                                        (--it3->config_list.end())->index = index;
                                        break;
                                    }
                                }
                            }
                            // combination of mac address and interface not found - add it when list type!
                            if (charListenerId && !charIndex && macAddress && interfaceName && !macAddressFailed && !interfaceNameFailed)
                                if (val->type == SR_LIST_T)
                                {
                                    it2->addInterfaceConfiguration(interface_configuration_t(macAddress, interfaceName, alloc));
                                    break;
                                }
                        }
                        // listener id does not exist - add a new listener
                        if (charListenerId && !charIndex && !macAddress && !interfaceName && !macAddressFailed && !interfaceNameFailed)
                            if (val->type == SR_LIST_T)
                            {
                                it->addListenerStatus(listeners_status_t(alloc));
                                (--it->listenerStatusList.end())->listener_id = listenerId;
                                break;
                            }
                        break;
                    }
                    // failed-interfaces
                    if (macAddressFailed && interfaceNameFailed)
                    {
                        // add a new entry when list type
                        if (val->type == SR_LIST_T)
                        {  
                            // cycle through failed interfaces
                            for (auto it2 = it->failedInterfacesList.begin(); it2 != it->failedInterfacesList.end(); it2++)
                            {
                                // skip until match found
                                if (!((strcmp(macAddressFailed, it2->mac_address.c_str()) == 0)) && 
                                    (strcmp(interfaceNameFailed, it2->interface_name.c_str()) == 0))
                                    continue;
                                
                                // optional?: update the values and exit loop
                                if (sr_xpath_node_name_eq(strdup(val->xpath), "mac-address"))
                                {
                                    it2->mac_address = val->data.string_val;
                                    break;
                                }
                                // optional?: update the values and exit loop
                                if (sr_xpath_node_name_eq(strdup(val->xpath), "interface-name"))
                                {
                                    it2->interface_name = val->data.string_val;
                                    break;
                                }
                                break;
                            }
                            // add new one if not found
                            it->addFailedInterface(end_station_interface_t(macAddressFailed, interfaceNameFailed, alloc));
                        }
                        break;
                    }
                    // status-info
                    if (sr_xpath_node_name_eq(strdup(val->xpath), "talker-status"))
                    {
                        if (val->type != SR_ENUM_T)
                            break;
                        if ((strcmp(val->data.string_val, "none") == 0))
                            it->status_info.talker_status = T_NONE;
                        else if ((strcmp(val->data.string_val, "ready") == 0))
                            it->status_info.talker_status = T_READY;
                        else if ((strcmp(val->data.string_val, "failed") == 0))
                            it->status_info.talker_status = T_FAILED;
                        else
                            break;
                    }
                    if (sr_xpath_node_name_eq(strdup(val->xpath), "listener-status"))
                    {    
                        if (val->type != SR_ENUM_T)
                            break;
                        if ((strcmp(val->data.string_val, "none") == 0))
                            it->status_info.listener_status = L_NONE;
                        else if ((strcmp(val->data.string_val, "ready") == 0))
                            it->status_info.listener_status = L_READY;
                        else if ((strcmp(val->data.string_val, "partial-failed") == 0))
                            it->status_info.listener_status = L_PARTIAL_FAILED;
                        else if ((strcmp(val->data.string_val, "failed") == 0))
                            it->status_info.listener_status = L_FAILED;
                        else
                            break;
                    }
                    if (sr_xpath_node_name_eq(strdup(val->xpath), "failure-code"))
                    {
                        it->status_info.failure_code = val->data.uint8_val;
                    }
                }
                // stream id does not exist - add a new stream
                if (!charListenerId && !charIndex && !macAddress && !interfaceName && !macAddressFailed && !interfaceNameFailed)
                    if (val->type == SR_LIST_T)
                    {
                        module_shm.first->addStream(stream_t(alloc));
                        (--module_shm.first->streamsList.end())->setStreamId(streamId);
                    }
            }
        }        
        else
        {
            SRP_LOG_INF_MSG("Data locked. Try again later.");
            return SR_ERR_INTERNAL;
        }
    } catch (boost::interprocess::interprocess_exception &ex) {
        std::cout << ex.what() << std::endl;
        return SR_ERR_INTERNAL;
    }
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