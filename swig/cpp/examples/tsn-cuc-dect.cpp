#ifndef TSN_CUC_DECT
#define TSN_CUC_DECT
#include "tsn-cuc-dect.hpp"
#endif

using namespace std;

/*
    Helpers
*/
template <class T>
int removeEntryFromList(List<T> *list, uint entry)
{
    if ((list->size() <= entry) || (entry < 0))
        return 0;
    typename List<T>::iterator it = list->begin();
    for (uint i = 0; i < entry; i++)
        it++;
    list->erase(it);
    return 1;
}
template <class T>
int removeIdFromList(List<T> *list, int id)
{
    auto it = list->begin();
    for (uint i = 0; i<list->size(); i++)
    {
        if ((*it).getId() == id)
        {
            list->erase(it);
            return 1;
        }
    }
    return 0;
}
template <class T>
int removeIndexFromList(List<T> *list, int index)
{
    auto it = list->begin();
    for (uint i = 0; i<list->size(); i++)
    {
        if ((*it).index == index)
        {
            list->erase(it);
            return 1;
        }
    }
    return 0;
}
template <class T>
int removeInterfaceNameFromList(std::list<T> &list, std::string name)
{
    auto it = list.begin();
    for (uint i = 0; i<list.size(); i++)
    {
        if ((*it).interface_name.compare(name) == 0)
        {
            list.erase(it);
            return 1;
        }
        it++;
    }
    return 0;
}
template <class T>
bool contains(std::list<T> list, T x)
{
	for (typename std::list<T>::iterator it = list.begin() ; it != list.end() ; ++it)
	{
		if (*it == x)
			return true;
	}
	return false;
}
bool containsInterfaceName(List<end_station_interface_t> *list, end_station_interface_t interface)
{
    auto it = list->begin();
    for (uint i = 0; i<list->size(); i++)
    {
        if ((*it).interface_name.compare(interface.interface_name) == 0)
            return true;
        it++;
    }
    return false;
}

bool isxnumber(std::string input)
{
    for (uint i = 0; i<input.size(); i++)
    {
        if (!(isxdigit(input[i])))
            return false;
    }
    return true;
}

/*
    @brief read the given ID, check if it exists in the device list and set the iterator
    @param[in] int input id
    @param[in,out] references to devices list and the iterator
    @return 0 for failure, 1 if successfull
*/
int checkId(int id, List<device_t> *devices, List<device_t>::iterator &it)
{
    it = devices->begin();
    for (uint i = 0; i<devices->size(); i++)
        {
            if (id == ((device_t)(*it)).getId())
                return 1;
            it++;
        }
    #ifndef CATCH_CONFIG_RUNNER
    std::cout << "Device ID not found\n";
    #endif
    return 0;
}

/*
    @brief safely read int from cin
    @param[in] std::string input string from cin
    @param[out] int reference for read integer
    @return: 1 for success, 0 for fail
*/
int readInt(std::string input, int &converted)
{
    try{
        converted = stoi(input);
    }    
    catch (const std::invalid_argument& ia) {
        #ifndef CATCH_CONFIG_RUNNER
        std::cerr << "Invalid Input: " << ia.what() << '\n';
        #endif
        return 0;
    }
    return 1;
}

/*
    @brief check if input is compliant to number mac address separated by :
    @param[in] input string to check
    @param[out] mac_t mac address i correct format
    @return 0 when non-compliant, 1 when compliant
*/
int readMac(const char* input)
{
    int i = 0;
    int s = 0;
    while (*input) 
    {
        if (isxdigit(*input)) 
            i++;
        else if (*input == ':') 
        {
            if (i == 0 || i / 2 - 1 != s)
                break;
            ++s;
        }
        else
            s = -1;
        ++input;
    }
    if (!(i == 12 && (s == 5 || s == 0)))
        return 0;
    return 1;
}

