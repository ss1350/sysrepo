#include <boost/interprocess/managed_shared_memory.hpp>
#include <iostream> 
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <stdio.h> 
#include <unistd.h>

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


int createTalker(module_t &module)
{
    std::string input;
    
    traffic_specification_t traffic_specification;
    user_to_network_requirements_t user_to_network_requirements;
    interface_capabilities_t interface_capabilities;
    int rank;
    
    talker_t talker;

    int stage = 0;
    int counter;
    std::list<int> inputList;

    while(true)
    {
        switch (stage)
        {
            case 0: // Device ID
            {
                cout << "\n!---- DEVICE ID ----!\n\n";
                cout << "Enter device ID to make talker, enter quit to exit: ";
                cin >> input;
                int id;
                if (!(readInt(input, id)))
                    break;
                cout << "Checking ID\n";
                auto it = module.devicesList.begin();
                if (checkId(id, module.devicesList, it))
                {
                    cout << "ID found. Belongs to " << ((device_t)*(it)).getName() << 
                        " with PMID " << ((device_t)*(it)).getPmid() << "\n";
                    talker.id = id;
                    stage++;
                }
                break;
            }
            case 1: // rank
            {
                cout << "\n!---- RANK ----!\n\n";
                cout << "Enter rank (0 or 1, 0 being more important): ";
                cin >> input;
                if (input.size() > 1)
                    break;
                readInt(input, rank);
                if (!(readInt(input, rank)) || rank < 0 || rank > 1)
                    break;
                cout << "Read rank: " << rank << "\n";
                stage++;
                break;
            }
            case 2: // traffic specification
            {
                cout << "\n!---- TRAFFIC SPECIFICATION ----!\n\n";
                cout << "Configure traffic specification (how the Talker transmits frames for the Stream):\n";
                cout << "Interval Numerator, Interval Denumerator, Max Frame Size, Max Frames per Interval, Transmission Selection:\n";
                cin >> input;
                if (!(getList(counter, inputList, input)) || (counter != 5))
                {
                    cout << "Wrong list input for traffic specification!\n";
                }
                else 
                {
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
                    stage++; 
                }
                break; 
            }
            case 3: // Time Aware
            {
                cout << "\n!---- TIME AWARENESS ----!\n\n";
                cout << "Shall the Talker be time-aware? (Y or N):\n";
                    while(true)
                    {
                        cin >> input;
                        if (input.compare("N") == 0)
                        {                            
                            stage+=2;
                            break;
                        }                        
                        else if (input.compare("Y") == 0)
                        {
                            stage++;
                            break;
                        }
                        cout << "Enter Y or N\n";
                    }
                break;
            }      
            case 4: // Time aware Talker
            {
                cout << "Time aware talker: Enter values for earliest transmit offset, latest transmit offset and jitter\n";
                cin >> input;
                if ((getList(counter, inputList, input)) && (counter == 3))
                {
                    traffic_specification.time_aware.earliest_transmit_offset = *(inputList.begin());
                    inputList.pop_front();
                    traffic_specification.time_aware.latest_transmit_offset = *(inputList.begin());
                    inputList.pop_front();
                    traffic_specification.time_aware.jitter = *(inputList.begin());
                    cout << "Saving settings:\n" << "Earliest Transmit Offset: " << traffic_specification.time_aware.earliest_transmit_offset <<
                        " latest transmit offset: " << traffic_specification.time_aware.latest_transmit_offset << " jitter: " <<
                        traffic_specification.time_aware.jitter << "\n";
                    stage++;
                }
                break;
            }
            case 5: // user to network requirements
            {
                cout << "\n!---- USER TO NETWORK REQUIREMENTS ----!\n\n";
                cout << "User to network requirements: Number of seamless trees and maximum latency\n";
                cin >> input;
                if ((getList(counter, inputList, input)) || (counter == 3))
                {
                    user_to_network_requirements.num_seamless_trees = *(inputList.begin());
                    inputList.pop_front();
                    user_to_network_requirements.max_latency = *(inputList.begin());
                    cout << "Saving settings:\n" << "Number of seamless trees: " << user_to_network_requirements.num_seamless_trees <<
                        " maximum latency: " << user_to_network_requirements.max_latency << "\n";
                    stage++;
                }
                break;
            }
            case 6: // interface capabilities
            {
                cout << "\n!---- INTERFACE CAPABILITIES ----!\n\n";
                cout << "Interface capabilities: is the talker interface able to VLAN tag frames? (Y or N)\n";
                while(true)
                {
                    cout << "Enter Y or N\n";
                    cin >> input;
                    if (input.compare("N") == 0)
                    {
                        interface_capabilities.vlan_tag_capable = false;
                        break;
                    }
                    else if (input.compare("Y") == 0)
                    {
                        interface_capabilities.vlan_tag_capable = true;
                        break;
                    }
                }
                stage++;
            }
            case 7: // CB Stream iden type or sequence type add
            {
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
                        if (contains(interface_capabilities.cb_stream_iden_type_list, identType))
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
                        if (contains(interface_capabilities.cb_sequence_type_list, sequenceType))
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
                        std::list<int>::iterator it;
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
                                readInt(input, choice);
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
                                readInt(input, choice);
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
                        stage++;
                        break;
                    }
                }
                break;
            }
            case 8: // end station interfaces
            {
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
                        end_station_interface_t interface;
                        cout << "Creating a new interface:\nEnter Interface Name\n";
                        cin >> interface.interface_name;
                        cout << "Enter MAC Address:\n";
                        cin >> interface.mac_address;
                        cout << "trying to add interface: " << interface.interface_name << " mac: " << interface.mac_address << "\n";
                        if(talker.add_interface(interface))                        
                            break;
                        cout << "Interface name already existing!\n";
                        break;
                    }
                    case 2: // List interfaces
                    {
                        auto it = talker.end_station_interface_list.begin();
                        for (uint i = 0; i < talker.end_station_interface_list.size(); i++)
                        {
                            cout << i << " - Name: " << (*it).interface_name << " Address: " << (*it).mac_address << "\n";
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
                        stage++;
                        break;
                    }
                }
                break;
            }
            case 9: // data frame specification
            {
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
                        choice_t choice;
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
                                        *choice.source_mac_address = input;
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
                                        *choice.destination_mac_address = input;
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
                                    cin >> input;
                                    if (readInt(input, *choice.pcp))
                                        break;
                                    cout << "Wrong format. Try again.\n";
                                }
                                while(true)
                                {
                                    cout << "Enter VLAN id:\n";
                                    cin >> input;
                                    if (readInt(input, *choice.vlan_id))
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
                                        *choice.ipv4_source_ip_address = input;
                                        break;
                                    }
                                    cout << "Wrong format. Try again.\n";
                                }
                                while(true)
                                {
                                    cout << "Enter source port:\n";
                                    cin >> input;
                                    if (readInt(input, *choice.source_port))
                                        break;
                                    cout << "Wrong format. Try again.\n";
                                }
                                while(true)
                                {
                                    cout << "Enter destination IP address:\n";
                                    cin >> input;
                                    if (readIpv4(input))
                                    {
                                        *choice.ipv4_destination_ip_address = input;
                                        break;
                                    }
                                    cout << "Wrong format. Try again.\n";
                                }
                                while(true)
                                {
                                    cout << "Enter destination port:\n";
                                    cin >> input;
                                    if (readInt(input, *choice.destination_port))
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
                                        *choice.ipv6_source_ip_address = input;
                                        break;
                                    }
                                    cout << "Wrong format. Try again.\n";
                                }
                                while(true)
                                {
                                    cout << "Enter source port:\n";
                                    cin >> input;
                                    if (readInt(input, *choice.source_port))
                                        break;
                                    cout << "Wrong format. Try again.\n";
                                }
                                while(true)
                                {
                                    cout << "Enter destination address:\n";
                                    cin >> input;
                                    if (readIpv6(input))
                                    {
                                        *choice.ipv6_destination_ip_address = input;
                                        break;
                                    }
                                    cout << "Wrong format. Try again.\n";
                                }    
                                while(true)
                                {
                                    cout << "Enter destination port:\n";
                                    cin >> input;
                                    if (readInt(input, *choice.destination_port))
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
                        data_frame_specification_t spec(choice);
                        talker.add_specification(spec);
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
                            cin >> input;
                            int index;
                            if (readInt(input, index))
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
                        stage++;
                        break;
                    }
                }
                break;
            }
            case 10:    // incorporate the data into the talker object and add it to the module in shared memory
            {
                talker.traffic_specification = traffic_specification;
                talker.user_to_network_requirements = user_to_network_requirements;
                talker.interface_capabilities = interface_capabilities;
                talker.stream_rank.rank = rank;
                cout << "Adding talker with id: " << talker.id << " to module...\n";
                module.addTalker(talker);
                return 1;
            }
        }
    }
    return 0;
}

