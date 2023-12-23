#include "ns3/applications-module.h"
#include "ns3/basic-energy-source.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/simple-device-energy-model.h"
#include "ns3/ssid.h"
#include "ns3/wifi-radio-energy-model.h"
#include "ns3/yans-wifi-helper.h"

using namespace ns3;

int main(){
    NodeContainer nodes;
    nodes.Create(6);

    NodeContainer n0n2 = NodeContainer(nodes.Get(0), nodes.Get(2));
    NodeContainer n1n2 = NodeContainer(nodes.Get(1), nodes.Get(2));
    NodeContainer n2n4 = NodeContainer(nodes.Get(2), nodes.Get(4));
    NodeContainer n4n3 = NodeContainer(nodes.Get(4), nodes.Get(3));
    NodeContainer n4n5 = NodeContainer(nodes.Get(4), nodes.Get(5));
    NodeContainer staNodes = NodeContainer(nodes.Get(0), nodes.Get(1));
    NodeContainer apNodes = NodeContainer(nodes.Get(2));
    NodeContainer csmaNodes = NodeContainer(nodes.Get(2), nodes.Get(3), nodes.Get(4), nodes.Get(5));

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    WifiHelper wifi;
    WifiMacHelper mac;
    Ssid ssid = Ssid("ns-3-ssid");
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNodes);
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevices = wifi.Install(phy, mac, apNodes);

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));
    NetDeviceContainer d2d4 = csma.Install(n2n4);
    NetDeviceContainer d4d3 = csma.Install(n4n3);
    NetDeviceContainer d4d5 = csma.Install(n4n5);

    MobilityHelper mobility;
    mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                  "MinX", DoubleValue (0.0),
                                  "MinY", DoubleValue (0.0),
                                  "DeltaX", DoubleValue (1.0),
                                  "DeltaY", DoubleValue (1.0),
                                  "GridWidth", UintegerValue (1),
                                  "LayoutType", StringValue ("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    AnimationInterface::SetConstantPosition(nodes.Get(0), 10, 10);
    AnimationInterface::SetConstantPosition(nodes.Get(1), 10, 50);
    AnimationInterface::SetConstantPosition(nodes.Get(2), 30, 30);
    AnimationInterface::SetConstantPosition(nodes.Get(3), 70, 10);
    AnimationInterface::SetConstantPosition(nodes.Get(4), 50, 30);
    AnimationInterface::SetConstantPosition(nodes.Get(5), 70, 50);

    Ptr<BasicEnergySource> energySource = CreateObject<BasicEnergySource>();
    Ptr<WifiRadioEnergyModel> energyModel = CreateObject<WifiRadioEnergyModel>();
    energySource->SetInitialEnergy(300);
    energyModel->SetEnergySource(energySource);
    energySource->AppendDeviceEnergyModel(energyModel);
    apNodes.Get(0)->AggregateObject(energySource);

    InternetStackHelper internet;
    internet.Install(nodes);
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.10.2.0", "255.255.255.0");
    Ipv4InterfaceContainer i10i2 = ipv4.Assign(staDevices);
    Ipv4InterfaceContainer i1i2 = ipv4.Assign(apDevices);
    ipv4.SetBase("10.2.4.0", "255.255.255.0");
    Ipv4InterfaceContainer i2i4 = ipv4.Assign(d2d4);
    ipv4.SetBase("10.4.3.0", "255.255.255.0");
    Ipv4InterfaceContainer i4i3 =  ipv4.Assign(d4d3);
    ipv4.SetBase("10.4.5.0", "255.255.255.0");
    Ipv4InterfaceContainer i4i5 = ipv4.Assign(d4d5);
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    OnOffHelper source("ns3::TcpSocketFactory", InetSocketAddress(i4i3.GetAddress(1), 3000));
    source.SetConstantRate(DataRate("2kbps"));
    source.SetAttribute("PacketSize", UintegerValue(50));
    ApplicationContainer sourceApps = source.Install(nodes.Get(0));
    PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(i4i3.GetAddress(1), 3000));
    ApplicationContainer sinkApps = sink.Install(nodes.Get(3));

    sinkApps.Start (Seconds (0.0));
    sinkApps.Stop (Seconds (9));
    sourceApps.Start (Seconds (1));
    sourceApps.Stop (Seconds (9));

    AnimationInterface anim("dumbbell-wireless.xml");
    for (uint32_t i = 0; i < staNodes.GetN(); ++i)
    {
        anim.UpdateNodeDescription(staNodes.Get(i), "STA"); // Optional
        anim.UpdateNodeColor(staNodes.Get(i), 255, 0, 0);   // Optional
    }
    for (uint32_t i = 0; i < apNodes.GetN(); ++i)
    {
        anim.UpdateNodeDescription(apNodes.Get(i), "AP"); // Optional
        anim.UpdateNodeColor(apNodes.Get(i), 0, 255, 0);  // Optional
    }
    for (uint32_t i = 0; i < csmaNodes.GetN(); ++i)
    {
        anim.UpdateNodeDescription(csmaNodes.Get(i), "CSMA"); // Optional
        anim.UpdateNodeColor(csmaNodes.Get(i), 0, 0, 255);    // Optional
    }

    anim.EnablePacketMetadata(); // Optional
    anim.EnableIpv4RouteTracking("routingtable-wireless.xml",
                                 Seconds(0),
                                 Seconds(5),
                                 Seconds(0.25));         // Optional
    anim.EnableWifiMacCounters(Seconds(0), Seconds(10)); // Optional
    anim.EnableWifiPhyCounters(Seconds(0), Seconds(10)); // Optional

    csma.EnablePcap("dumbbell-wireless", d2d4.Get(0), true);

    Simulator::Stop (Seconds (10));
    Simulator::Run ();
    Simulator::Destroy ();
}