/*
    @brief check if input is compliant to an ipv4 address separated by .
    @param[in] input string to check
    @return 0 if fail, 1 if compliant
*/
int readIpv4(std::string input)
{
    int pos, number;
    std::string part;
    for (int i = 0; i<3; i++)
    {
        pos = input.find_first_of('.');
        if (pos == -1)
            return 0;
        part = input.substr(0,pos);
        input = input.substr(pos+1);
        try {
            number = stoi(part);
        } catch (const std::invalid_argument& ia) {
            #ifndef CATCH_CONFIG_RUNNER
            std::cerr << "Invalid argument in input: " << ia.what() << '\n';
            #endif
            return 0;
        }
        if ((number<0) || (number>255))
            return 0;
    }
    try {
        number = stoi(input);
    } catch (const std::invalid_argument& ia) {
        #ifndef CATCH_CONFIG_RUNNER
        std::cerr << "Invalid argument in input: " << ia.what() << '\n';
        #endif
        return 0;
    }
    if ((number<0) || number>255)
        return 0;
    return 1;
}

/*
    @brief check if input is compliant to an ipv6 address separated by :
    leading 0 can be omitted, groups with 0 can be omitted by :: (JUST ONCE!)
    example: FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF
    @param[in,out] input string to be processed and completed with 0s
    @return 0 for failure, 1 for success
*/
int readIpv6(std::string input)
{
    int pos = 0, pos2 = 0, groups = 0, missing = -1;
    std::string part;
    std::string output;
    while(true)
    {
        pos = input.find_first_of(':');
        pos2 = input.find(':', pos+1);
        if (pos == -1)
            break;
        if (pos > 4)
            return 0;
        part = input.substr(0,pos);
        if ((pos2-pos) == 1)
        {
            if (missing != -1)
                return 0;
            missing = groups;
            input = input.substr(pos2+1);   
            if (input.size() == 0)
                input.append(":0000");       
        }
        else
        {
            input = input.substr(pos+1);            
        }
        if (!isxnumber(part))
            return 0;
        for (int i = 0; i<4-pos; i++)
            output.append("0");
        output.append(part);
        output.append(":");
        groups++;
    }
    if (!isxnumber(input))
        return 0;
    if (input.size() > 4)
        return 0;
    output.append(input);
    groups++;
    std::string padding;
    for (int i = 0; i<(8-groups); i++)
        padding.append("0000:");
    output.insert(5*(missing+1),padding);
    input.assign(output);
    return 1;
}

/*
    @brief get number list of int values out of number single input with comma between thems
    @param[in] input string from cin
    @param[out] list of integers
    @param[out] counter as number of single int values taken
    @return 0 when successful
*/
int getList(int &counter, std::list<int> &list, std::string input)
{
    list.clear();
    counter = 0;
    if (input.length() == 0)
        return 0;
    std::string substr;
    int pos = input.find_first_of(",");
    bool success = false;
    while (pos != -1)
    {
        substr = input.substr(0,pos);
        if ((int)input.size() > pos)
            input = input.substr(pos+1);
        else
            break;
        if (substr.length() == 0)
            break;
        try{
            list.push_back(stoi(substr));
            counter++;
        }    
        catch (const std::invalid_argument& ia) {
            #ifndef CATCH_CONFIG_RUNNER
            std::cerr << "Invalid argument in input: " << ia.what() << '\n';
            #endif
            break;
        }
        pos = input.find_first_of(",");   
        if (pos == -1)
        {
            if ((int)input.size() > pos)
                input = input.substr(pos+1);
            else
                break;
            try{
                list.push_back(stoi(input));
                counter++;
            }    
            catch (const std::invalid_argument& ia) {
                #ifndef CATCH_CONFIG_RUNNER
                std::cerr << "Invalid argument in input: " << ia.what() << '\n';
                #endif
                break;
            }
            success = true;
        }
    }
    if (!success)
    {
        list.clear();
        counter = 0;
        return 0;
    }
    return 1;
}

/*
    @brief return the iterator to number certain item
    @param[in] reference to the list, independent of its template; int itemnumber of the element to delete
    @param[out] reference to iterator that points to the selected item in the list
*/
template <class T>
int getItemFromList(std::list<T> list, typename std::list<T>::iterator &it, int itemnumber)
{
    if ((itemnumber < 0) || ((uint)itemnumber+1 > list.size()))
        return 0;
    it = list.begin();
    while (itemnumber>0)
    {
        it++;
        itemnumber--;
    }
    return 1;
}