int createListener()
{
    return 0;
}

int main( int argc, char* argv[] ) 
{
    #ifdef CATCH_CONFIG_RUNNER
    int result = 0;
    result = Catch::Session().run( argc, argv );
    return 1;
    #endif 

    // boost::interprocess::shared_memory_object::remove("cuc-dect");
    boost::interprocess::managed_shared_memory managed_shm{boost::interprocess::open_only, "cuc-dect"};
    cout << "reading from shared memory:\n";
    std::pair<device_t*, std::size_t> p = managed_shm.find_no_lock<device_t>("device");
    if (p.first)
    {
        std::cout << p.first << '\n';
        std::cout << &((p.first)->id) << "\n";
        std::cout << (p.first)->name2 << "\n";
        // boost::container::string str;
        // str.append("test");
        std::cout << (p.first)->id << "\n";

        std::cout << "the name: " <<  (p.first)->name2 << "\n";
    }
    // module_t *module_shm = managed_shm.construct<module_t>("tsn-cuc-dect-module")();
    // cout << "creating a device\n";
    // device_t device("test", 12345);
    // module_shm->addDevice(device);
    // std::pair<module_t*, std::size_t> p = managed_shm.find<module_t>("tsn-cuc-dect-module");
    // cout << "The id: " << (*(p.first->devicesList.begin())).getPmid() << "\n";
    // std::pair<int*, std::size_t> p = managed_shm.find<int>("Integer");
    // if (p.first)
    //     std::cout << *p.first << '\n';

    int mainMenu = 1;

    module_t module;
    device_t device1("test", 123);
    device_t device2("test1", 1234);
    device_t device3("test2", 1235);
    device_t device4("test3", 1236);
    device_t device5("test4", 1237);
    module.addDevice(device1);
    module.addDevice(device2);
    module.addDevice(device3);
    module.addDevice(device4);
    module.addDevice(device5);

    while (mainMenu)
    {
        cout << "\n!---- MAIN MENU ----!\n\n";
        cout << "Select option\nShow Devices: D\nCreate Talker: T\nCreateListener: L\nListTalkers: LT\n";
        std::string input;
        cin >> input;
        if ((input.size() < 0) || (input.size() > 2))
        {
            cout << "invalid input: wrong length\n";
        }
        else if (input.compare("D") == 0)
        {
            cout << "\n!---- SHOW DEVICES ----!\n\n";
            cout << module.devicesList.size() << " registered Devices:\n";
            auto it = module.devicesList.begin();
            for (uint i = 0; i<module.devicesList.size(); i++)
            {
                cout << "\t" << (*it).getId() << " - " << (*it).getName() << " - " << (*it).getPmid() << "\n";
                it++;
            }
        }
        else if (input.compare("T") == 0)
        {
            cout << "\n!---- CREATING A TALKER ----!\n\n";
            int result = createTalker(module);
            cout << result << "\n";
            cout << "talker with id " << (*(module.talkersList.begin())).id << " was added.\n";
        }
        else if (input.compare("L") == 0)
        {
            cout << "\n!---- CREATING A LISTENER ----!\n\n";
            cout << "Creating Listener\n";
        }
        else if (input.compare("LT") == 0)
        {
            cout << "\n!---- LISTING THE TALKERS ----!\n\n";
            cout << "Number of registered talkers: " << module.talkersList.size() << "\n\n";
            auto it = module.talkersList.begin();
            cout << "Device IDs registered as talkers:\n";
            int id;
            pmid_t pmid;
            for (uint i = 0; i<module.talkersList.size(); i++)
            {
                id = (*it).getId();
                for (std::list<device_t>::iterator it2 = module.devicesList.begin(); 
                    it2 != module.devicesList.end(); it2++)
                    if ((*it2).getId() == id)
                        pmid = (*it2).getPmid();
                cout << "\t" << std::distance(module.talkersList.begin(), it) << ": Device ID " << id << " with PMID " << pmid << "\n";
                it++;
            }
            cout << "\nEnter entry number to list data or (q) to quit\n";
            int entry;
            // talker_t* talkerptr;
            while (true)
            {
                cin >> input;
                if (input.compare("q") == 0)
                    break;
                else if (readInt(input, entry))
                {
                    if ((entry < 0) || ((uint)entry >= module.talkersList.size()))
                    {
                        cout << "Entry not found.\n";
                        continue;
                    }
                    it = module.talkersList.begin();
                    advance(it, entry);
                    (*it).printData();
                    break;
                }
            }
        }
        else
        {
            cout << "Invalid input: wrong char\n";
        }
        input.clear();
    }


    // ftok to generate unique key 
    //key_t key = ftok("shmfile",65); 
  
    // shmget returns an identifier in shmid 
    //int shmid = shmget(key,1024,0666);
  
    // shmat to attach to shared memory 
    // device_t *ptr = (device_t*) shmat(shmid,(void*)0,0);
 
    // ptr->id = 3;
 
    // ptr->name = "test";

    // int i = 0;
    // while(true) 
    // {
    //     i++;
    //     ptr->id = i;
    //     printf("New Data written:%d\n", ptr->id);
    //     sleep(5);
    // }

    //detach from shared memory  
    //shmdt(ptr); 
  
    return 0; 
} 