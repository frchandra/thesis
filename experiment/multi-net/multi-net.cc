#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/mobility-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MultiNetExample");

int main(){

    Config::SetDefault("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue(true));
    LogComponentEnable("UdpClient", LOG_LEVEL_INFO);

    NodeContainer c;
    c.Create(4);

    NodeContainer n0n1 = NodeContainer(c.Get(0), c.Get(1));
    NodeContainer n0n2 = NodeContainer(c.Get(0), c.Get(2));
    NodeContainer n1n3 = NodeContainer(c.Get(1), c.Get(3));
    NodeContainer n2n3 = NodeContainer(c.Get(2), c.Get(3));

    InternetStackHelper internet;
    internet.Install(c);

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));
    NetDeviceContainer d0d1 = csma.Install(n0n1);
    NetDeviceContainer d0d2 = csma.Install(n0n2);
    NetDeviceContainer d1d3 = csma.Install(n1n3);
    NetDeviceContainer d2d3 = csma.Install(n2n3);

    Ipv4AddressHelper ipv4;

    ipv4.SetBase("10.0.1.0", "255.255.255.0");
    ipv4.Assign(d0d1);
    ipv4.SetBase("10.0.2.0", "255.255.255.0");
    ipv4.Assign(d0d2);
    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer i1i3 = ipv4.Assign(d1d3);
    ipv4.SetBase("10.2.3.0", "255.255.255.0");
    Ipv4InterfaceContainer i2i3 = ipv4.Assign(d2d3);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    //Create TCP onoff server helper
    OnOffHelper onoff("ns3::TcpSocketFactory", InetSocketAddress(i2i3.GetAddress(1), 3000));
    onoff.SetConstantRate(DataRate("2kbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(50));
    ApplicationContainer apps1 = onoff.Install(c.Get(0));
    apps1.Start(Seconds(1.0));
    apps1.Stop(Seconds(10.0));
    ApplicationContainer apps2 = onoff.Install(c.Get(0));
    apps2.Start(Seconds(1.0));
    apps2.Stop(Seconds(10.0));

    //Create TCP bulk send
//    BulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(i2i3.GetAddress(1), 3000));
//    source.SetAttribute("MaxBytes", UintegerValue(1000000));
//    ApplicationContainer sourceApps = source.Install(c.Get(0));
//    sourceApps.Start(Seconds(0.0));
//    sourceApps.Stop(Seconds(10.0));

    PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 3000));
    ApplicationContainer sinkApps = sink.Install(c.Get(3));
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(10.0));


    Ptr<Ipv4> ip0 = c.Get(0)->GetObject<Ipv4>();
    uint32_t ipv4ifIndex1 = 1;
    uint32_t ipv4ifIndex2 = 2;

    Ptr<Ipv4> ip1 = c.Get(1)->GetObject<Ipv4>();
    Ptr<Ipv4> ip2 = c.Get(2)->GetObject<Ipv4>();




    Simulator::Schedule(Seconds(0), &Ipv4::SetUp, ip0, ipv4ifIndex1);
    Simulator::Schedule(Seconds(0), &Ipv4::SetUp, ip1, 1);
    Simulator::Schedule(Seconds(0), &Ipv4::SetDown, ip0, ipv4ifIndex2);
    Simulator::Schedule(Seconds(0), &Ipv4::SetDown, ip2, 1);

    Simulator::Schedule(Seconds(0), &Ipv4GlobalRoutingHelper::RecomputeRoutingTables);

    Simulator::Schedule(Seconds(4), &Ipv4::SetDown, ip0, ipv4ifIndex1);
    Simulator::Schedule(Seconds(4), &Ipv4::SetDown, ip1, 1);
    Simulator::Schedule(Seconds(4), &Ipv4::SetUp, ip0, ipv4ifIndex2);
    Simulator::Schedule(Seconds(4), &Ipv4::SetUp, ip2, 1);

    Simulator::Schedule(Seconds(8), &Ipv4::SetUp, ip0, ipv4ifIndex1);
    Simulator::Schedule(Seconds(8), &Ipv4::SetUp, ip1, 1);
    Simulator::Schedule(Seconds(8), &Ipv4::SetDown, ip0, ipv4ifIndex2);
    Simulator::Schedule(Seconds(8), &Ipv4::SetDown, ip2, 1);

    AnimationInterface animationInterface("multi-net.xml");
    animationInterface.SetConstantPosition(c.Get(0), 10.0, 30.0);
    animationInterface.SetConstantPosition(c.Get(1), 30.0, 10.0);
    animationInterface.SetConstantPosition(c.Get(2), 30.0, 50.0);
    animationInterface.SetConstantPosition(c.Get(3), 50.0, 30.0);

//    csma.EnablePcap("multi-net", d0d1.Get(0), true);
    csma.EnablePcap("multi-net", d0d2.Get(0), true);


    NS_LOG_INFO("Run Simulation.");
    Simulator::Run();
    Simulator::Destroy();
    NS_LOG_INFO("Run Simulation.");

    Ptr<PacketSink> sink1 = DynamicCast<PacketSink>(sinkApps.Get(0));
    std::cout << "Total Bytes Received: " << sink1->GetTotalRx() << std::endl;

}