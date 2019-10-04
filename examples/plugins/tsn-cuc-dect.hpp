#include <stdlib.h>
#include <iostream>
#include <string>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h> 
#include <list>

#ifndef IPC
#define IPC
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/list.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/vector.hpp>

template <typename T> using Alloc = boost::interprocess::allocator<T, boost::interprocess::managed_shared_memory::segment_manager>;
template <typename K> using List = boost::interprocess::list<K, Alloc<K>>;

// typedef module_sm_t<module_t, std::char_traits<module_t>, Alloc<module_t>>   module_sm_t;
#endif

#ifndef SYSREPO
#define SYSREPO
#include <sysrepo.h>
#include <sysrepo/plugins.h>
#include <sysrepo/values.h>
#endif

/* type for PMID: should only have 20 Bits used*/
typedef uint32_t pmid_t;
/* type for mac addresses
*/
typedef boost::container::string mac_t;
/* type for ipv4 addresses from ietf-inet-types YANG Module
*/
typedef boost::container::string ipv4_address_t;

/* type for ipv6 addresses
*/
typedef boost::container::string ipv6_address_t;

/* choice for data frame specification */
enum field {MAC, VLAN, IPV4, IPV6};

/* type for end station interface list in groups 
uses group-interface-id*/
struct end_station_interface_t {
    // mac_t mac_address;
    // std::string interface_name;
    boost::container::string mac_address;
    boost::container::string interface_name;
};

/* type for stream-rank in groups */
struct stream_rank_t {
    int rank;
};

/*
    type combining all choices, identifiers are overloaded by pointers
*/
struct choice_t {
    int field;
    boost::container::string str1;
    boost::container::string str2;
    mac_t* source_mac_address = &str1;
    ipv4_address_t* ipv4_source_ip_address = &str1;
    ipv6_address_t* ipv6_source_ip_address = &str1;
    boost::container::string* dest_mac_address = &str2;
    mac_t* destination_mac_address = &str2;
    ipv4_address_t* ipv4_destination_ip_address = &str2;
    ipv6_address_t* ipv6_destination_ip_address = &str2;
    int val1;
    int* pcp = &val1;
    int* dscp = &val1;
    int val2;
    int* vlan_id = &val2;
    int* protocol = &val2;
    int val3;
    int* source_port = &val3;
    int val4;
    int* destination_port = &val4;
};

/* 
    type for data-frame-specification 
    gives choice for what spec. to use 
*/
class data_frame_specification_t {
    public:
    int index;
    choice_t choice;
    data_frame_specification_t(choice_t choice);
};

/*
    structure for interval in traffic specification
*/
struct interval_t{
    int numerator = -1;
    int denominator = -1;
};

/*
    structure for time aware in traffic spec
*/
struct time_aware_t
{
    int earliest_transmit_offset;
    int latest_transmit_offset;
    int jitter;
};

/*
    structure for user-to-network-requirements in traffic spec
*/
struct user_to_network_requirements_t
{
    int num_seamless_trees;
    int max_latency;
};

/*
    structure for interface-capabilities
    contains two lists of int
*/
struct interface_capabilities_t
{
    bool vlan_tag_capable;
    std::list<int> cb_stream_iden_type_list;
    std::list<int> cb_sequence_type_list;
};


/* 
    struct for traffic specification
*/
struct traffic_specification_t {
    interval_t interval;
    int max_frames_per_interval;
    int max_frame_size;
    int transmission_selection;
    time_aware_t time_aware;
};

/* 
    Structure for the Data in the YANG model
    Device datatype for items in devices list 
*/
class device_t {
    static int count;
    boost::container::string name;
    pmid_t pmid;
    public:
    // boost::container::string name2;
    int id;
    device_t(std::string name, pmid_t pmid);
    boost::container::string getName();
    int getId();
    pmid_t getPmid();
};

/*
    end station Class
    lists as single linked lists!
    includes group-listener from YANG 
*/
class end_station_t {
    public:
    int id;
    user_to_network_requirements_t user_to_network_requirements;
    interface_capabilities_t interface_capabilities;
    List<end_station_interface_t> *end_station_interface_list;
    int add_interface(end_station_interface_t interface);
    int getId();
};

/*
    listener Class
    lists as single linked lists!
    includes group-listener from YANG 
*/
class listener_t : public end_station_t{
    public:
    listener_t(int id, user_to_network_requirements_t user_to_network_requirements, interface_capabilities_t interface_capabilities);
};


/* 
    Talker class
    lists as single linked lists!
    includes group-talker from YANG 
    has listener as base class
*/
class talker_t : public end_station_t{
    public:
    stream_rank_t stream_rank;
    List<data_frame_specification_t> *data_frame_specification_list;
    traffic_specification_t traffic_specification;
    talker_t(int id, int rank, traffic_specification_t traffic_specification,
        user_to_network_requirements_t user_to_network_requirements, 
        interface_capabilities_t interface_capabilities);
    talker_t(List<data_frame_specification_t> *data_frame_specification_list, List<end_station_interface_t> *end_station_interface_list);
    talker_t();
    int add_specification(data_frame_specification_t specification);
    void printData();
    // int remove_interface();
    // int remove_specification();
};

enum talker_status_t {T_NONE, T_READY, T_FAILED};
enum listener_status_t {L_NONE, L_READY, L_PARTIAL_FAILED, L_FAILED};

