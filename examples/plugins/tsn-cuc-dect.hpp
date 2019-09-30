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
typedef std::string mac_t;
/* type for ipv4 addresses from ietf-inet-types YANG Module
*/
typedef std::string ipv4_address_t;

/* type for ipv6 addresses
*/
typedef std::string ipv6_address_t;

/* choice for data frame specification */
enum field {MAC, VLAN, IPV4, IPV6};

/* type for end station interface list in groups 
uses group-interface-id*/
struct end_station_interface_t {
    mac_t mac_address;
    std::string interface_name;
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
    std::string str1;
    std::string str2;
    mac_t* source_mac_address = &str1;
    ipv4_address_t* ipv4_source_ip_address = &str1;
    ipv6_address_t* ipv6_source_ip_address = &str1;
    std::string* dest_mac_address = &str2;
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
    std::string name;
    pmid_t pmid;
    public:
    boost::container::string name2;
    int id;
    device_t(std::string name, pmid_t pmid);
    std::string getName();
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
    std::list<end_station_interface_t> end_station_interface_list;
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
    std::list<data_frame_specification_t> data_frame_specification_list;
    traffic_specification_t traffic_specification;
    talker_t(int id, int rank, traffic_specification_t traffic_specification,
        user_to_network_requirements_t user_to_network_requirements, 
        interface_capabilities_t interface_capabilities);
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
    std::list<data_frame_specification_t> config_values;
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
    std::list<device_t> devicesList;
    std::list<talker_t> talkersList;
    std::list<listener_t> listenersList;
    std::list<status_stream_t> streamsList;
    // public:
    int addDevice(device_t device);
    int removeDevice(int id);
    int addTalker(talker_t talker);
    int removeTalker(int id);
    int addListener(listener_t listener);
    int removeListener(int id);
};