/*
    @brief print an entire list
    @param[in] reference to the list, independent of its template
    @return 1 when success, 0 when fail
*/
template <class T>
int printIntList(std::list<T> list)
{
    auto it = list.begin();
    if (list.size() == 0)
        return 0;
    it = list.begin();
    for (uint i = 0; i<list.size(); i++)
    {
        #ifndef CATCH_CONFIG_RUNNER
        std::cout << i << ": " << (int)(*it) << "\n";
        #endif
        it++;
    }
    return 1;
}

/*
    @brief: safely get the interval data type from input
    @param[in] input string to process
    @return interval data type
*/
interval_t getInterval(std::string input)
{
    int pos = input.find('-');
    if (pos == -1)
    {
        #ifndef CATCH_CONFIG_RUNNER
        std::cout << "No - char between num and den\n";
        #endif
        return interval_t();
    }
    std::string num = input.substr(0,pos);
    std::string den = input.substr(++pos);
    if ((num.size() == 0) || (den.size() == 0))
    {
        #ifndef CATCH_CONFIG_RUNNER
        std::cout << "No num or no den\n";
        #endif
        return interval_t();
    }
    interval_t interval;
    interval.numerator = stoi(num);
    interval.denominator = stoi(den);
    return interval;
}

/*
    Class functions
*/
/*
    @brief constructor for specification, entry gets assigned when added to list
*/
data_frame_specification_t::data_frame_specification_t(choice_t choice) 
{
    this->index = -1;
    this->choice = choice;
}

/*
    @brief constructor for listener
*/
listener_t::listener_t(int id, user_to_network_requirements_t user_to_network_requirements, interface_capabilities_t interface_capabilities)
{
    this->id = id;
    this->user_to_network_requirements = user_to_network_requirements;
    this->interface_capabilities = interface_capabilities;
}

/*
    @brief constructor for talker, includes two empty lists
*/
talker_t::talker_t(int id, int rank, traffic_specification_t traffic_specification,
    user_to_network_requirements_t user_to_network_requirements, interface_capabilities_t interface_capabilities)
{
    this->id = id;
    this->stream_rank.rank = rank;
    this->traffic_specification = traffic_specification;
    this->user_to_network_requirements = user_to_network_requirements;
    this->interface_capabilities = interface_capabilities;
    this->end_station_interface_list->clear();
    this->data_frame_specification_list->clear();
}
/*
    @brief dummy constructor
*/
talker_t::talker_t()
{

}
talker_t::talker_t(List<data_frame_specification_t> *data_frame_specification_list, List<end_station_interface_t> *end_station_interface_list)
{
    this->data_frame_specification_list = data_frame_specification_list;
    this->end_station_interface_list = end_station_interface_list;
    // int id = -1;
    // int rank = -1;
    // traffic_specification_t traffic_specification;
    // interval_t interval;
    // traffic_specification.interval = interval;
    // traffic_specification.max_frame_size = -1;
    // traffic_specification.max_frames_per_interval = -1;
    // traffic_specification.time_aware.earliest_transmit_offset = -1;
    // traffic_specification.time_aware.jitter = -1;
    // traffic_specification.time_aware.latest_transmit_offset = -1;
    // traffic_specification.transmission_selection = -1;
    // user_to_network_requirements_t user_to_network_requirements;
    // user_to_network_requirements.max_latency = -1;
    // user_to_network_requirements.num_seamless_trees = -1;
    // interface_capabilities_t interface_capabilities;
    // interface_capabilities.vlan_tag_capable = false;
    // this->id = id;
    // this->stream_rank.rank = rank;
    // this->traffic_specification = traffic_specification;
    // this->user_to_network_requirements = user_to_network_requirements;
    // this->interface_capabilities = interface_capabilities;
    // this->end_station_interface_list->clear();
    // this->data_frame_specification_list->clear();
}

/*
    @brief constructor for status_stream
*/
status_stream_t::status_stream_t(std::string stream_id, status_info_t status_info)
{
    this->stream_id = stream_id;
    this->status_info = status_info;
}

/*
    @brief Add the failed interface
*/
int status_stream_t::add_interface(end_station_interface_t failedInterface)
{
    this->failedInterfacesList.push_back(failedInterface);
    return 1;
}


