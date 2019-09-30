#include "catch.hpp"
#ifndef TSN_CUC_DECT
#define TSN_CUC_DECT
#include "tsn-cuc-dect.hpp"
#endif

#include "tsn-cuc-dect.cpp"

#ifdef CATCH_CONFIG_RUNNER
TEST_CASE ("Talker test", "[talkers]")
{
    user_to_network_requirements_t user_to_network_requirements;
    user_to_network_requirements.max_latency = 1;
    user_to_network_requirements.num_seamless_trees = 1;
    interface_capabilities_t interface_capabilities;
    interface_capabilities.vlan_tag_capable = true;
    interface_capabilities.cb_sequence_type_list.push_front(1);
    interface_capabilities.cb_sequence_type_list.push_back(2);
    interface_capabilities.cb_stream_iden_type_list.push_front(3);
    interface_capabilities.cb_stream_iden_type_list.push_back(4);
    traffic_specification_t traffic_specification;
    traffic_specification.interval.denominator = 2;
    traffic_specification.interval.numerator = 1;
    traffic_specification.max_frame_size = 11;
    traffic_specification.max_frames_per_interval = 22;
    traffic_specification.time_aware.earliest_transmit_offset = 3;
    traffic_specification.time_aware.jitter = 4;
    traffic_specification.time_aware.latest_transmit_offset = 5;
    traffic_specification.transmission_selection = 33;
    talker_t talker(1,1, traffic_specification, user_to_network_requirements, interface_capabilities);
    // Test the overall data in the talker
    REQUIRE(talker.id == 1);
    REQUIRE(talker.stream_rank.rank == 1);
    REQUIRE(talker.interface_capabilities.vlan_tag_capable == interface_capabilities.vlan_tag_capable);
    REQUIRE(*(talker.interface_capabilities.cb_sequence_type_list.begin()) == 1);
    REQUIRE(*(++(talker.interface_capabilities.cb_sequence_type_list.begin())) == 2);
    REQUIRE(*((talker.interface_capabilities.cb_stream_iden_type_list.begin())) == 3);
    REQUIRE(*(++(talker.interface_capabilities.cb_stream_iden_type_list.begin())) == 4);
    REQUIRE(talker.user_to_network_requirements.max_latency == 1);
    REQUIRE(talker.user_to_network_requirements.num_seamless_trees == 1);
    // Test the traffic specification
    REQUIRE(talker.traffic_specification.interval.denominator == traffic_specification.interval.denominator);
    REQUIRE(talker.traffic_specification.interval.numerator == traffic_specification.interval.numerator);
    REQUIRE(talker.traffic_specification.max_frames_per_interval == 22);
    REQUIRE(talker.traffic_specification.max_frame_size == 11);
    REQUIRE(talker.traffic_specification.transmission_selection == 33);
    REQUIRE(talker.traffic_specification.time_aware.earliest_transmit_offset == traffic_specification.time_aware.earliest_transmit_offset);
    REQUIRE(talker.traffic_specification.time_aware.jitter == traffic_specification.time_aware.jitter);
    REQUIRE(talker.traffic_specification.time_aware.latest_transmit_offset == traffic_specification.time_aware.latest_transmit_offset);
    // Test the end station interfaces
    end_station_interface_t testinterface;
    testinterface.interface_name = "NAME1";
    testinterface.mac_address = "MAC1";
    end_station_interface_t testinterface2;
    testinterface2.interface_name = "NAME2";
    testinterface2.mac_address = "MAC2";
    talker.add_interface(testinterface);
    talker.add_interface(testinterface2);
    auto iter = talker.end_station_interface_list.begin();
    REQUIRE((*(iter)).interface_name == testinterface.interface_name);
    REQUIRE((*(++(iter))).mac_address == testinterface2.mac_address);
    // !---- OBSOLETE ----!
    // create data frame Specifications
    // create testchoices, create specs, add the specs to the list
    // ieee802_mac_addresses_t testchoice0;
    // testchoice0.destination_mac_address = "dest mac ";
    // testchoice0.source_mac_address = "source mac";
    // data_frame_specification_t testspec0 = data_frame_specification_t(MAC, (void*)&testchoice0);
    // talker.add_specification(&testspec0);
    // ieee802_vlan_tag_t testchoice;
    // testchoice.pcp = 1;
    // testchoice.vlan_id = 2;
    // data_frame_specification_t testspec = data_frame_specification_t(VLAN, (void*)&testchoice);
    // talker.add_specification(&testspec);
    // ipv4_tuple_t testchoice2;
    // testchoice2.destination_ip_address = "dest ip";
    // testchoice2.destination_port = 1;
    // testchoice2.dscp = 1;
    // testchoice2.protocol = 1;
    // testchoice2.source_ip_address = "source ip";
    // testchoice2.source_port = 2;
    // data_frame_specification_t testspec2 = data_frame_specification_t(IPV4, (void*)&testchoice2);
    // talker.add_specification(&testspec2);
    // ipv6_tuple_t testchoice3;
    // testchoice3.destination_ip_address = "dest ip";
    // testchoice3.destination_port = 11;
    // testchoice3.dscp = 23;
    // testchoice3.protocol = 22;
    // testchoice3.source_ip_address = "source ip";
    // testchoice3.source_port = 24;
    // data_frame_specification_t testspec3 = data_frame_specification_t(IPV6, (void*)&testchoice3);
    // talker.add_specification(&testspec3);
    // auto it = talker.data_frame_specification_list.begin();
    // REQUIRE(testspec0.ptr == &testchoice0);
    // REQUIRE(*(talker.data_frame_specification_list.begin()) == &testspec0);
    // for (int i = 0; i<(int)(talker.data_frame_specification_list.size()); i++)
    // {
    //     data_frame_specification_t* restore = (data_frame_specification_t*)*it;

    //     switch (restore->choice)
    //     {
    //         case (MAC):
    //         {
    //             ieee802_mac_addresses_t* newPtr = (ieee802_mac_addresses_t*)(restore->ptr);
    //             REQUIRE(newPtr->destination_mac_address == testchoice0.destination_mac_address);
    //             REQUIRE(newPtr->source_mac_address == testchoice0.source_mac_address);
    //             break;
    //         }
                
    //         case (VLAN):
    //         {
    //             ieee802_vlan_tag_t* newPtr = (ieee802_vlan_tag_t*)(restore->ptr);
    //             REQUIRE(newPtr->pcp == testchoice.pcp);
    //             REQUIRE(newPtr->vlan_id == testchoice.vlan_id);
    //             break;
    //         }
    //         case (IPV4):
    //         {
    //             ipv4_tuple_t* newPtr = (ipv4_tuple_t*)(restore->ptr);
    //             REQUIRE(newPtr->destination_ip_address == testchoice2.destination_ip_address);
    //             REQUIRE(newPtr->destination_port == testchoice2.destination_port);
    //             REQUIRE(newPtr->dscp == testchoice2.dscp);
    //             REQUIRE(newPtr->protocol == testchoice2.protocol);
    //             REQUIRE(newPtr->source_ip_address == testchoice2.source_ip_address);
    //             REQUIRE(newPtr->source_port == testchoice2.source_port);
    //             break;
    //         }
    //         case (IPV6):
    //         {
    //             ipv6_tuple_t* newPtr = (ipv6_tuple_t*)(restore->ptr);
    //             REQUIRE(newPtr->destination_ip_address == testchoice3.destination_ip_address);
    //             REQUIRE(newPtr->destination_port == testchoice3.destination_port);
    //             REQUIRE(newPtr->dscp == testchoice3.dscp);
    //             REQUIRE(newPtr->protocol == testchoice3.protocol);
    //             REQUIRE(newPtr->source_ip_address == testchoice3.source_ip_address);
    //             REQUIRE(newPtr->source_port == testchoice3.source_port);
    //             break;
    //         }
    //     }
    //     it++;
    // }
}

