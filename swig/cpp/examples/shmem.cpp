#include <iostream> 
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <stdio.h> 
#include <unistd.h>

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

// remove the define to disable testing
#ifndef CATCH_CONFIG_RUNNER
// #define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#include "tsn-cuc-tests.cpp"
#endif

#ifndef TSN_CUC_DECT
#define TSN_CUC_DECT
#include "tsn-cuc-dect.hpp"
#include "tsn-cuc-dect.cpp"
#endif

using namespace std; 

int createDeviceId(module_t* moduleptr, end_station_t &device)
{
    cout << "\n!---- DEVICE ID ----!\n\n";
    cout << "Enter device ID to make talker or listener, enter quit to exit: ";
    int id;
    if (!(readInt(id)))
        return 0;
    if (id < 0)
        return 0;
    cout << "Checking ID\n";
    auto it = moduleptr->devicesList.begin();
    if (checkId(id, &moduleptr->devicesList, it))
    {
        cout << "ID found. Belongs to " << ((device_t)*(it)).getName() << 
            " with PMID " << ((device_t)*(it)).getPmid() << "\n";
        device.setId(id);
        return 1;
    }
    cout << "Device ID not found. Try again.\n";
    return 0;
}

int createTalkerRank(talker_t &talker)
{
    int rank;
    cout << "\n!---- RANK ----!\n\n";
    cout << "Enter rank (0 or 1, 0 being more important): ";
    readInt(rank);
    cout << "Read rank: " << rank << "\n";
    talker.setRank(rank);
    return 1;
}

int createTalkerTrafficSpecification(talker_t &talker)
{
    std::string input;
    traffic_specification_t traffic_specification;
    int counter;
    std::list<int> inputList;
    cout << "\n!---- TRAFFIC SPECIFICATION ----!\n\n";
    cout << "Configure traffic specification (how the Talker transmits frames for the Stream):\n";
    cout << "Interval Numerator, Interval Denumerator, Max Frame Size, Max Frames per Interval, Transmission Selection:\n";
    cin >> input;
    if (!(getList(counter, inputList, input)) || (counter != 5))
        return 0;
    traffic_specification.interval.numerator = *(inputList.begin());
    inputList.pop_front();
    traffic_specification.interval.denominator = *(inputList.begin());
    inputList.pop_front();
    traffic_specification.max_frame_size = *(inputList.begin());
    inputList.pop_front();
    traffic_specification.max_frames_per_interval = *(inputList.begin());
    inputList.pop_front();
    traffic_specification.transmission_selection = *(inputList.begin());
    cout << "Saving settings:\n" << 
        "Interval Numerator - Denominator: " << traffic_specification.interval.numerator << "-" << 
        traffic_specification.interval.denominator << "\n" << "Max Frame Size: " << traffic_specification.max_frame_size << 
        " max frames per interval: " << traffic_specification.max_frames_per_interval << " Transmission Selection: " << 
        traffic_specification.transmission_selection << "\n"; 
    talker.traffic_specification = traffic_specification;
    return 1;
}

int createTalkerTimeAware(talker_t &talker)
{
    std::string input;
    cout << "\n!---- TIME AWARENESS ----!\n\n";
    cout << "Shall the Talker be time-aware? (Y or N):\n";
        while(true)
        {
            cin >> input;
            if (input.compare("N") == 0)
            {
                // -1 as not configured value!
                talker.traffic_specification.time_aware.earliest_transmit_offset = -1;
                talker.traffic_specification.time_aware.jitter = -1;
                talker.traffic_specification.time_aware.latest_transmit_offset = -1;
                return 0;
            }                        
            else if (input.compare("Y") == 0)
            {
                return 1;
            }
            cout << "Enter Y or N\n";
        }
}