/*
    group-status-stream from YANG model
*/
struct status_info_t{
    talker_status_t talker_status;
    listener_status_t listener_status;
    int failure_code;
};

/*
    config list inside interface list inside group-status-talker-listener
*/
class config_list_t {
    int id;
    List<data_frame_specification_t> *config_values;
};

/*
    group-interface-configuration
*/
class interface_configuration_t {
    public:
    std::list<config_list_t> config_list;
};

/*
    group-status-talker-listener
*/
struct status_talker_listener_t {
    int id;
    int accumulated_latency;
    interface_configuration_t interface_configuration;
};

/*
    List of interfaces: need to compare original interfaces?
    Failed interfaces are no pointers, they come from the CNC
*/
class status_stream_t {
    public:
    std::string stream_id;
    status_info_t status_info;
    status_talker_listener_t talkerStatus;
    std::list<status_talker_listener_t> listenerStatusList;
    std::list<end_station_interface_t> failedInterfacesList;
    status_stream_t(std::string stream_id, status_info_t status_info);
    int add_interface(end_station_interface_t failed_interface);
};

/*
    Overall structure for the whole module in the shared memory
    Maybe introduce visibility changes?
*/
class module_t{
    public:
    // std::list<device_t> devicesList;
    int test;
    List<device_t> *devicesList;
    List<talker_t> *talkersList;
    List<listener_t> *listenersList;
    // std::list<device_t> *devicesList;
    // std::list<talker_t> *talkersList;
    // std::list<listener_t> *listenersList;
    std::list<status_stream_t> streamsList;
    // public:
    module_t(List<device_t> *devicesList, List<talker_t> *talkersList);
    // module_t(boost::interprocess::managed_shared_memory &managed_shm, boost::interprocess::managed_shared_memory::segment_manager *mgr);
    // module_t(List<device_t> *devicesList);
    // module_t();
    int addDevice(device_t device);
    int removeDevice(int id);
    int addTalker(talker_t talker);
    int removeTalker(int id);
    int addListener(listener_t listener);
    int removeListener(int id);
};


// testing shmem
//Typedefs of allocators and containers
struct choice_t_shm {
    int field;
    boost::container::string str1;
    boost::container::string str2;
    mac_t* source_mac_address = &str1;
    ipv4_address_t* ipv4_source_ip_address = &str1;
    ipv6_address_t* ipv6_source_ip_address = &str1;
    boost::container::string* dest_mac_address = &str2;
    mac_t* destination_mac_address = &str2;
    ipv4_address_t* ipv4_destination_ip_address = &str2;
    ipv6_address_t* ipv6_destination_ip_address = &str2;
    int val1;
    int* pcp = &val1;
    int* dscp = &val1;
    int val2;
    int* vlan_id = &val2;
    int* protocol = &val2;
    int val3;
    int* source_port = &val3;
    int val4;
    int* destination_port = &val4;
};


struct data_frame_specification_t_shm {
    int index;
    choice_t_shm choice;
    data_frame_specification_t_shm(choice_t_shm choice);
};

typedef boost::interprocess::managed_shared_memory::segment_manager                       segment_manager_t;
typedef boost::interprocess::allocator<void, segment_manager_t>                           void_allocator;
typedef boost::interprocess::allocator<int, segment_manager_t>                            int_allocator;
typedef boost::interprocess::allocator<char, segment_manager_t>                           char_allocator;
typedef boost::interprocess::basic_string<char, std::char_traits<char>, char_allocator>   char_string;

typedef boost::interprocess::allocator<end_station_interface_t, segment_manager_t>        end_station_interface_allocator;
typedef boost::interprocess::allocator<data_frame_specification_t_shm, segment_manager_t>        data_frame_specification_allocator;

typedef boost::interprocess::list<int, int_allocator>                                     int_list;
typedef boost::interprocess::list<end_station_interface_t, end_station_interface_allocator>         end_station_interface_list_t;
typedef boost::interprocess::list<data_frame_specification_t_shm, data_frame_specification_allocator>   data_frame_specification_list_t;

struct interface_capabilities_t_shm
{
    bool vlan_tag_capable;
    int_list cb_stream_iden_type_list;
    int_list cb_sequence_type_list;
    interface_capabilities_t_shm(const void_allocator &alloc)
        : cb_stream_iden_type_list(alloc), cb_sequence_type_list(alloc)
        {}
};

class end_station_t_shm {
    public:
    int id;
    user_to_network_requirements_t user_to_network_requirements;
    interface_capabilities_t_shm interface_capabilities;
    end_station_interface_list_t end_station_interface_list;
    end_station_t_shm(int id, const void_allocator &alloc)
        : interface_capabilities(alloc), end_station_interface_list(alloc)
        {}
};

class talker_t_shm : public end_station_t_shm{
    public:
    stream_rank_t stream_rank;
    data_frame_specification_list_t data_frame_specification_list;
    traffic_specification_t traffic_specification;
    // talker_t_shm(int id, int rank, traffic_specification_t traffic_specification,
    //     user_to_network_requirements_t user_to_network_requirements, 
    //     interface_capabilities_t_shm interface_capabilities, const void_allocator &alloc);
    talker_t_shm(int id, const void_allocator &alloc)
        : end_station_t_shm(id, alloc), data_frame_specification_list(alloc)
        {}
    // talker_t();
    // int add_specification(data_frame_specification_t specification);
    void printData();
};