TEST_CASE("Listener Tests", "[testListener]")
{
    // Test the testListener
    end_station_interface_t testinterface;
    testinterface.interface_name = "NAME1";
    testinterface.mac_address = "MAC1";
    end_station_interface_t testinterface2;
    testinterface2.interface_name = "NAME2";
    testinterface2.mac_address = "MAC2";
    user_to_network_requirements_t user_to_network_requirements;
    user_to_network_requirements.max_latency = 1;
    user_to_network_requirements.num_seamless_trees = 1;
    interface_capabilities_t interface_capabilities;
    interface_capabilities.vlan_tag_capable = true;
    interface_capabilities.cb_sequence_type_list.push_front(1);
    interface_capabilities.cb_sequence_type_list.push_back(2);
    interface_capabilities.cb_stream_iden_type_list.push_front(3);
    interface_capabilities.cb_stream_iden_type_list.push_back(4);
    listener_t testListener(1, user_to_network_requirements, interface_capabilities);
    testListener.add_interface(testinterface);
    testListener.add_interface(testinterface2);
    REQUIRE(testListener.id == 1);
    REQUIRE(testListener.interface_capabilities.vlan_tag_capable == interface_capabilities.vlan_tag_capable);
    REQUIRE(*(testListener.interface_capabilities.cb_sequence_type_list.begin()) == 1);
    REQUIRE(*(++(testListener.interface_capabilities.cb_sequence_type_list.begin())) == 2);
    REQUIRE(*((testListener.interface_capabilities.cb_stream_iden_type_list.begin())) == 3);
    REQUIRE(*(++(testListener.interface_capabilities.cb_stream_iden_type_list.begin())) == 4);
    REQUIRE(testListener.user_to_network_requirements.max_latency == user_to_network_requirements.max_latency);
    REQUIRE(testListener.user_to_network_requirements.num_seamless_trees == user_to_network_requirements.num_seamless_trees);
    // Interfaces
    end_station_interface_t testinterface3;
    testinterface3.interface_name = "NAME3";
    testinterface3.mac_address = "MAC3";
    end_station_interface_t testinterface4;
    testinterface4.interface_name = "NAME4";
    testinterface4.mac_address = "MAC4";
    testListener.add_interface(testinterface3);
    testListener.add_interface(testinterface4);
    auto iter = testListener.end_station_interface_list.begin();
    end_station_interface_t restore = (end_station_interface_t) *iter;
    REQUIRE((*(iter)).interface_name == (testinterface.interface_name));
    REQUIRE(restore.interface_name == testinterface.interface_name);
    REQUIRE(restore.mac_address == testinterface.mac_address);
    iter++;
    restore = (end_station_interface_t) *iter;
    REQUIRE((*(iter)).mac_address == (testinterface2.mac_address));
    REQUIRE(restore.interface_name == testinterface2.interface_name);
    REQUIRE(restore.mac_address == testinterface2.mac_address);
}