int createTalkerTimeAwareConfig(talker_t &talker)
{
    std::string input;
    int counter;
    std::list<int> inputList;
    cout << "Time aware talker: Enter values for earliest transmit offset, latest transmit offset and jitter\n";
    cin >> input;
    if (!(getList(counter, inputList, input)) || (counter != 3))
        return 0;
    talker.traffic_specification.time_aware.earliest_transmit_offset = *(inputList.begin());
    inputList.pop_front();
    talker.traffic_specification.time_aware.latest_transmit_offset = *(inputList.begin());
    inputList.pop_front();
    talker.traffic_specification.time_aware.jitter = *(inputList.begin());
    cout << "Saving settings:\n" << "Earliest Transmit Offset: " << talker.traffic_specification.time_aware.earliest_transmit_offset <<
        " latest transmit offset: " << talker.traffic_specification.time_aware.latest_transmit_offset << " jitter: " <<
        talker.traffic_specification.time_aware.jitter << "\n";
    return 1;
}

int createUserToNetworkRequirements(user_to_network_requirements_t &user_to_network_requirements)
{
    std::string input;
    int counter = 0;
    std::list<int> inputList;
    cout << "\n!---- USER TO NETWORK REQUIREMENTS ----!\n\n";
    cout << "User to network requirements: Number of seamless trees and maximum latency\n";
    cin >> input;
    if (!(getList(counter, inputList, input)) || (counter != 2))
        return 0;
    cout << "the counter: " << counter << "\n";
    user_to_network_requirements.num_seamless_trees = *(inputList.begin());
    inputList.pop_front();
    user_to_network_requirements.max_latency = *(inputList.begin());
    cout << "Saving settings:\n" << "Number of seamless trees: " << user_to_network_requirements.num_seamless_trees <<
        " maximum latency: " << user_to_network_requirements.max_latency << "\n";
    return 1;
}

int createInterfaceCapabilities(interface_capabilities_t &interface_capabilities)
{
    std::string input;
    cout << "\n!---- INTERFACE CAPABILITIES ----!\n\n";
    cout << "Interface capabilities: is the talker interface able to VLAN tag frames? (Y or N)\n";
    while(true)
    {
        cout << "Enter Y or N\n";
        cin >> input;
        if (input.compare("N") == 0)
        {
            interface_capabilities.vlan_tag_capable = false;
            return 1;
        }
        else if (input.compare("Y") == 0)
        {
            interface_capabilities.vlan_tag_capable = true;
            return 1;
        }
    }
}

int createInterfaceCapabilitiesCB(interface_capabilities_t &interface_capabilities)
{
    std::string input;
    cout << "Add Stream Identification Type (1), add Sequence Type (2), " << 
    "list Stream Identification Types (3), list sequence types (4), " << 
    "delete Entry menu (5), carry on  (6):\n";
    int choice = 0;
    cin >> input;
    try{
        choice = stoi(input);
    } catch (const std::invalid_argument& ia) {
        std::cerr << "Invalid argument in input: " << ia.what() << '\n';
    }
    switch (choice)
    {
        case 1: // add stream ident
        {
            cout << "Adding Stream Identification Type:\n";
            cin >> input;
            int identType;
            try{
                identType = stoi(input);
            }catch (const std::invalid_argument& ia) {
                std::cerr << "Invalid argument in input: " << ia.what() << '\n';
                break;
            }
            if (uintListContainsUint(interface_capabilities.cb_stream_iden_type_list, identType))
            {
                cout << "Item already exists!\n";
                break;
            }
            cout << "Adding: " << identType << "\n";
            interface_capabilities.cb_stream_iden_type_list.push_back(identType);
            break;
        }
        case 2: // add sequence
        {
            cout << "Adding Sequence Type:\n";
            cin >> input;
            int sequenceType;
            try{
                sequenceType = stoi(input);
            }catch (const std::invalid_argument& ia) {
                std::cerr << "Invalid argument in input: " << ia.what() << '\n';
                break;
            }
            if (uintListContainsUint(interface_capabilities.cb_sequence_type_list, sequenceType))
            {
                cout << "Item already exists!\n";
                break;
            }
            interface_capabilities.cb_sequence_type_list.push_back(sequenceType);
            break;
        }
        case 3: // list stream iden
        {
            cout << "Listing Stream Identification types:\n";
            printIntList(interface_capabilities.cb_stream_iden_type_list);
            break;
        }
        case 4: // list sequence
        {
            cout << "Listing Sequence types:\n";
            printIntList(interface_capabilities.cb_sequence_type_list);
            break;
        }
        case 5: // delete an entry
        {
            uint_list::iterator it;
            cout << "Delete Menu: choose stream ident type (1) or sequence type (2):\n";
            cin >> input;
            cout << "Select entry to delete from list:\n";
            int choice;
            bool choosing = true;
            if (input.compare("1") == 0)
            {
                while (choosing)
                {
                    cout << "Choosing stream ident type entry: (q for quit): \n";
                    cin >> input;
                    if (input.compare("q") == 0)
                        break;
                    readInt(choice);
                    if (getItemFromList(interface_capabilities.cb_stream_iden_type_list, it, choice))
                    {
                        interface_capabilities.cb_stream_iden_type_list.remove(*(it));
                        choosing = false;
                    }
                    else
                        cout << "Invalid choice\n";
                }                               
            }
            else if (input.compare("2") == 0)
            {
                while (choosing)
                {
                    cout << "Choosing sequence type entry: (q for quit): \n";
                    cin >> input;
                    if (input.compare("q") == 0)
                        break;
                    readInt(choice);
                    if (getItemFromList(interface_capabilities.cb_sequence_type_list, it, choice))
                    {
                        interface_capabilities.cb_sequence_type_list.remove(*(it));
                        choosing = false;
                    } 
                    else
                        cout << "Invalid choice\n";
                }
            }
            else 
            {
                cout << "Invalid choice. Choose 1 or 2\n";
            }
            break;
        }
        case 6: // carry on
        {
            return 1;
        }
    }
    return 0;
}

