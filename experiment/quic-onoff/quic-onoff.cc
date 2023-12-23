#include <fstream>
#include <string>

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/quic-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/packet-sink.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("quic-onoff");

int
main (){
    NodeContainer nodes;
    nodes.Create (2);


    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
    pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install (nodes);

    QuicHelper stack;
    stack.InstallQuic (nodes);

    Ipv4AddressHelper address;
    address.SetBase ("10.1.1.0", "255.255.255.0");

    Ipv4InterfaceContainer interfaces = address.Assign (devices);

    OnOffHelper onoff("ns3::QuicSocketFactory", InetSocketAddress(interfaces.GetAddress(1), 10000));
    onoff.SetConstantRate(DataRate("2kbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(50));
    ApplicationContainer client1 = onoff.Install(nodes.Get(0));

    PacketSinkHelper sink("ns3::QuicSocketFactory", InetSocketAddress(interfaces.GetAddress(1), 10000));
    ApplicationContainer server1 = sink.Install(nodes.Get(1));


    server1.Start (Seconds (0.0));
    server1.Stop (Seconds (9));
    client1.Start (Seconds (1));
    client1.Stop (Seconds (9));

    pointToPoint.EnablePcapAll("quic-onoff", true);

    Simulator::Stop (Seconds (10));
    Simulator::Run ();
    Simulator::Destroy ();
}