TEST_CASE ("Streams Tests" ,"[stream]")
{
    // Stream Info
    status_info_t status_info;
    status_info.failure_code = 0;
    status_info.listener_status = L_READY;
    status_info.talker_status = T_READY;
    status_stream_t stream("TestStreamId", status_info);
    end_station_interface_t failedInterface;
    failedInterface.interface_name = "fail1";
    failedInterface.mac_address = "MAC f1";
    end_station_interface_t failedInterface2;
    failedInterface2.interface_name = "fail2";
    failedInterface2.mac_address = "MAC f2";   
    REQUIRE(stream.failedInterfacesList.size() == 0);
    stream.add_interface(failedInterface);
    stream.add_interface(failedInterface2);
    REQUIRE(stream.stream_id == "TestStreamId");
    REQUIRE(stream.status_info.failure_code == status_info.failure_code);
    REQUIRE(stream.status_info.listener_status == status_info.listener_status);
    REQUIRE(stream.status_info.talker_status == status_info.talker_status);
    auto it = stream.failedInterfacesList.begin();
    REQUIRE(stream.failedInterfacesList.size() == 2);
    REQUIRE(((end_station_interface_t)*(it)).interface_name == failedInterface.interface_name);
    REQUIRE(((end_station_interface_t)*(it)).mac_address == failedInterface.mac_address);
    it++;
    REQUIRE(((end_station_interface_t)*(it)).interface_name == failedInterface2.interface_name);
    REQUIRE(((end_station_interface_t)*(it)).mac_address == failedInterface2.mac_address);
}