/* 
    end station functions
*/
/* 
    @brief add number new interface to the list. 
    @param[in] address of end_station_interfaces_t interface to number certain interface
*/
int end_station_t::add_interface(end_station_interface_t interface)
{
    if (containsInterfaceName(this->end_station_interface_list, interface))
        return 0;
    this->end_station_interface_list->push_back(interface);
    return 1;
}
int end_station_t::getId()
{
    return this->id;
}

/*
    Talker_t functions
*/
/*
    @brief add number new data frame specification to the list.
    @param[in] specification
*/
int talker_t::add_specification(data_frame_specification_t specification)
{
    specification.index = this->data_frame_specification_list->size();
    this->data_frame_specification_list->push_back(specification);
    return 1;
}
void talker_t::printData()
{
    cout << "\n!---- PRINTING TALKER DATA ----!\n\n";
    cout << "\ttalker-id: " << this->id << "\n";
    cout << "\tstream-rank:\n\t\trank: " << this->stream_rank.rank << "\n";
    cout << "\tend-station-interfaces:\n";
    for (List<end_station_interface_t>::iterator it = this->end_station_interface_list->begin(); 
        it != this->end_station_interface_list->end(); it++)
        cout << "\t\t" << std::distance(this->end_station_interface_list->begin(), it) << "\n\t\t\tname: " 
            << (*it).interface_name << "\n\t\t\taddress " << (*it).mac_address << "\n"; 
    cout << "\tdata-frame-specification:\n";
    for (List<data_frame_specification_t>::iterator it = this->data_frame_specification_list->begin();
        it != this->data_frame_specification_list->end(); it++)
        if ((*it).choice.field == MAC)
            cout << "\t\tindex: " << (*it).index << "\n\t\t\tieee802-mac-addresses\n\t\t\t\tsource_mac_address: " << 
                (*it).choice.str1 << "\n\t\t\t\tdestination_mac_address: "<< (*it).choice.str2 << "\n";
        else if ((*it).choice.field == VLAN)
            cout << "\t\tindex: " << (*it).index << "\n\t\t\tieee802-vlan-tag\n\t\t\t\tpcp: " << (*it).choice.val1 << 
                "\n\t\t\t\tvlan_id: " << (*it).choice.val2 << "\n";
        else if ((*it).choice.field == IPV4)
            cout << "\t\tindex: " << (*it).index << "\n\t\t\tipv4-tuple\n\t\t\t\tipv4_source_ip_address: " << (*it).choice.str1 << 
                "\n\t\t\t\tipv4_destination_ip_address: "<< (*it).choice.str2 << "\n\t\t\t\tdscp: " << (*it).choice.val1 << 
                "\n\t\t\t\tprotocol: " << (*it).choice.val2 << "\n\t\t\t\tsource_port: " << (*it).choice.val3 << 
                "\n\t\t\t\tdestination_port: " << (*it).choice.val4 << "\n";
        else if ((*it).choice.field == IPV6)
            cout << "\t\tindex: " << (*it).index << "\n\t\t\tipv6-tuple\n\t\t\t\tipv6_source_ip_address: " << (*it).choice.str1 << 
                "\n\t\t\t\tipv6_destination_ip_address: "<< (*it).choice.str2 << "\n\t\t\t\tdscp: " << (*it).choice.val1 << 
                "\n\t\t\t\tprotocol: " << (*it).choice.val2 << "\n\t\t\t\tsource_port: " << (*it).choice.val3 << 
                "\n\t\t\t\tdestination_port: " << (*it).choice.val4 << "\n";
        else
            break;
    cout << "\ttraffic-specification:\n";
    cout << "\t\tinterval: " << this->traffic_specification.interval.numerator << "/" << this->traffic_specification.interval.denominator << "\n";
    cout << "\t\tmax-frames-per-interval: " << this->traffic_specification.max_frames_per_interval << "\n";
    cout << "\t\tmax-frame-size: " << this->traffic_specification.max_frame_size << "\n";
    cout << "\t\ttransmission-selection: " << this->traffic_specification.transmission_selection << "\n";
    cout << "\t\ttime-aware:\n\t\t\tearliest transmit offset: " << this->traffic_specification.time_aware.earliest_transmit_offset << 
        "\n\t\t\tlatest-transmit-offset: " << this->traffic_specification.time_aware.latest_transmit_offset << "\n\t\t\tjitter: " << 
        this->traffic_specification.time_aware.jitter << "\n";
    cout << "\tuser-to-network-requirements:\n";
    cout << "\t\tnum-seamless-trees: " << this->user_to_network_requirements.num_seamless_trees << "\n\t\tmax-latency: " <<
        this->user_to_network_requirements.max_latency << "\n";
    cout << "\tinterface-capabilities:\n";
    cout << "\t\tvlan-tag-capable: " << this->interface_capabilities.vlan_tag_capable << "\n";
    cout << "\t\tcb-stream-iden-type-list:\n";
    for (std::list<int>::iterator it = this->interface_capabilities.cb_stream_iden_type_list.begin();
        it != this->interface_capabilities.cb_stream_iden_type_list.end(); it++)
        cout << "\t\t\t" << std::distance(this->interface_capabilities.cb_stream_iden_type_list.begin(), it) <<
            (*it) << "\n";
    cout << "\t\tcb-sequence-type-list:\n";
    for (std::list<int>::iterator it = this->interface_capabilities.cb_sequence_type_list.begin();
        it != this->interface_capabilities.cb_sequence_type_list.end(); it++)    
        cout << "\t\t\t" << std::distance(this->interface_capabilities.cb_sequence_type_list.begin(), it) <<
            (*it) << "\n";
    cout << "\n!---- END OF TALKER DATA ---!\n";
}

