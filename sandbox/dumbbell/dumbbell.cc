#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-helper.h"
#include "ns3/flow-monitor.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"


#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ExperimentDumbbell");

void performance(FlowMonitorHelper &flowmon, Ptr<FlowMonitor> monitor ){
    uint32_t SentPackets = 0;
    uint32_t ReceivedPackets = 0;
    uint32_t LostPackets = 0;
    int j=0;
    float AvgThroughput = 0;
    Time Jitter;
    Time Delay;
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin (); iter != stats.end (); ++iter){
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (iter->first);
        NS_LOG_UNCOND("----Flow ID:" <<iter->first);
        NS_LOG_UNCOND("Src Addr" <<t.sourceAddress << "Dst Addr "<< t.destinationAddress);
        NS_LOG_UNCOND("Sent Packets=" <<iter->second.txPackets);
        NS_LOG_UNCOND("Received Packets =" <<iter->second.rxPackets);
        NS_LOG_UNCOND("Lost Packets =" <<iter->second.txPackets-iter->second.rxPackets);
        NS_LOG_UNCOND("Packet delivery ratio =" <<iter->second.rxPackets*100/iter->second.txPackets << "%");
        NS_LOG_UNCOND("Packet loss ratio =" << (iter->second.txPackets-iter->second.rxPackets)*100/iter->second.txPackets << "%");
        NS_LOG_UNCOND("Delay =" <<iter->second.delaySum);
        NS_LOG_UNCOND("Jitter =" <<iter->second.jitterSum);
        NS_LOG_UNCOND("Throughput =" <<iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds())/1024<<"Kbps");
        SentPackets = SentPackets +(iter->second.txPackets);
        ReceivedPackets = ReceivedPackets + (iter->second.rxPackets);
        LostPackets = LostPackets + (iter->second.txPackets-iter->second.rxPackets);
        AvgThroughput = AvgThroughput + (iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds())/1024);
        Delay = Delay + (iter->second.delaySum);
        Jitter = Jitter + (iter->second.jitterSum);
        j = j + 1;
    }
    AvgThroughput = AvgThroughput/j;
    NS_LOG_UNCOND("--------Total Results of the simulation----------"<<std::endl);
    NS_LOG_UNCOND("Total sent packets  =" << SentPackets);
    NS_LOG_UNCOND("Total Received Packets =" << ReceivedPackets);
    NS_LOG_UNCOND("Total Lost Packets =" << LostPackets);
    NS_LOG_UNCOND("Packet Loss ratio =" << ((LostPackets*100)/SentPackets)<< "%");
    NS_LOG_UNCOND("Packet delivery ratio =" << ((ReceivedPackets*100)/SentPackets)<< "%");
    NS_LOG_UNCOND("Average Throughput =" << AvgThroughput<< "Kbps");
    NS_LOG_UNCOND("End to End Delay =" << Delay);
    NS_LOG_UNCOND("End to End Jitter delay =" << Jitter);
    NS_LOG_UNCOND("Total Flod id " << j);
    monitor->SerializeToXmlFile("manet-routing.xml", true, true);
}

int
main()
{
    NodeContainer c;
    c.Create(6);

    NodeContainer n0n2 = NodeContainer(c.Get(0), c.Get(2));
    NodeContainer n1n2 = NodeContainer(c.Get(1), c.Get(2));
    NodeContainer n2n4 = NodeContainer(c.Get(2), c.Get(4));
    NodeContainer n4n3 = NodeContainer(c.Get(4), c.Get(3));
    NodeContainer n4n5 = NodeContainer(c.Get(4), c.Get(5));

    InternetStackHelper internet;
    internet.Install(c);

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));

    NetDeviceContainer d0d2 = csma.Install(n0n2);
    NetDeviceContainer d1d2 = csma.Install(n1n2);
    NetDeviceContainer d2d4 = csma.Install(n2n4);
    NetDeviceContainer d4d3 = csma.Install(n4n3);
    NetDeviceContainer d4d5 = csma.Install(n4n5);

    Ipv4AddressHelper ipv4;

    ipv4.SetBase("10.0.2.0", "255.255.255.0");
    Ipv4InterfaceContainer i0i2 = ipv4.Assign(d0d2);
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer i1i2 = ipv4.Assign(d1d2);
    ipv4.SetBase("10.2.4.0", "255.255.255.0");
    Ipv4InterfaceContainer i2i4 = ipv4.Assign(d2d4);
    ipv4.SetBase("10.4.3.0", "255.255.255.0");
    Ipv4InterfaceContainer i4i3 =  ipv4.Assign(d4d3);
    ipv4.SetBase("10.4.5.0", "255.255.255.0");
    Ipv4InterfaceContainer i4i5 = ipv4.Assign(d4d5);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // tcp on off
    OnOffHelper onoff("ns3::TcpSocketFactory", InetSocketAddress(i4i3.GetAddress(1), 3000));
    onoff.SetConstantRate(DataRate("2kbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(50));
    ApplicationContainer client1 = onoff.Install(c.Get(0));
    client1.Start(Seconds(2.0));
    client1.Stop(Seconds(10.0));

    PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(i4i3.GetAddress(1), 3000));
    ApplicationContainer server1 = sink.Install(c.Get(3));
    server1.Start(Seconds(1.0));
    server1.Stop(Seconds(11.0));

    MobilityHelper mobility;
    mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                        "MinX", DoubleValue (0.0),
                                        "MinY", DoubleValue (0.0),
                                        "DeltaX", DoubleValue (1.0),
                                        "DeltaY", DoubleValue (1.0),
                                        "GridWidth", UintegerValue (1),
                                        "LayoutType", StringValue ("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(c);
    AnimationInterface::SetConstantPosition(c.Get(0), 10, 10);
    AnimationInterface::SetConstantPosition(c.Get(1), 10, 50);
    AnimationInterface::SetConstantPosition(c.Get(2), 30, 30);
    AnimationInterface::SetConstantPosition(c.Get(3), 70, 10);
    AnimationInterface::SetConstantPosition(c.Get(4), 50, 30);
    AnimationInterface::SetConstantPosition(c.Get(5), 70, 50);

    AnimationInterface anim("ExperimentDumbbell.xml");

    csma.EnablePcap("ExperimentDumbbell", d2d4.Get(0), true);

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    NS_LOG_INFO("Run Simulation.");
    Simulator::Stop(Seconds(11.1));
    Simulator::Run();
    performance(flowmon, monitor);
    Simulator::Destroy();
    NS_LOG_INFO("End Simulation.");


}