TEST_CASE("checkId", "[ID]")
{
    std::list<device_t> test_devices;
    device_t test_device("test", 123);
    device_t test_device2("test2", 1232);
    device_t test_device3("test3", 1233);
    test_devices.push_front(test_device);
    test_devices.push_front(test_device2);
    test_devices.push_front(test_device3);
    auto it = test_devices.begin();
    REQUIRE(checkId(5, test_devices, it) == 0);
    REQUIRE(checkId(-1, test_devices, it) == 0);
    REQUIRE(checkId(0, test_devices, it) == 1);
    REQUIRE(checkId(1, test_devices, it) == 1);
    REQUIRE(checkId(2, test_devices, it) == 1);
    REQUIRE((*it).getId() == (test_device3.getId()));
}

TEST_CASE("getInterval", "[INTERVAL]")
{
    interval_t interval;
    interval.numerator = 123;
    interval.denominator = 456; 
    interval_t result = getInterval("123-456");
    REQUIRE(interval.denominator == result.denominator);
    REQUIRE(interval.numerator == result.numerator);
    REQUIRE(getInterval("1235").denominator == -1);
    REQUIRE(getInterval("1234-").numerator == -1);
}

TEST_CASE("getList", "[LIST]")
{
    std::list<int> testlist;
    int counter;
    REQUIRE(getList(counter, testlist, "") == 0);
    REQUIRE(getList(counter, testlist, "1,") == 0);
    REQUIRE(getList(counter, testlist, ",2,3,4,5,6") == 0);
    REQUIRE(getList(counter, testlist, "1,2,3,") == 0);
    REQUIRE(getList(counter, testlist, "1,2,a,4,5,6") == 0);
    REQUIRE(getList(counter, testlist, "1,2,") == 0);
    REQUIRE(counter == 0);
    REQUIRE(testlist.size() == 0);
    REQUIRE(getList(counter, testlist, "1,2,,4,a,6") == 0);
    REQUIRE(testlist.size() == 0);
    REQUIRE(getList(counter, testlist, "1,2,3,4,5,6") == 1);
    REQUIRE(testlist.size() == 6);
    REQUIRE(counter == 6);
}

TEST_CASE("getItemFromList", "[ITEM]")
{
    std::list<int> testlist;
    testlist.clear();
    std::list<int>::iterator it;
    REQUIRE(printIntList(testlist) == 0);
    testlist.push_back(3);
    REQUIRE(printIntList(testlist) == 1);
    testlist.push_back(4);
    testlist.push_back(33);
    testlist.push_back(6);
    REQUIRE(getItemFromList(testlist, it, -1) == 0);
    REQUIRE(getItemFromList(testlist, it, 4) == 0);
    printIntList(testlist);
    REQUIRE(getItemFromList(testlist, it, 3) == 1);
    REQUIRE((*it) == 6);
    testlist.remove(*it);
    REQUIRE(getItemFromList(testlist, it, 3) == 0);
}

TEST_CASE("readMac", "[MAC]")
{
    REQUIRE(readMac("12:45:ff:ab:aa:cd") == 1);
    REQUIRE(readMac("12:45:FF:AB:AA:CD") == 1);
    REQUIRE(readMac("2:45:ff:ab:aa:cd") == 0);
    REQUIRE(readMac("12:45:ff:ab:aa:c") == 0);
    REQUIRE(readMac("12:45:f:ab:aa:cd") == 0);
    REQUIRE(readMac("12a:45:ff:ab:aa:cd") == 0);
    REQUIRE(readMac("12:45:ff:ab:aa:acd") == 0);
    REQUIRE(readMac("G2:45:ff:ab:aa:cd") == 0);
    REQUIRE(readMac("12:45:ff:ab:aa:cG") == 0);
    REQUIRE(readMac("12-45:ff:ab:aa:cd") == 0);
    REQUIRE(readMac("12:45:ff:ab:aa-cd") == 0);
    REQUIRE(readMac("12:45:ff-ab:aa:cd") == 0);
}