int createEndStationInterfaces(end_station_t &talker, void_allocator &alloc)
{
    std::string input;
    cout << "\n!---- END STATION INTERFACES ----!\n\n";
    cout << "Configuring end station interfaces.\nCreate a new interface: 1, List interfaces: 2, " <<
        "Delete an interface: 3, carry on with next step: 4\n";
    int choice = 0;
    cin >> input;
    try{
        choice = stoi(input);
    } catch (const std::invalid_argument& ia) {
        std::cerr << "Invalid argument in input: " << ia.what() << '\n';
    }
    switch (choice)
    {
        case 1: // create a new interface
        {
            end_station_interface_t interface(alloc);
            cout << "Creating a new interface:\nEnter Interface Name\n";
            cin >> interface.interface_name;
            while (true)
            {
                cout << "Enter MAC Address:\n";
                cin >> input;
                if (readMac(input.c_str()))
                {
                    interface.mac_address.assign(input.c_str());
                    break;
                }
                else
                    cout << "Wrong format: should be XX:XX:XX:XX:XX:XX\n";
            }

            cout << "trying to add interface: " << interface.interface_name << " mac: " << interface.mac_address << "\n";
            if(talker.addInterface(interface))                        
                break;
            cout << "Interface name already existing!\n";
            break;
        }
        case 2: // List interfaces
        {
            auto it = talker.end_station_interface_list.begin();
            for (uint i = 0; i < talker.end_station_interface_list.size(); i++)
            {
                cout << i << " - Name: " << it->interface_name << " Address: " << it->mac_address << "\n";
                it++;
            }
            break;
        }
        case 3: // delete entry
        {
            while (true)
            {
                cout << "Enter index in list (q for quit): \n";
                cin >> input;
                uint index;
                if (input.compare("q") == 0)
                    break;
                try{
                    index = stoi(input);
                } catch (const std::invalid_argument& ia) {
                    std::cerr << "Invalid argument in input: " << ia.what() << '\n';
                    break;
                }
                if (removeEntryFromList(talker.end_station_interface_list, index))
                    break;
                cout << "Entry number not found!\n";
            }
            break;
        }
        case 4: // carry on
        {
            return 1;
        }
    }
    return 0;
}

