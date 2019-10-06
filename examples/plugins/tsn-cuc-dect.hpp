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

/*
    The standard segment manager for shared memory containers
*/
typedef boost::interprocess::managed_shared_memory::segment_manager                                 segment_manager_t;

/*
    The standard void allocator for shared memory containers
*/
typedef boost::interprocess::allocator<void, segment_manager_t>                                     void_allocator;

/*
    The standard char allocator for all strings in the shared memory
*/
typedef boost::interprocess::allocator<char, segment_manager_t>                                     char_allocator;

/*
    The standard string data type for all strings in the shared memory
*/
typedef boost::interprocess::basic_string<char, std::char_traits<char>, char_allocator>             char_string;

/* 
    type for mac addresses
*/

typedef char_string mac_t;

/* 
    type for ipv4 addresses from ietf-inet-types YANG Module
*/
typedef char_string ipv4_address_t;

/* 
    type for ipv6 addresses
*/
typedef char_string ipv6_address_t;

/* 
    choice for data frame specification 
*/
enum field {MAC, VLAN, IPV4, IPV6};

/* 
    type for end station interface list in groups 
    uses group-interface-id
*/
struct end_station_interface_t {
    // char_string str1;
    // char_string str2;
    // mac_t mac_address = str1;
    // mac_t interface_name = str2;
    mac_t mac_address;
    mac_t interface_name;    
    // mac_t* mac_address = &str1;
    // mac_t* interface_name = &str2;
    end_station_interface_t(const char_allocator &alloc)
        : mac_address(alloc), interface_name(alloc)
        {} 
};

/* 
    type for stream-rank in groups 
*/
struct stream_rank_t {
    int rank;
};

/*
    structure for interval in traffic specification
*/
struct interval_t{
    int numerator;
    int denominator;
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
    listener Class
    lists as single linked lists!
    includes group-listener from YANG 
*/
// class listener_t : public end_station_t{
//     public:
//     listener_t(int id, user_to_network_requirements_t user_to_network_requirements, interface_capabilities_t interface_capabilities);
// };

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

//Typedefs of allocators and containers



/*
    choice for data_frame_specification_t depending on field:
    1 = VLAN:
        pcp and vlan_id
    2 = MAC:
        source_mac_address, destination_mac_address
    3 = IPv4:
        ipv4_source_ip_address, ipv4_source_ip_address, dscp, protocol, source_port, destination_port
    4 = IPv6:
        ipv6_source_ip_address, ipv6_source_ip_address, dscp, protocol, source_port, destination_port
*/
struct choice_t {
    int field;
    char_string str1;
    char_string str2;
    mac_t* source_mac_address = &str1;
    ipv4_address_t* ipv4_source_ip_address = &str1;
    ipv6_address_t* ipv6_source_ip_address = &str1;
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
    choice_t(const char_allocator &alloc)
        : str1(alloc), str2(alloc)
        {}
};

/*
    data frame specification for data_frame_specification_list_t
    needs choice_t in constructor and an index
*/
struct data_frame_specification_t {
    int index;
    choice_t choice;
    data_frame_specification_t(const char_allocator &alloc)
        : index(-1), choice(alloc) 
        {}
};

typedef boost::interprocess::allocator<int, segment_manager_t>                                      int_allocator;
typedef boost::interprocess::list<int, int_allocator>                                               int_list;
/*
    Structure for the interface capabilities group
    has shared memory int lists for Lists
*/
struct interface_capabilities_t
{
    bool vlan_tag_capable;
    int_list cb_stream_iden_type_list;
    int_list cb_sequence_type_list;
    interface_capabilities_t(const void_allocator &alloc)
        : cb_stream_iden_type_list(alloc), cb_sequence_type_list(alloc)
        {}
};

typedef boost::interprocess::allocator<end_station_interface_t, segment_manager_t>                  end_station_interface_allocator;
typedef boost::interprocess::list<end_station_interface_t, end_station_interface_allocator>         end_station_interface_list_t;
/*
    Structure for end station data type
    has shared memory lists for end station interfaces
    and normal structs for user to network requirements, interface capabilities and
    a unique id
*/
struct end_station_t {
    public:
    int id;
    user_to_network_requirements_t user_to_network_requirements;
    interface_capabilities_t interface_capabilities;
    end_station_interface_list_t end_station_interface_list;
    end_station_t(int id, const void_allocator &alloc)
        : id(id), interface_capabilities(alloc), end_station_interface_list(alloc)
        {}
};



typedef boost::interprocess::allocator<data_frame_specification_t, segment_manager_t>               data_frame_specification_allocator;
typedef boost::interprocess::list<data_frame_specification_t, data_frame_specification_allocator>   data_frame_specification_list_t;
/*
    Class for the talkers in the module.
    Inherits from end_station_t
    has shared memory list for data_frame_specification
    and additional rank as well as traffic specification
*/
class talker_t : public end_station_t{
    public:
    stream_rank_t stream_rank;
    data_frame_specification_list_t data_frame_specification_list;
    traffic_specification_t traffic_specification;
    talker_t(int id, const void_allocator &alloc)
        : end_station_t(id, alloc), data_frame_specification_list(alloc)
        {}
    void printData();
};

typedef boost::interprocess::allocator<device_t, segment_manager_t>                                 device_t_allocator;
typedef boost::interprocess::list<device_t, device_t_allocator>                                     device_list_t;
typedef boost::interprocess::allocator<talker_t, segment_manager_t>                                 talker_t_allocator;
typedef boost::interprocess::list<talker_t, talker_t_allocator>                                     talker_list_t;
/*
    Overall structure for the whole module in the shared memory
    has shared memory lists of devices, talkers and listeners
*/
class module_t{
    public:
    // std::list<device_t> devicesList;
    device_list_t devicesList;
    talker_list_t talkersList;
    // List<listener_t> *listenersList;
    // std::list<device_t> *devicesList;
    // std::list<talker_t> *talkersList;
    // std::list<listener_t> *listenersList;
    // std::list<status_stream_t> streamsList;
    // public:
    // module_t(List<device_t> *devicesList, List<talker_t> *talkersList);
    // module_t(boost::interprocess::managed_shared_memory &managed_shm, boost::interprocess::managed_shared_memory::segment_manager *mgr);
    // module_t(List<device_t> *devicesList);
    // module_t();
    // int addDevice(device_t device);
    // int removeDevice(int id);
    // int addTalker(talker_t talker);
    // int removeTalker(int id);
    // int addListener(listener_t listener);
    // int removeListener(int id);
    module_t(void_allocator &alloc)
        : devicesList(alloc), talkersList(alloc)
        {}
};

// Stream
class config_list_t {
    int id;
    data_frame_specification_list_t config_values;
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