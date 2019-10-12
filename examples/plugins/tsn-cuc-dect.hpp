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
    type for PMID: should only have 20 Bits used pattern '[0-1]{20}'
*/
typedef char_string pmid_t;

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
    mac_t mac_address;
    mac_t interface_name;
    end_station_interface_t(const char_allocator &alloc)
        : mac_address(alloc), interface_name(alloc)
        {}
    end_station_interface_t(std::string macAddress, std::string interfaceName, const char_allocator &alloc);
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
    device_t(std::string name, const char* pmid, const char_allocator &alloc);
    boost::container::string getName();
    int getId();
    const char* getPmid();
};

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
    // VLAN constructor
    choice_t(int pcp, int vlan_id, const char_allocator &alloc);
    // MAC constructor source, dest
    choice_t(std::string mac_source, std::string mac_dest, const char_allocator &alloc);
    // IPV4/6 constructor
    choice_t(std::string ipSource, std::string ipDest, int sourcePort, int destPort, int dscp, int protocol, const char_allocator &alloc);
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
    data_frame_specification_t(int index, std::string mac_source, std::string mac_dest, const char_allocator &alloc)
        : index(index), choice(mac_source, mac_dest, alloc)
        {}
    data_frame_specification_t(int index, int pcp, int vlan_id, const char_allocator &alloc)
        : index(index), choice(pcp, vlan_id, alloc)
        {}
    data_frame_specification_t(int index, std::string ipSource, std::string ipDest, int sourcePort, int destPort, int dscp, int protocol, const char_allocator &alloc)
        : index(index), choice(ipSource, ipDest, sourcePort, destPort, dscp, protocol, alloc)
        {}
};