int createTalkerDataFrameSpecifications(talker_t &talker, void_allocator &alloc)
{
    std::string input;
    int counter;
    std::list<int> inputList;
    cout << "\n!---- DATA FRAME SPECIFICATION ----!\n\n";
    cout << "Configuring data frame specifications. Add new specification: 1, List specifications: 2, delete specification: 3, continue: 4\n";
    int choice = 0;
    cin >> input;
    try{
        choice = stoi(input);
    } catch (const std::invalid_argument& ia) {
        std::cerr << "Invalid argument in input: " << ia.what() << '\n';
    }
    switch (choice)
    {
        case 1: // add new spec
        {
            cout << "Choose field: MAC (1), VLAN (2), IPV4(3), IPV6(4)\n";
            int field = 0;
            cin >> input;
            try{
                field = stoi(input);
            } catch (const std::invalid_argument& ia) {
                std::cerr << "Invalid argument in input: " << ia.what() << '\n';
                break;
            }
            choice_t choice(alloc);
            switch (field)
            {
                case 1: // MAC
                {
                    choice.field = MAC;
                    while (true)
                    {
                        cout << "MAC - Enter source MAC address:\n";
                        cin >> input;
                        if (readMac(input.c_str()))
                        {
                            choice.source_mac_address->assign(input.c_str());
                            break;
                        }
                        cout << "Wrong format. Try again.\n";
                    }
                    while (true)
                    {
                        cout << "Enter destination MAC address:\n";
                        cin >> input;
                        if (readMac(input.c_str()))
                        {
                            choice.destination_mac_address->assign(input.c_str());
                            break;
                        }
                        cout << "Wrong format. Try again.\n";
                    }
                    break;
                }
                case 2: // VLAN
                {
                    choice.field = VLAN;
                    while(true)
                    {
                        cout << "VLAN - Enter priority code point:\n";
                        if (readInt(*choice.pcp))
                            break;
                        cout << "Wrong format. Try again.\n";
                    }
                    while(true)
                    {
                        cout << "Enter VLAN id:\n";
                        if (readInt(*choice.vlan_id))
                            break;
                        cout << "Wrong format. Try again.\n";
                    }
                    break;
                }
                case 3: //IPV4
                {
                    choice.field = IPV4;
                    while(true)
                    {
                        cout << "IPv4 - Enter source IP address:\n";
                        cin >> input;
                        if (readIpv4(input))
                        {                                        
                            choice.ipv4_source_ip_address->assign(input.c_str());
                            break;
                        }
                        cout << "Wrong format. Try again.\n";
                    }
                    while(true)
                    {
                        cout << "Enter source port:\n";
                        if (readInt(*choice.source_port))
                            break;
                        cout << "Wrong format. Try again.\n";
                    }
                    while(true)
                    {
                        cout << "Enter destination IP address:\n";
                        cin >> input;
                        if (readIpv4(input))
                        {
                            choice.ipv4_destination_ip_address->assign(input.c_str());
                            break;
                        }
                        cout << "Wrong format. Try again.\n";
                    }
                    while(true)
                    {
                        cout << "Enter destination port:\n";
                        if (readInt(*choice.destination_port))
                            break;
                        cout << "Wrong format. Try again.\n";
                    }
                    while(true)
                    {
                        cout << "Enter dpsc and protocol:\n";
                        while(true)
                        {
                            cin >> input;
                            counter = 0;
                            inputList.clear();
                            if ((!getList(counter, inputList, input)) || (counter != 2))
                                cout << "Wrong format. Try again.\n";
                            else
                                break;
                        }
                        *choice.dscp = *(inputList.begin());
                        *choice.protocol = *(++(inputList.begin()));
                        break;
                    }
                    break;
                }
                case 4: // IPv6
                {
                    choice.field = IPV6;
                    while(true)
                    {
                        cout << "IPv6 - Enter source address:\n";
                        cin >> input;
                        if (readIpv6(input))
                        {
                            choice.ipv6_source_ip_address->assign(input.c_str());
                            break;
                        }
                        cout << "Wrong format. Try again.\n";
                    }
                    while(true)
                    {
                        cout << "Enter source port:\n";
                        if (readInt(*choice.source_port))
                            break;
                        cout << "Wrong format. Try again.\n";
                    }
                    while(true)
                    {
                        cout << "Enter destination address:\n";
                        cin >> input;
                        if (readIpv6(input))
                        {
                            choice.ipv6_destination_ip_address->assign(input.c_str());
                            break;
                        }
                        cout << "Wrong format. Try again.\n";
                    }    
                    while(true)
                    {
                        cout << "Enter destination port:\n";
                        if (readInt(*choice.destination_port))
                            break;
                        cout << "Wrong format. Try again.\n";
                    }
                    while(true)
                    {
                        cout << "Enter dpsc and protocol:\n";
                        cin >> input;
                        counter = 0;
                        inputList.clear();
                        if (getList(counter, inputList, input))
                            if (counter == 2)
                                {
                                    *choice.dscp = *(inputList.begin());
                                    *choice.protocol = *(++(inputList.begin()));
                                    break;
                                }
                        cout << "Wrong format. Try again.\n";
                    }
                    break;
                }
            }
            data_frame_specification_t spec(alloc);
            spec.choice = choice;
            talker.addSpecification(spec);
            break;
        }
        case 2: // list specification
        {
            cout << "Listing specifications: \n";
            auto it = talker.data_frame_specification_list.begin();
            for (uint i = 0; i<talker.data_frame_specification_list.size(); i++)
            {
                if ((*it).choice.field == MAC)
                    cout << "index: " << (*it).index << "\n\tieee802-mac-addresses\n\t\tsource_mac_address: " << 
                        (*it).choice.str1 << "\n\t\tdestination_mac_address: "<< (*it).choice.str2 << "\n";
                else if ((*it).choice.field == VLAN)
                    cout << "index: " << (*it).index << "\n\tieee802-vlan-tag\n\t\tpcp: " << (*it).choice.val1 << 
                        "\n\t\tvlan_id: " << (*it).choice.val2 << "\n";
                else if ((*it).choice.field == IPV4)
                    cout << "index: " << (*it).index << "\n\tipv4-tuple\n\t\tipv4_source_ip_address: " << (*it).choice.str1 << 
                        "\n\t\tipv4_destination_ip_address: "<< (*it).choice.str2 << "\n\t\tdscp: " << (*it).choice.val1 << 
                        "\n\t\tprotocol: " << (*it).choice.val2 << "\n\t\tsource_port: " << (*it).choice.val3 << 
                        "\n\t\tdestination_port: " << (*it).choice.val4 << "\n";
                else if ((*it).choice.field == IPV6)
                    cout << "index: " << (*it).index << "\n\tipv6-tuple\n\t\tipv6_source_ip_address: " << (*it).choice.str1 << 
                        "\n\t\tipv6_destination_ip_address: "<< (*it).choice.str2 << "\n\t\tdscp: " << (*it).choice.val1 << 
                        "\n\t\tprotocol: " << (*it).choice.val2 << "\n\t\tsource_port: " << (*it).choice.val3 << 
                        "\n\t\tdestination_port: " << (*it).choice.val4 << "\n";
                it++;
            }
            break;
        }
        case 3: // delete specification
        {
            while(true)
            {
                cout << "Enter Index to be deleted:\n";
                int index;
                if (readInt(index))
                {
                    if (removeIndexFromList(talker.data_frame_specification_list, index))
                        break;
                    cout << "Index not found in list\n";
                }
                cout << "Wrong input. Try again.\n";
            }
            break;
        }
        case 4: // continue
        {
            cout << "Continuing...\n";
            return 1;
        }
    }
    return 0;
}

