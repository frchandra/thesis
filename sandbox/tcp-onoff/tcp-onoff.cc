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
main (){
    NodeContainer nodes;
    nodes.Create (2);


    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
    pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install (nodes);

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase ("10.1.1.0", "255.255.255.0");

    Ipv4InterfaceContainer interfaces = address.Assign (devices);

    OnOffHelper onoff("ns3::TcpSocketFactory", InetSocketAddress(interfaces.GetAddress(1), 10000));
    onoff.SetConstantRate(DataRate("2kbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(50));
    ApplicationContainer client1 = onoff.Install(nodes.Get(0));

    PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(interfaces.GetAddress(1), 10000));
    ApplicationContainer server1 = sink.Install(nodes.Get(1));


    server1.Start (Seconds (0.0));
    server1.Stop (Seconds (9));
    client1.Start (Seconds (1));
    client1.Stop (Seconds (9));

    pointToPoint.EnablePcapAll("quic-onoff", true);
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop (Seconds (10));
    Simulator::Run ();
    performance(flowmon, monitor);
    Simulator::Destroy ();
}