/*
    Device_t functions
*/
int device_t::count = 0;
/*

*/
device_t::device_t(std::string name, pmid_t pmid)
{
    this->id = this->count;
    this->count++;
    this->name.append(name.c_str());
    this->pmid = pmid;
}
boost::container::string device_t::getName()
{
    return this->name;
}
int device_t::getId()
{
    return this->id;
}
pmid_t device_t::getPmid()
{
    return this->pmid;
}

/*
    Module_t functions
*/
// module_t::module_t(List<device_t> *devicesList, List<talker_t> *talkersList, boost::interprocess::managed_shared_memory managed_shm)
module_t::module_t(List<device_t> *devicesList, List<talker_t> *talkersList)
// module_t::module_t(boost::interprocess::managed_shared_memory &managed_shm, boost::interprocess::managed_shared_memory::segment_manager *mgr)
{
    // List<device_t> *devicesList = managed_shm.construct<List<device_t>>("devicesList")(mgr);
    // List<talker_t> *talkersList = managed_shm.construct<List<talker_t>>("talkersList")(mgr);
    this->devicesList = devicesList;
    this->talkersList = talkersList;
}
// module_t::module_t(List<device_t> *devicesList)
// {
//     this->devicesList = devicesList;
// }
int module_t::addDevice(device_t device)
{
    this->devicesList->push_back(device);
    return 1;
}

int module_t::removeDevice(int id)
{
    return removeIdFromList(this->talkersList, id);
}

int module_t::addTalker(talker_t talker)
{
    this->talkersList->push_back(talker);
    return 1;
}

int module_t::removeTalker(int id)
{
    return removeIdFromList(this->talkersList, id);
}

int module_t::addListener(listener_t listener)
{
    this->listenersList->push_back(listener);
    return 1;
}

int module_t::removeListener(int id)
{
    return removeIdFromList(this->listenersList, id);
}

/*
    SHM functions
*/
data_frame_specification_t_shm::data_frame_specification_t_shm(choice_t_shm choice) 
{
    this->index = -1;
    this->choice = choice;
}