int createTalker(module_t* moduleptr, void_allocator &alloc)
{
    std::string input;
    talker_t talker(-1, alloc);
    int stage = 0;
    while(true)
    {
        switch (stage)
        {
            case 0: // Device ID
            {
                if (createDeviceId(moduleptr, talker))
                    stage++;
                else 
                    return 0;
                break;
            }
            case 1: // rank
            {
                if (createTalkerRank(talker))
                    stage++;
                break;
            }
            case 2: // traffic specification
            {
                if (createTalkerTrafficSpecification(talker))
                    stage++;
                break; 
            }
            case 3: // Time Aware
            {
                if (createTalkerTimeAware(talker))
                    stage++;
                else
                    stage += 2;
                break;
            }      
            case 4: // Time aware Talker
            {
                if (createTalkerTimeAwareConfig(talker))
                    stage++;
                break;
            }
            case 5: // user to network requirements
            {
                if (createUserToNetworkRequirements(talker.user_to_network_requirements))
                    stage++;
                break;
            }
            case 6: // interface capabilities
            {
                if (createInterfaceCapabilities(talker.interface_capabilities))
                    stage++;
                break;
            }
            case 7: // CB Stream iden type or sequence type add
            {
                if (createInterfaceCapabilitiesCB(talker.interface_capabilities))
                    stage++;
                break;
            }
            case 8: // end station interfaces
            {
                if (createEndStationInterfaces(talker, alloc))
                    stage++;
                break;
            }
            case 9: // data frame specification
            {
                if (createTalkerDataFrameSpecifications(talker, alloc))
                    stage++;
                break;
            }
            case 10:    // incorporate the data into the talker object and add it to the module in shared memory
            {
                cout << "Adding talker with id: " << talker.getId() << " to module...\n";
                moduleptr->addTalker(talker);
                return 1;
            }
        }
    }
    return 0;
}