TEST_CASE("readIpv4", "[IPv4]")
{
    REQUIRE(readIpv4("255.168.0.1") == 1);
    REQUIRE(readIpv4("192.-168.1.1") == 0);
    REQUIRE(readIpv4("192.168.1") == 0);
    REQUIRE(readIpv4("g.168.1.1") == 0);
    REQUIRE(readIpv4("192.999.1.1") == 0);
    REQUIRE(readIpv4("255.255.255.255") == 1);
}

TEST_CASE("readIpv6", "[IPv6]")
{
    std::string a("001:0db8:85a3:5:0000:8a2e:0370:7334");
    std::string b("001:0db8:aaaa::0370:7334");
    std::string c("001::");
    std::string d("::0370:7334");
    std::string e("1234:1234:1234:1234::1234:1234:1234");
    REQUIRE(readIpv6(a));
    REQUIRE(readIpv6(b));
    REQUIRE(readIpv6(c));
    REQUIRE(readIpv6(d));   
    REQUIRE(readIpv6(e));
    REQUIRE(a.compare("0001:0db8:85a3:0005:0000:8a2e:0370:7334"));
    REQUIRE(b.compare("0001:0db8:aaaa:0000:0000:0000:0370:7334"));
    REQUIRE(c.compare("0001:0000:0000:0000:0000:0000:0000:0000"));
    REQUIRE(d.compare("0000:0000:0000:0000:0000:0000:0370:7334"));
    REQUIRE(e.compare("1234:1234:1234:1234:0000:1234:1234:1234"));
    REQUIRE(readIpv6("asdada") == 0);
    REQUIRE(!(readIpv6("AAAAA:AAAA:AAAA:AAAA:AAA::")));
    REQUIRE(!(readIpv6("AAAA:AAAA:AAAA:AAAA:AAA::AAAAA")));
    REQUIRE(!(readIpv6("AAAA:AAAA:AAAAA:AAAA:AAA::")));
    REQUIRE(!(readIpv6("AAAA::AAAA:AAAA:AAA::")));
    REQUIRE(!(readIpv6("AGAA:AAAA:AAAA:AAAA:AAA::")));
    REQUIRE(!(readIpv6("AAAA:AAAA:AAAA:AAAA:AAA::AAAG")));
}

TEST_CASE("removeEntryFromList", "[RFL]")
{
    std::list<int> testlist;
    testlist.push_back(1);  // 0
    testlist.push_back(2);  // 1    0   0
    testlist.push_back(3);  // 2    1   1
    testlist.push_back(4);  // 3    2   
    testlist.push_back(5);  // 4    3   2
    REQUIRE(*(--testlist.end()) == 5);
    REQUIRE(removeEntryFromList(testlist, -1) == 0);
    REQUIRE(testlist.size() == 5);
    REQUIRE(removeEntryFromList(testlist, 5) == 0);
    REQUIRE(testlist.size() == 5);
    REQUIRE(((std::distance(testlist.begin(), std::find(testlist.begin(), (--testlist.end()), 3))) == 2));
    REQUIRE(removeEntryFromList(testlist, 0) == 1); // remove 1 on 0
    REQUIRE(testlist.size() == 4);
    REQUIRE(((std::distance(testlist.begin(), std::find(testlist.begin(), (--testlist.end()), 3))) == 1));
    REQUIRE(removeEntryFromList(testlist, 2) == 1); // remove 4 on 2
    REQUIRE(((std::distance(testlist.begin(), std::find(testlist.begin(), (--testlist.end()), 3))) == 1));
    REQUIRE(testlist.size() == 3);
    REQUIRE(*(--testlist.end()) == 5);
    REQUIRE(removeEntryFromList(testlist, 2) == 1); // remove 5 on 2
    REQUIRE(*(--testlist.end()) == 3);
}
#endif