void talker_t_shm::printData()
{
    cout << "\n!---- PRINTING TALKER DATA ----!\n\n";
    cout << "\ttalker-id: " << this->id << "\n";
    cout << "\tstream-rank:\n\t\trank: " << this->stream_rank.rank << "\n";
    cout << "\tend-station-interfaces:\n";
    for (auto it = this->end_station_interface_list.begin(); 
        it != this->end_station_interface_list.end(); it++)
        cout << "\t\t" << std::distance(this->end_station_interface_list.begin(), it) << "\n\t\t\tname: " 
            << (*it).interface_name << "\n\t\t\taddress " << (*it).mac_address << "\n"; 
    cout << "\tdata-frame-specification:\n";
    for (auto it = this->data_frame_specification_list.begin();
        it != this->data_frame_specification_list.end(); it++)
        if ((*it).choice.field == MAC)
            cout << "\t\tindex: " << (*it).index << "\n\t\t\tieee802-mac-addresses\n\t\t\t\tsource_mac_address: " << 
                (*it).choice.str1 << "\n\t\t\t\tdestination_mac_address: "<< (*it).choice.str2 << "\n";
        else if ((*it).choice.field == VLAN)
            cout << "\t\tindex: " << (*it).index << "\n\t\t\tieee802-vlan-tag\n\t\t\t\tpcp: " << (*it).choice.val1 << 
                "\n\t\t\t\tvlan_id: " << (*it).choice.val2 << "\n";
        else if ((*it).choice.field == IPV4)
            cout << "\t\tindex: " << (*it).index << "\n\t\t\tipv4-tuple\n\t\t\t\tipv4_source_ip_address: " << (*it).choice.str1 << 
                "\n\t\t\t\tipv4_destination_ip_address: "<< (*it).choice.str2 << "\n\t\t\t\tdscp: " << (*it).choice.val1 << 
                "\n\t\t\t\tprotocol: " << (*it).choice.val2 << "\n\t\t\t\tsource_port: " << (*it).choice.val3 << 
                "\n\t\t\t\tdestination_port: " << (*it).choice.val4 << "\n";
        else if ((*it).choice.field == IPV6)
            cout << "\t\tindex: " << (*it).index << "\n\t\t\tipv6-tuple\n\t\t\t\tipv6_source_ip_address: " << (*it).choice.str1 << 
                "\n\t\t\t\tipv6_destination_ip_address: "<< (*it).choice.str2 << "\n\t\t\t\tdscp: " << (*it).choice.val1 << 
                "\n\t\t\t\tprotocol: " << (*it).choice.val2 << "\n\t\t\t\tsource_port: " << (*it).choice.val3 << 
                "\n\t\t\t\tdestination_port: " << (*it).choice.val4 << "\n";
        else
            break;
    cout << "\ttraffic-specification:\n";
    cout << "\t\tinterval: " << this->traffic_specification.interval.numerator << "/" << this->traffic_specification.interval.denominator << "\n";
    cout << "\t\tmax-frames-per-interval: " << this->traffic_specification.max_frames_per_interval << "\n";
    cout << "\t\tmax-frame-size: " << this->traffic_specification.max_frame_size << "\n";
    cout << "\t\ttransmission-selection: " << this->traffic_specification.transmission_selection << "\n";
    cout << "\t\ttime-aware:\n\t\t\tearliest transmit offset: " << this->traffic_specification.time_aware.earliest_transmit_offset << 
        "\n\t\t\tlatest-transmit-offset: " << this->traffic_specification.time_aware.latest_transmit_offset << "\n\t\t\tjitter: " << 
        this->traffic_specification.time_aware.jitter << "\n";
    cout << "\tuser-to-network-requirements:\n";
    cout << "\t\tnum-seamless-trees: " << this->user_to_network_requirements.num_seamless_trees << "\n\t\tmax-latency: " <<
        this->user_to_network_requirements.max_latency << "\n";
    cout << "\tinterface-capabilities:\n";
    cout << "\t\tvlan-tag-capable: " << this->interface_capabilities.vlan_tag_capable << "\n";
    cout << "\t\tcb-stream-iden-type-list:\n";
    for (auto it = this->interface_capabilities.cb_stream_iden_type_list.begin();
        it != this->interface_capabilities.cb_stream_iden_type_list.end(); it++)
        cout << "\t\t\t" << std::distance(this->interface_capabilities.cb_stream_iden_type_list.begin(), it) <<
            " : " << (*it) << "\n";
    cout << "\t\tcb-sequence-type-list:\n";
    for (auto it = this->interface_capabilities.cb_sequence_type_list.begin();
        it != this->interface_capabilities.cb_sequence_type_list.end(); it++)    
        cout << "\t\t\t" << std::distance(this->interface_capabilities.cb_sequence_type_list.begin(), it) <<
            " : " << (*it) << "\n";
    cout << "\n!---- END OF TALKER DATA ---!\n";
}