int createListener(module_t *moduleptr, void_allocator &alloc)
{    
    listener_t listener(-1, alloc);
    int stage = 0;
    while(true)
    {
        switch (stage)
        {
            case 0: // Device ID
            {
                if (createDeviceId(moduleptr, listener))
                    stage++;
                else 
                    return 0;
                break;
            }
            case 1: // user to network requirements
            {
                if (createUserToNetworkRequirements(listener.user_to_network_requirements))
                    stage++;
                break;
            }
            case 2: // interface capabilities
            {
                if (createInterfaceCapabilities(listener.interface_capabilities))
                    stage++;
                break;
            }
            case 3: // CB Stream iden type or sequence type add
            {
                if (createInterfaceCapabilitiesCB(listener.interface_capabilities))
                    stage++;
                break;
            }
            case 4: // end station interfaces
            {
                if (createEndStationInterfaces(listener, alloc))
                    stage++;
                break;
            }
            case 5:    // incorporate the data into the talker object and add it to the module in shared memory
            {
                cout << "Adding talker with id: " << listener.getId() << " to module...\n";
                moduleptr->addListener(listener);
                return 1;
            }
        }
    }
    return 0;
}

template <class T>
int listEntities(boost::interprocess::list<T, boost::interprocess::allocator<T, boost::interprocess::managed_shared_memory::segment_manager>> &list, module_t *moduleptr)
{
    cout << "\n!---- LISTING THE " << "asd" << " ----!\n\n";
    cout << "Number of registered talkers: " << list.size() << "\n\n";
    cout << "Device IDs registered as talkers:\n";
    int id;
    const char* pmid;
    auto it = list.begin();
    for (uint i = 0; i<list.size(); i++)
    {
        id = (*it).getId();
        for (auto it2 = moduleptr->devicesList.begin(); 
            it2 != moduleptr->devicesList.end(); it2++)
            if ((*it2).getId() == id)
                pmid = (*it2).getPmid();
        cout << "\t" << std::distance(list.begin(), it) << ": Device ID " << id << " with PMID " << pmid << "\n";
        it++;
    }
    cout << "\nEnter entry number to list data or (q) to quit\n";
    int entry;
    while (true)
    {
        if (readInt(entry))
        {
            if ((entry < 0) || ((uint)entry >= list.size()))
            {
                cout << "Entry not found.\n";
                continue;
            }
            it = list.begin();
            advance(it, entry);
            it->printData();
            break;
        }
        break;
    }
    return 1;
}

