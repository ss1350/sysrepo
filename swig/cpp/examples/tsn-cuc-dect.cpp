#ifndef TSN_CUC_DECT
#define TSN_CUC_DECT
#include "tsn-cuc-dect.hpp"
#endif

using namespace std;

/*
    Helpers
*/
int removeEntryFromList(end_station_interface_list_t &list, uint entry)
{
    if ((list.size() <= entry) || (entry < 0))
        return 0;
    auto it = list.begin();
    for (uint i = 0; i < entry; i++)
        it++;
    list.erase(it);
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
int removeIndexFromList(data_frame_specification_list_t &list, int index)
{
    auto it = list.begin();
    for (uint i = 0; i<list.size(); i++)
    {
        if ((*it).index == index)
        {
            list.erase(it);
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

bool uintListContainsUint(uint_list list, uint x)
{
	for (auto it = list.begin() ; it != list.end() ; it++)
	{
		if (*it == x)
			return true;
	}
	return false;
}


bool containsInterfaceName(end_station_interface_list_t &list, end_station_interface_t interface)
{
    auto it = list.begin();
    for (uint i = 0; i<list.size(); i++)
    {
        // if ((*it).interface_name->compare(*(interface.interface_name)) == 0)
        if (it->interface_name.compare(interface.interface_name) == 0)
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
    #endif
    return 0;
}

/*
    @brief safely read int from cin
    @param[in] std::string input string from cin
    @param[out] int reference for read integer
    @return: 1 for success, 0 for fail
*/
int readInt(int &converted)
{
    std::string input;
    cin >> input;
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

int readInt(uint &converted)
{
    std::string input;
    cin >> input;
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
    @brief check if input is compliant to number mac address separated by -
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
        else if (*input == '-') 
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
int readIpv6(std::string &input)
{
    int pos = 0, pos2 = 0, groups = 0, missing = -1;
    std::string part;
    std::string output;
    while(true)
    {
        // find two consecutive :
        pos = input.find_first_of(':');
        pos2 = input.find(':', pos+1);
        // if : not found -> last part or error
        if (pos == -1)
            break;
        // if space to : too big -> error 
        if (pos > 4)
            return 0;
        // substring from beginning to first :
        part = input.substr(0,pos);
        // if :: -> omitted 0s
        if ((pos2-pos) == 1)
        {
            // only one time omit allowed. else -> error
            if (missing != -1)
                return 0;
            // how many groups are already in the output string? (place for padding!)
            missing = groups;
            // rest of input as new substring
            input = input.substr(pos2+1);   
            // if substring empty: no input after :: therefore 0s group
            if (input.size() == 0)
                input.append(":0000");       
        }
        else
        {
            // rest of input is substring
            input = input.substr(pos+1);            
        }
        // check if the part is hexadecimal
        if (!isxnumber(part))
            return 0;
        // if the part does not have 4 hex numbers, padding for the rest!
        for (int i = 0; i<4-pos; i++)
            output.append("0");
        output.append(part);
        output.append(":");
        // increment number of groups that are added to the output
        groups++;
    }
    // check for hex number or wrong size of group
    if (!isxnumber(input))
        return 0;
    if (input.size() > 4)
        return 0;
    // check if padding for last group is needed
    else if ((input.size() != 0) && (input.size() < 4))
    {
        uint size = input.size();
        // does not work! how to pad before end?
        for (uint i = 0; i<(4-size); i++)
            output.append("0");
    }
    output.append(input);
    groups++;
    // padding for omitted groups
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
int getItemFromList(uint_list list, uint_list::iterator &it, int itemnumber)
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

int printIntList(uint_list list)
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
    @brief constructor for end_station_interface with mac check
    @param[in] std::string macAddress and interfaceName
*/
end_station_interface_t::end_station_interface_t(std::string macAddress, std::string interfaceName, const char_allocator &alloc)
    : mac_address(alloc), interface_name(alloc)
{
    if (readMac(macAddress.c_str()))
    {
        this->mac_address.assign(macAddress.c_str());
        this->interface_name.assign(interfaceName.c_str());
    }
}

/*
    @brief constructor for end_station_t
    @param[in] int id, void_allocator alloc
*/
end_station_t::end_station_t(int id, const void_allocator &alloc)
    : id(id), interface_capabilities(alloc), end_station_interface_list(alloc)
{

}

/*
    @brief set the id
    @param[in] int id
    @return 1 when successful
*/
int end_station_t::setId(int id)
{
    this->id = id;
    return 1;
}

/*
    @brief get the id
    @return int id
*/
int end_station_t::getId()
{
    return this->id;
}

/* 
    @brief add number new interface to the list. 
    @param[in] address of end_station_interfaces_t interface to number certain interface
*/
int end_station_t::addInterface(end_station_interface_t interface)
{
    if (containsInterfaceName(this->end_station_interface_list, interface))
        return 0;
    this->end_station_interface_list.push_back(interface);
    return 1;
}

/*
    @brief remove an interface
    @param[in] iterator to interface in list that should be removed
    @return 1 if found and removed, 0 else
*/
int end_station_t::removeInterface(end_station_interface_list_t::iterator it)
{
    for (end_station_interface_list_t::iterator i = this->end_station_interface_list.begin(); 
        i != this->end_station_interface_list.end(); i++)
    {
        if (i == it)
        {
            this->end_station_interface_list.erase(it);
            return 1;
        }
    }
    return 0;
}

/*
    @brief add a CB Sequence Type. Check if it is a 32 bit unsigned integer and if it is already there
    @param[in] integer for the type
*/
int end_station_t::addCBSequenceType(uint type)
{
    if (type > 4294967295)
        return 0;
    if (uintListContainsUint(this->interface_capabilities.cb_sequence_type_list, type))
        return 0;
    this->interface_capabilities.cb_sequence_type_list.push_back(type);
    return 1;
}

/*
    @brief add a CB Stream Iden Type. Check if it is a 32 bit unsigned integer and if it is already there
    @param[in] integer for the type
*/
int end_station_t::addCBStreamIdenType(uint type)
{    
    if (type > 4294967295)
        return 0;
    if (uintListContainsUint(this->interface_capabilities.cb_stream_iden_type_list, type))
        return 0;
    this->interface_capabilities.cb_stream_iden_type_list.push_back(type);
    return 1;
}

/*
    @brief
*/
int end_station_t::setUserToNetworkRequirements(int maxLatency, int numSeamlessTrees)
{
    this->user_to_network_requirements.max_latency = maxLatency;
    this->user_to_network_requirements.num_seamless_trees = numSeamlessTrees;
    return 1;
}


/*
    Constructors for choice_t
*/
/*
    @brief constructor for VLAN choice_t
    @param[in] integers pcp, vlan_id, alloc
*/
choice_t::choice_t(uint pcp, int vlan_id, const char_allocator &alloc)
    : str1(alloc), str2(alloc)
{
    // if (!((pcp >=0 ) && (pcp <= 7) && (vlan_id > 0)))
    //     return;
    this->field = VLAN;
    *this->pcp = pcp;
    *this->vlan_id = vlan_id;
}
/*
    @brief constructor for MAC choice_t
    @param[in] std::strings mac source, mac dest
*/
choice_t::choice_t(std::string mac_source, std::string mac_dest, const char_allocator &alloc)
    : str1(alloc), str2(alloc)
{
    // if (!((readMac(str1.c_str())) && readMac(str2.c_str())))
    //     return;
    this->field = MAC;
    this->source_mac_address->assign(mac_source.c_str());
    this->destination_mac_address->assign(mac_dest.c_str());
}
/*
    @brief constructor for IP choice_t. Auto detect ipv4 or 6
    @param[in] std::strings for source and dest address, ints for source/dest port, dscp and protocol
*/
choice_t::choice_t(std::string ipSource, std::string ipDest, int sourcePort, int destPort, int dscp, int protocol, const char_allocator &alloc)
    : str1(alloc), str2(alloc)
{
    if ((readIpv4(ipSource)) && (readIpv4(ipDest)))
    {
        this->field = IPV4;
        this->ipv4_source_ip_address->assign(ipSource.c_str());
        this->ipv4_destination_ip_address->assign(ipSource.c_str());
        *this->source_port = sourcePort;
        *this->destination_port = destPort;
        *this->dscp = dscp;
        *this->protocol = protocol;
    }
    else if ((readIpv6(ipSource)) && (readIpv6(ipDest)))
    {
        this->field = IPV6;
        this->ipv6_source_ip_address->assign(ipSource.c_str());
        this->ipv6_destination_ip_address->assign(ipDest.c_str());
        *this->source_port = sourcePort;
        *this->destination_port = destPort;
        *this->dscp = dscp;
        *this->protocol = protocol;
    }
    else
        return;
}
/*    
    @brief constructor for TAO choice_t.
    @param[in] uint timeAwareOffset
*/
choice_t::choice_t(uint timeAwareOffset, const char_allocator &alloc)
    : str1(alloc), str2(alloc)
{
    this->field = TAO;
    *this->timeAwareOffset = timeAwareOffset;
}

/*
    Talker_t functions
*/
/*
    @brief add number new data frame specification to the list.
    @param[in] specification
*/
int talker_t::addSpecification(data_frame_specification_t specification)
{
    specification.index = this->data_frame_specification_list.size();
    this->data_frame_specification_list.push_back(specification);
    return 1;
}

/*
    @brief check if rank is valid and check it
    @param[in] int rank
    @return 1 if successful, 0 if not
*/
int talker_t::setRank(int rank)
{
    if ((rank < 0) || (rank > 1))
        return 0;
    this->stream_rank.rank = (uint)rank;
    return 1;
}
int talker_t::setRank(uint rank)
{
    if (rank > 1)
        return 0;
    this->stream_rank.rank = rank;
    return 1;
}

/*
    Device_t functions
*/
int device_t::count = 0;
/*

*/
device_t::device_t(std::string name, const char* pmid, const char_allocator &alloc)
    : pmid(alloc)
{
    this->id = this->count;
    this->count++;
    this->name.append(name.c_str());
    this->pmid.assign(pmid);
}
boost::container::string device_t::getName()
{
    return this->name;
}
int device_t::getId()
{
    return this->id;
}
const char* device_t::getPmid()
{
    return this->pmid.c_str();
}

int module_t::addTalker(talker_t talker)
{
    this->talkersList.push_back(talker);
    return 1;
}

int module_t::addListener(listener_t listener)
{
    this->listenersList.push_back(listener);
    return 1;
}

int module_t::addStream(stream_t stream)
{
    this->streamsList.push_back(stream);
    return 1;
}

int module_t::addDevice(device_t device)
{
    this->devicesList.push_back(device);
    return 1;
}

/*
    SHM functions
*/
void talker_t::printData()
{
    cout << "\n!---- PRINTING TALKER DATA ----!\n\n";
    cout << "\ttalker-id: " << this->getId() << "\n";
    cout << "\tstream-rank:\n\t\trank: " << this->stream_rank.rank << "\n";
    cout << "\tend-station-interfaces:\n";
    for (auto it = this->end_station_interface_list.begin(); 
        it != this->end_station_interface_list.end(); it++)
        cout << "\t\t" << std::distance(this->end_station_interface_list.begin(), it) << "\n\t\t\tname: " 
            << it->interface_name << "\n\t\t\taddress " << it->mac_address << "\n"; 
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

void listener_t::printData()
{
    cout << "\n!---- PRINTING LISTENER DATA ----!\n\n";
    cout << "\ttalker-id: " << this->getId() << "\n";
    cout << "\tend-station-interfaces:\n";
    for (auto it = this->end_station_interface_list.begin(); 
        it != this->end_station_interface_list.end(); it++)
        cout << "\t\t" << std::distance(this->end_station_interface_list.begin(), it) << "\n\t\t\tname: " 
            << it->interface_name << "\n\t\t\taddress " << it->mac_address << "\n"; 
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
    cout << "\n!---- END OF LISTENER DATA ---!\n";
}

void stream_t::printData()
{
    cout << "\n!---- PRINTING STREAM DATA ----!\n\n";
    cout << "\tstream-id: " << this->stream_id << "\n";
    cout << "\ttalker-status:\n";
    cout << "\t\ttalker-id: " << this->talkerStatus.talker_id << "\n";
    cout << "\t\taccumulated-latency: " << this->talkerStatus.accumulated_latency << "\n";
    cout << "\t\tinterface-configuration:\n";
    cout << "\t\t\tinterface-list:\n";
    for (auto it = this->talkerStatus.interface_configuration.begin(); 
        it != this->talkerStatus.interface_configuration.end(); it++)
        {
            cout << "\t\t\t\t" << std::distance(this->talkerStatus.interface_configuration.begin(), it) << 
            "\n\t\t\t\t\tinterface-name: " << it->interface_name << "\n\t\t\t\t\tmac-address: " << it->mac_address <<
            "\n\t\t\t\t\tconfig-list:";
            for (auto i = it->config_list.begin(); i != it->config_list.end(); i++)
            {
                cout << "\n\t\t\t\t\t\tindex: " << i->index << "\n\t\t\t\t\t\tconfig-value:";
                if (i->choice.field == MAC)
                    cout << "\n\t\t\t\t\t\t\tieee802-mac-addresses\n\t\t\t\t\t\t\t\tsource_mac_address: " << 
                        i->choice.str1 << "\n\t\t\t\t\t\t\t\tdestination_mac_address: "<< i->choice.str2 << "\n";
                else if (i->choice.field == TAO)
                    cout << "\n\t\t\t\t\t\t\ttime-aware-offset\n\t\t\t\t\t\t\t\ttime-aware-offset: " << 
                        i->choice.val1 << "\n";
                else if (i->choice.field == VLAN)
                    cout << "\n\t\t\t\t\t\t\tieee802-vlan-tag\n\t\t\t\t\t\t\t\tpcp: " << i->choice.val1 << 
                        "\n\t\t\t\t\t\t\t\tvlan_id: " << i->choice.val2 << "\n";
                else if (i->choice.field == IPV4)
                    cout << "\n\t\t\t\t\t\t\tipv4-tuple\n\t\t\t\t\t\t\t\tipv4_source_ip_address: " << i->choice.str1 << 
                        "\n\t\t\t\t\t\t\t\tipv4_destination_ip_address: "<< i->choice.str2 << "\n\t\t\t\t\t\t\t\tdscp: " << i->choice.val1 << 
                        "\n\t\t\t\t\t\t\t\tprotocol: " << i->choice.val2 << "\n\t\t\t\t\t\t\t\tsource_port: " << i->choice.val3 << 
                        "\n\t\t\t\t\t\t\t\tdestination_port: " << i->choice.val4 << "\n";
                else if (i->choice.field == IPV6)
                    cout << "\n\t\t\t\t\t\t\tipv6-tuple\n\t\t\t\t\t\t\t\tipv6_source_ip_address: " << i->choice.str1 << 
                        "\n\t\t\t\t\t\t\t\tipv6_destination_ip_address: "<< i->choice.str2 << "\n\t\t\t\t\t\t\t\tdscp: " << i->choice.val1 << 
                        "\n\t\t\t\t\t\t\t\tprotocol: " << i->choice.val2 << "\n\t\t\t\t\t\t\t\tsource_port: " << i->choice.val3 << 
                        "\n\t\t\t\t\t\t\t\tdestination_port: " << i->choice.val4 << "\n";
            }
        }
    cout << "\tlisteners-status:\n";
    for (auto it = this->listenerStatusList.begin(); it != listenerStatusList.end(); it++)
    {
        cout << "\t\t\tlistener-id: " << it->listener_id << "\n";
        cout << "\t\t\taccumulated-latency: " << it->accumulated_latency << "\n";
        cout << "\t\t\tinterface-configuration:\n";
        cout << "\t\t\t\tinterface-list:\n";
        for (auto i = it->interface_configuration.begin(); i != it->interface_configuration.end(); i++)
        {
            cout << "\t\t\t\t\t" << std::distance(it->interface_configuration.begin(), i) << 
            "\n\t\t\t\t\t\tinterface-name: " << i->interface_name << "\n\t\t\t\t\t\tmac-address: " << i->mac_address <<
            "\n\t\t\t\t\t\tconfig-list:";
            for (auto j = i->config_list.begin(); j != i->config_list.end(); j++)
            {
                cout << "\n\t\t\t\t\t\t\tindex: " << j->index << "\n\t\t\t\t\t\t\tconfig-value:";
                if (j->choice.field == MAC)
                    cout << "\n\t\t\t\t\t\t\t\tieee802-mac-addresses\n\t\t\t\t\t\t\t\t\tsource_mac_address: " << 
                        j->choice.str1 << "\n\t\t\t\t\t\t\t\t\tdestination_mac_address: "<< j->choice.str2 << "\n";
                else if (j->choice.field == TAO)
                    cout << "\n\t\t\t\t\t\t\t\ttime-aware-offset\n\t\t\t\t\t\t\t\t\ttime-aware-offset: " << 
                        j->choice.val1 << "\n";
                else if (j->choice.field == VLAN)
                    cout << "\n\t\t\t\t\t\t\t\tieee802-vlan-tag\n\t\t\t\t\t\t\t\t\tpcp: " << j->choice.val1 << 
                        "\n\t\t\t\t\t\t\t\t\tvlan_id: " << j->choice.val2 << "\n";
                else if (j->choice.field == IPV4)
                    cout << "\n\t\t\t\t\t\t\t\tipv4-tuple\n\t\t\t\t\t\t\t\t\tipv4_source_ip_address: " << j->choice.str1 << 
                        "\n\t\t\t\t\t\t\t\t\tipv4_destination_ip_address: "<< j->choice.str2 << "\n\t\t\t\t\t\t\t\t\tdscp: " << j->choice.val1 << 
                        "\n\t\t\t\t\t\t\t\t\tprotocol: " << j->choice.val2 << "\n\t\t\t\t\t\t\t\t\tsource_port: " << j->choice.val3 << 
                        "\n\t\t\t\t\t\t\t\t\tdestination_port: " << j->choice.val4 << "\n";
                else if (j->choice.field == IPV6)
                    cout << "\n\t\t\t\t\t\t\t\tipv6-tuple\n\t\t\t\t\t\t\t\t\tipv6_source_ip_address: " << j->choice.str1 << 
                        "\n\t\t\t\t\t\t\t\t\tipv6_destination_ip_address: "<< j->choice.str2 << "\n\t\t\t\t\t\t\t\t\tdscp: " << j->choice.val1 << 
                        "\n\t\t\t\t\t\t\t\t\tprotocol: " << j->choice.val2 << "\n\t\t\t\t\t\t\t\t\tsource_port: " << j->choice.val3 << 
                        "\n\t\t\t\t\t\t\t\t\tdestination_port: " << j->choice.val4 << "\n"; 
            }
        }
    }
    cout << "\tstatus-info:\n\t\ttalker-status: " << this->status_info.talker_status << "\n\t\tlisteners-status: " << 
        this->status_info.listener_status << "\n\t\tfailure-code: " << this->status_info.failure_code << "\n";
    cout << "\tfailed-interfaces:\n";
    for (auto it = this->failedInterfacesList.begin(); it != this->failedInterfacesList.end(); it++)
    {
        cout << "\t\tinterface-name: " << it->interface_name << "\n\t\tmac-address: " << it->mac_address << "\n";
    }
    cout << "\n!---- END OF STREAM DATA ---!\n";
}

int stream_t::setStreamId(std::string streamId)
{
    this->stream_id.assign(streamId.c_str());
    return 1;
}
int stream_t::setTalkerStatus(talkers_status_t talkerStatus)
{
    this->talkerStatus = talkerStatus;
    return 1;
}
int stream_t::addListenerStatus(listeners_status_t listenerStatus)
{
    this->listenerStatusList.push_back(listenerStatus);
    return 1;
}
int stream_t::setStatusInfo(status_info_t statusInfo)
{
    this->status_info = statusInfo;
    return 1;
}
int stream_t::addFailedInterface(end_station_interface_t failedInterface)
{
    this->failedInterfacesList.push_back(failedInterface);
    return 1;
}
int stream_t::getId()
{
    return stoi(this->stream_id.c_str());
}

/*
    Constructor for interface_configuration_t with macAddress and InterfaceName
*/
interface_configuration_t::interface_configuration_t(std::string macAddress, std::string interfaceName, const void_allocator &alloc)
    : end_station_interface_t(macAddress, interfaceName, alloc), config_list(alloc)
{}

/*
    @brief add a config to the interface config list in status-talker-listener and set a correct id
    @param[in] config_t datatype
    @return always 1
*/
int interface_configuration_t::addConfig(config_t config)
{
    config.index = this->config_list.size();
    this->config_list.push_back(config);
    return 1;
}

/*
    @brief add an interface to the interface configuration list. Check if interface name already there
    @param[in] interface_configuration_t
    @return 1 if added, 0 if not
*/
int status_talker_listener_t::addInterfaceConfiguration(interface_configuration_t interface)
{
    this->interface_configuration.begin();
    for (interface_list_t::iterator it = this->interface_configuration.begin();
        it != this->interface_configuration.end(); it++)
        if (it->interface_name.compare(interface.interface_name) == 0)
            return 0;
    this->interface_configuration.push_back(interface);
    return 1;
}

void fillData(module_t *moduleptr, void_allocator alloc)
{
    // devices
    device_t test("device1", "11110000000000001111", alloc);
    device_t test2("device2", "11110000000000001110", alloc);
    device_t test3("device3", "11110100000000001111", alloc);
    device_t test4("device4", "11110000001000001111", alloc);
    moduleptr->addDevice(test);
    moduleptr->addDevice(test2);
    moduleptr->addDevice(test3);
    moduleptr->addDevice(test4);

    // talker 1
    talker_t testtalker(1, alloc);
    testtalker.setRank(1);
    testtalker.addInterface(end_station_interface_t("AA-AA-AA-AA-AA-AA", "eth0", alloc));
    testtalker.addInterface(end_station_interface_t("CC-CC-CC-CC-CC-CC", "dect0", alloc));
    testtalker.addCBSequenceType(1);
    testtalker.addCBSequenceType(2);
    testtalker.addCBSequenceType(50);
    testtalker.addCBSequenceType(60);
    testtalker.addCBStreamIdenType(3);
    testtalker.addCBStreamIdenType(4);
    testtalker.addCBStreamIdenType(70);
    testtalker.addCBStreamIdenType(80);
    testtalker.interface_capabilities.vlan_tag_capable = true;
    testtalker.user_to_network_requirements.max_latency = 5;
    testtalker.user_to_network_requirements.num_seamless_trees = 2;
    testtalker.traffic_specification.interval.numerator = 1;
    testtalker.traffic_specification.interval.denominator = 2;
    testtalker.traffic_specification.max_frame_size = 1024;
    testtalker.traffic_specification.max_frames_per_interval = 20;
    testtalker.traffic_specification.time_aware.earliest_transmit_offset = 1;
    testtalker.traffic_specification.time_aware.jitter = 5;
    testtalker.traffic_specification.time_aware.latest_transmit_offset = 10;
    testtalker.traffic_specification.transmission_selection = 1234;
    testtalker.data_frame_specification_list.push_back(data_frame_specification_t(0, 4, 80, alloc));
    testtalker.data_frame_specification_list.push_back(data_frame_specification_t(1, "AA-AA-AA-AA-AA-AA", "BB-BB-BB-BB-BB-BB", alloc));
    // talker 2
    talker_t testtalker2(2, alloc);
    testtalker2.setRank(1);
    testtalker2.addInterface(end_station_interface_t("11-11-11-11-11-11", "eth1", alloc));
    testtalker2.addInterface(end_station_interface_t("22-22-22-22-22-22", "dect1", alloc));
    testtalker2.addCBSequenceType(1);
    testtalker2.addCBSequenceType(2);
    testtalker2.addCBSequenceType(3);
    testtalker2.addCBSequenceType(4);
    testtalker2.addCBStreamIdenType(11);
    testtalker2.addCBStreamIdenType(22);
    testtalker2.addCBStreamIdenType(33);
    testtalker2.addCBStreamIdenType(44);
    testtalker2.interface_capabilities.vlan_tag_capable = false;
    testtalker2.user_to_network_requirements.max_latency = 80;
    testtalker2.user_to_network_requirements.num_seamless_trees = 90;
    testtalker2.traffic_specification.interval.numerator = 10;
    testtalker2.traffic_specification.interval.denominator = 20;
    testtalker2.traffic_specification.max_frame_size = 10240;
    testtalker2.traffic_specification.max_frames_per_interval = 200;
    testtalker2.traffic_specification.time_aware.earliest_transmit_offset = 10;
    testtalker2.traffic_specification.time_aware.jitter = 50;
    testtalker2.traffic_specification.time_aware.latest_transmit_offset = 100;
    testtalker2.traffic_specification.transmission_selection = 12340;
    testtalker2.addSpecification(data_frame_specification_t(0, "192.168.1.1", "127.0.0.1", 70, 90, 1234, 5678, alloc));
    testtalker2.addSpecification(data_frame_specification_t(1, "::1", "2::33", 50, 30, 5678, 9012, alloc));
    // adding listeners
    moduleptr->addTalker(testtalker);
    moduleptr->addTalker(testtalker2);

    // listener1
    listener_t testlistener(-1, alloc);
    testlistener.setId(1);
    testlistener.addCBSequenceType(100);
    testlistener.addCBSequenceType(200);
    testlistener.addCBSequenceType(300);
    testlistener.addCBSequenceType(400);
    testlistener.addCBStreamIdenType(500);
    testlistener.addCBStreamIdenType(600);
    testlistener.addCBStreamIdenType(700);
    testlistener.addCBStreamIdenType(800);
    testlistener.interface_capabilities.vlan_tag_capable = 1;
    testlistener.user_to_network_requirements.max_latency = 100;
    testlistener.user_to_network_requirements.num_seamless_trees = 3;
    testlistener.addInterface(end_station_interface_t("22-22-22-22-22-22", "test1a", alloc));
    testlistener.addInterface(end_station_interface_t("33-33-33-33-33-33", "test1b", alloc));
    // listener2
    listener_t testlistener2(-1, alloc);
    testlistener2.setId(2);
    testlistener2.addCBSequenceType(150);
    testlistener2.addCBSequenceType(250);
    testlistener2.addCBStreamIdenType(350);
    testlistener2.addCBStreamIdenType(450);
    testlistener2.interface_capabilities.vlan_tag_capable = 15;
    testlistener2.user_to_network_requirements.max_latency = 1500;
    testlistener2.user_to_network_requirements.num_seamless_trees = 53;
    testlistener2.addInterface(end_station_interface_t("33-33-33-22-22-22", "test2a", alloc));
    testlistener2.addInterface(end_station_interface_t("22-22-22-33-33-33", "test2b", alloc));
    // adding listeners
    moduleptr->addListener(testlistener);
    moduleptr->addListener(testlistener2);

    // // stream 1
    // stream_t stream1(alloc);
    // stream1.setStreamId("CC-CC-CC-CC-CC-CC:11-11");
    // // status info
    // status_info_t statusInfo;
    // statusInfo.failure_code = 0;
    // statusInfo.listener_status = L_PARTIAL_FAILED;
    // statusInfo.talker_status = T_READY;
    // stream1.setStatusInfo(statusInfo);
    // // failed interfaces
    // stream1.addFailedInterface(end_station_interface_t("AA-AA-AA-AA-AA-AA", "qwe", alloc));
    // stream1.addFailedInterface(end_station_interface_t("BB-BB-BB-BB-BB-BB", "asd", alloc));
    // // talker status
    // talkers_status_t talkerStatus(alloc);
    // talkerStatus.accumulated_latency = 100;
    // talkerStatus.talker_id = 1;
    // interface_configuration_t talkerIC("CC-CC-CC-CC-CC-CC", "talkerinterface", alloc);
    // talkerIC.addConfig(config_t(0, 3, 80, alloc));
    // talkerIC.addConfig(config_t(1, 456, 3, alloc));
    // talkerIC.addConfig(config_t(2, 100, alloc));
    // talkerStatus.addInterfaceConfiguration(talkerIC);
    // stream1.setTalkerStatus(talkerStatus);
    // // listeners status 1
    // listeners_status_t listenerStatus(alloc);
    // listenerStatus.accumulated_latency = 400;
    // listenerStatus.listener_id = 1;
    // interface_configuration_t listenerIC("11-11-11-11-11-11", "listenerif1", alloc);
    // listenerIC.addConfig(config_t(0, 2, 30, alloc));
    // listenerIC.addConfig(config_t(1, 4, 90, alloc));
    // listenerStatus.addInterfaceConfiguration(listenerIC);
    // interface_configuration_t listenerIC2("11-11-11-22-22-22", "listenerif2", alloc);
    // listenerIC2.addConfig(config_t(0, 3, 306, alloc));
    // listenerIC2.addConfig(config_t(1, 5, 906, alloc));
    // listenerStatus.addInterfaceConfiguration(listenerIC2);
    // stream1.addListenerStatus(listenerStatus);
    // // listeners status 2
    // listeners_status_t listenerStatus2(alloc);
    // listenerStatus2.accumulated_latency = 200;
    // listenerStatus2.listener_id = 2;
    // interface_configuration_t listenerIC3("22-11-11-11-11-11", "listenerif3", alloc);
    // listenerIC3.addConfig(config_t(0, 1, 310, alloc));
    // listenerIC3.addConfig(config_t(1, 3, 910, alloc));
    // listenerStatus2.addInterfaceConfiguration(listenerIC3);    
    // interface_configuration_t listenerIC4("11-11-11-11-11-33", "listenerif4", alloc);
    // listenerIC4.addConfig(config_t(0, 0, 320, alloc));
    // listenerIC4.addConfig(config_t(1, 7, 920, alloc));
    // listenerStatus2.addInterfaceConfiguration(listenerIC4); 
    // stream1.addListenerStatus(listenerStatus2);
    // // status_info
    // stream1.status_info.failure_code = 1;
    // stream1.status_info.listener_status = L_PARTIAL_FAILED;
    // stream1.status_info.talker_status = T_READY;
    // // failed interfaces
    // stream1.addFailedInterface(end_station_interface_t("11-11-11-11-11-33", "listenerif4", alloc));
    // stream1.addFailedInterface(end_station_interface_t("11-11-11-22-22-22", "listenerif2", alloc));
    // set
    // moduleptr->addStream(stream1);

    // stream1.talkerStatus.interface_configuration.config_list
}