typedef boost::interprocess::allocator<uint, segment_manager_t>                                     uint_allocator;
typedef boost::interprocess::list<uint, uint_allocator>                                             uint_list;
/*
    Structure for the interface capabilities group
    has shared memory int lists for Lists
*/
struct interface_capabilities_t
{
    bool vlan_tag_capable;
    uint_list cb_stream_iden_type_list;
    uint_list cb_sequence_type_list;
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
class end_station_t {
    int id;
    public:
    user_to_network_requirements_t user_to_network_requirements;
    interface_capabilities_t interface_capabilities;
    end_station_interface_list_t end_station_interface_list;
    int setId(int id);
    int getId();
    int addInterface(end_station_interface_t esi);
    int removeInterface(end_station_interface_list_t::iterator it);
    int addCBSequenceType(uint type);
    int addCBStreamIdenType(uint type);
    int setUserToNetworkRequirements(int maxLatency, int numSeamlessTrees);
    end_station_t(int id, const void_allocator &alloc);
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
    stream_rank_t stream_rank;
    public:
    data_frame_specification_list_t data_frame_specification_list;
    traffic_specification_t traffic_specification;
    talker_t(int id, const void_allocator &alloc)
        : end_station_t(id, alloc), data_frame_specification_list(alloc)
        {}
    int addSpecification(data_frame_specification_t spec);
    int setRank(int rank);
    void printData();
};

/*
    Class for the listeners in the module
    inherits from end_station_t
*/
class listener_t : public end_station_t{
    public:
    listener_t(int id, const void_allocator &alloc)
        : end_station_t(id, alloc)
        {}
    void printData();
};

/*
    Streams
*/
enum talker_status_t {T_NONE, T_READY, T_FAILED};
enum listener_status_t {L_NONE, L_READY, L_PARTIAL_FAILED, L_FAILED};
// pattern '[0-9a-fA-F]{2}(-[0-9a-fA-F]{2}){5}:[0-9a-fA-F]{2}-[0-9a-fA-F]{2}'
typedef char_string stream_id_t;

/*
    group-status-stream from YANG model
    uses the enums
*/
struct status_info_t{
    talker_status_t talker_status;
    listener_status_t listener_status;
    int failure_code;
};

/*
    Config list
*/
struct config_t : public data_frame_specification_t {
    int time_aware_offset;
    config_t(const void_allocator &alloc)
        : data_frame_specification_t(alloc)
        {}
    config_t(data_frame_specification_t dfs, int timeAwareOffset)
        : data_frame_specification_t(dfs), time_aware_offset(timeAwareOffset)
        {}
};

typedef boost::interprocess::allocator<config_t, segment_manager_t>                 config_t_allocator;
typedef boost::interprocess::list<config_t, config_t_allocator>                     config_list_t;
/*
    group-interface-configuration
*/
struct interface_configuration_t : end_station_interface_t{
    config_list_t config_list;
    interface_configuration_t(const void_allocator &alloc)
        : end_station_interface_t(alloc), config_list(alloc)
        {}
    interface_configuration_t(std::string macAddress, std::string interfaceName, const void_allocator &alloc);
    int addConfig(config_t config);
};

typedef boost::interprocess::allocator<interface_configuration_t, segment_manager_t>                    interface_configuration_t_allocator;
typedef boost::interprocess::list<interface_configuration_t, interface_configuration_t_allocator>       interface_list_t;
/*
    group-status-talker-listener
*/
struct status_talker_listener_t {
    int accumulated_latency;
    interface_list_t interface_configuration;
    status_talker_listener_t(const void_allocator &alloc)
        : interface_configuration(alloc)
        {}
    int addInterfaceConfiguration(interface_configuration_t interface);
};

struct listeners_status_t : public status_talker_listener_t {
    int listener_id;
    listeners_status_t(const void_allocator &alloc)
        : status_talker_listener_t(alloc)
        {}
};

struct talkers_status_t : public status_talker_listener_t {
    int talker_id;
    talkers_status_t(const void_allocator &alloc)
        : status_talker_listener_t(alloc)
        {}
};

typedef boost::interprocess::allocator<listeners_status_t, segment_manager_t>                   listeners_status_t_allocator;
typedef boost::interprocess::list<listeners_status_t, listeners_status_t_allocator>             listeners_status_list_t;
/*
    List of interfaces: need to compare original interfaces?
    Failed interfaces are no pointers, they come from the CNC
*/
class stream_t {
    public:
    stream_id_t stream_id;
    talkers_status_t talkerStatus;
    listeners_status_list_t listenerStatusList;
    status_info_t status_info;
    end_station_interface_list_t failedInterfacesList;
    stream_t(const void_allocator &alloc)
        : stream_id(alloc), talkerStatus(alloc), listenerStatusList(alloc), failedInterfacesList(alloc)
        {}
    int setStreamId(std::string streamId);
    int setTalkerStatus(talkers_status_t talkerStatus);
    int addListenerStatus(listeners_status_t listenerStatus);
    int setStatusInfo(status_info_t statusInfo);
    int addFailedInterface(end_station_interface_t failedInterface);
    void printData();
    int getId();
};

typedef boost::interprocess::allocator<device_t, segment_manager_t>                                 device_t_allocator;
typedef boost::interprocess::list<device_t, device_t_allocator>                                     device_list_t;
typedef boost::interprocess::allocator<talker_t, segment_manager_t>                                 talker_t_allocator;
typedef boost::interprocess::list<talker_t, talker_t_allocator>                                     talker_list_t;
typedef boost::interprocess::allocator<listener_t, segment_manager_t>                               listener_t_allocator;
typedef boost::interprocess::list<listener_t, listener_t_allocator>                                 listener_list_t;
typedef boost::interprocess::allocator<stream_t, segment_manager_t>                                 stream_t_allocator;
typedef boost::interprocess::list<stream_t, stream_t_allocator>                                     streams_list_t;
/*
    Overall structure for the whole module in the shared memory
    has shared memory lists of devices, talkers and listeners
*/
class module_t{
    public:
    device_list_t devicesList;
    talker_list_t talkersList;
    listener_list_t listenersList;
    streams_list_t streamsList;
    int addTalker(talker_t talker);
    int addListener(listener_t listener);
    int addStream(stream_t stream);
    module_t(void_allocator &alloc)
        : devicesList(alloc), talkersList(alloc), listenersList(alloc), streamsList(alloc)
        {}
};