int listStreams(module_t *moduleptr)
{
    cout << "\n!---- LISTING THE " << "asd" << " ----!\n\n";
    cout << "Number of registered talkers: " << moduleptr->streamsList.size() << "\n\n";
    cout << "Device IDs registered as talkers:\n";
    for (auto it = moduleptr->streamsList.begin(); 
        it != moduleptr->streamsList.end(); it++)
    {
        cout << "\t" << std::distance(moduleptr->streamsList.begin(), it) << 
            ": Stream ID " << it->stream_id << "\n";
    }
            
    cout << "\nEnter entry number to list data or (q) to quit\n";
    int entry;
    while (true)
    {
        if (readInt(entry))
        {
            if ((entry < 0) || ((uint)entry >= moduleptr->streamsList.size()))
            {
                cout << "Entry not found.\n";
                continue;
            }
            auto it2 = moduleptr->streamsList.begin();
            advance(it2, entry);
            it2->printData();
            break;
        }
        break;
    }
    return 1;
}

int showDevices(module_t *module)
{
    cout << module->devicesList.size() << " registered Devices:\n";
    auto it = module->devicesList.begin();
    for (uint i = 0; i < module->devicesList.size(); i++)
    {
        cout << "\t" << (*it).getId() << " - " << (*it).getName() << " - " << (*it).getPmid() << "\n";
        it++;
    }
    return 1;
}

// void sharedMemoryHealth()
// {
//         cout << "thread\n";
//         cout.flush();
//         // boost::chrono::milliseconds(1000);
// }

int main( int argc, char* argv[] ) 
{
    // testing
    #ifdef CATCH_CONFIG_RUNNER
    int result = 0;
    result = Catch::Session().run( argc, argv );
    return 1;
    #endif 

    // Create shared memory
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_or_create,"SYSREPO_SHM", 65536);

    // An allocator convertible to any allocator<T, segment_manager_t> type
    void_allocator alloc(segment.get_segment_manager());

    // Find the module in the managed shared memory
    std::pair<module_t*, std::size_t> module_shm = segment.find<module_t>("dect_cuc_tsn");

    if (!(module_shm.first))
    {
        cout << "Shared Memory unavailable. Is the sysrepo plugin running?\n";
        return 0;
    }

    // start the health thread
    // boost::thread t(sharedMemoryHealth);
    // t.join();

    while (true)
    {
        cout << "\n!---- MAIN MENU ----!\n\n";
        cout << "Select option\nShow Devices: D\nCreate Talker: T\nCreateListener: L\n" <<
            "List Talkers: LT\nList Listeners: LL\nList Streams: LS\nWrite Test Dummy Data: TT\nQuit: Q\n";
        std::string input;
        cin >> input;
        if (input.compare("Q") == 0)
        {
            return 0;
        }
        try {
            boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> 
                lock(module_shm.first->mutex, boost::interprocess::try_to_lock);
            if(lock)
            {
                if ((input.size() < 0) || (input.size() > 2))
                {
                    cout << "invalid input: wrong length\n";
                }
                else if (input.compare("D") == 0)
                {
                    cout << "\n!---- SHOW DEVICES ----!\n\n";
                    showDevices(module_shm.first);
                }
                else if (input.compare("T") == 0)
                {
                    cout << "\n!---- CREATING A TALKER ----!\n\n";
                    createTalker(module_shm.first, alloc);
                }
                else if (input.compare("L") == 0)
                {
                    cout << "\n!---- CREATING A LISTENER ----!\n\n";
                    createListener(module_shm.first, alloc);
                    cout << "Creating Listener\n";
                }
                else if (input.compare("LT") == 0)
                {
                    listEntities(module_shm.first->talkersList, module_shm.first);
                }
                else if (input.compare("LL") == 0)
                {
                    listEntities(module_shm.first->listenersList, module_shm.first);
                }
                else if (input.compare("LS") == 0)
                {
                    listStreams(module_shm.first);
                }
                else
                {
                    cout << "Invalid input: wrong char\n";
                }
            }
            else
            {
                cout << "Shared memory currently locked. Try again later\n";
            }
        } catch (boost::interprocess::interprocess_exception &ex) {
            std::cout << ex.what() << std::endl;
            continue;
        }
        input.clear();
    }
    return 0; 
} 