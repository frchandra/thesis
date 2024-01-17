/**
* This example program isns3 designed to illustrate the behavior of
* rate-adaptive WiFi rate controls such as Minstrel.  Power-adaptive
* rate controls can be illustrated also, but separate examples exist for
* highlighting the power adaptation.
*
* This simulation consist of 2 nodes, one AP and one STA.
* The AP generates UDP traffic with a CBR of 54 Mbps to the STA.
* The AP can use any power and rate control mechanism and the STA uses
* only Minstrel rate control.
* The STA can be configured to move away from (or towards to) the AP.
* By default, the AP is at coordinate (0,0,0) and the STA starts at
* coordinate (5,0,0) (meters) and moves away on the x axis by 1 meter every
* second.
*
 */

#include <ctime>
#include "ns3/boolean.h"
#include "ns3/config.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/double.h"
#include "ns3/flow-monitor.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/gnuplot.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ssid.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/quic-module.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("FairnessDistancingOnoff");

/** Node statistics */
class NodeStatistics
{
  public:
    std::string nodeName;
    int stepItr = 0;
    NodeStatistics(NodeContainer nodes, std::string nodeName);
    void RxCallback(std::string path, Ptr<const Packet> packet, const Address& from);
    void SetPosition(Ptr<Node> node, Vector position);
    void AdvancePosition(Ptr<Node> node, int stepsSize, int stepsTime);
    Vector GetPosition(Ptr<Node> node);
    Gnuplot2dDataset GetDatafile();
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor;
    void Performance();

  private:
    uint32_t m_bytesTotal;     //!< total bytes
    Gnuplot2dDataset m_output; //!< gnuplot 2d dataset
};

NodeStatistics::NodeStatistics(NodeContainer nodes, std::string nodeName): flowmon(){
    m_bytesTotal = 0;
    monitor = flowmon.Install(nodes);
    this->nodeName = nodeName;
}

void NodeStatistics::RxCallback(std::string path, Ptr<const Packet> packet, const Address& from){
    m_bytesTotal += packet->GetSize();
//    NS_LOG_INFO("= PACKET RECIEVED; SIZE: " << m_bytesTotal << "; FROM: " << InetSocketAddress::ConvertFrom(from).GetIpv4() << " ==");
}

void NodeStatistics::SetPosition(Ptr<Node> node, Vector position){
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    mobility->SetPosition(position);
}

Vector NodeStatistics::GetPosition(Ptr<Node> node){
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    return mobility->GetPosition();
}

void NodeStatistics::AdvancePosition(Ptr<Node> node, int stepsSize, int stepsTime){
    NS_LOG_INFO("### ADVANCING: STEP " << stepItr << "; STA NODE_NAME: " << this->nodeName << " ###");
    stepItr++;
    Vector pos = GetPosition(node);
    double mbs = ((m_bytesTotal * 8.0) / (1000000 * stepsTime));
    NS_LOG_INFO(" Mbps: " << mbs);
    m_bytesTotal = 0;
    m_output.Add(pos.x, mbs);
    pos.x += stepsSize;
    SetPosition(node, pos);
    NS_LOG_INFO(" NEW POSITION (" << std::to_string(pos.x) << ", " << std::to_string(pos.y) << ")");
    Performance();
    Simulator::Schedule(Seconds(stepsTime),
                        &NodeStatistics::AdvancePosition,
                        this,
                        node,
                        stepsSize,
                        stepsTime);
}

Gnuplot2dDataset NodeStatistics::GetDatafile(){
    return m_output;
}

void NodeStatistics::Performance(){
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

int main()
{
    LogComponentEnable("FairnessDistancingOnoff", LOG_LEVEL_INFO);

    std::time_t unixNow = std::time(0);
    std::string transport_prot = "ns3::TcpNewReno";
    int nQuic = 1;
    int nTcp = 0;
    int steps = 3;
    int stepsSize = 1; //1m
    int stepsTime = 1; //1s
    std::string outputFileName = "run-" + std::to_string(unixNow) + "-" + transport_prot + "-" + std::to_string(nTcp) + "-" + std::to_string(nQuic) ;
    int simuTime = steps * stepsTime + stepsTime;
    uint16_t port = 443;
    std::string p2pDataRate = "100Mbps";
    std::string p2pDelay = "2ms";
    std::string onOffDataRate = "100Mb/s";
    int onOffPktSize = 1420;

    // User may find it convenient to enable logging
//    Time::SetResolution (Time::NS);
//    LogComponentEnableAll (LOG_PREFIX_TIME);
//    LogComponentEnableAll (LOG_PREFIX_FUNC);
//    LogComponentEnableAll (LOG_PREFIX_NODE);
    // LogComponentEnable("QuicVariantsComparison", LOG_LEVEL_ALL);
    // LogComponentEnable("BulkSendApplication", LOG_LEVEL_INFO);
    // LogComponentEnable("PfifoFastQueueDisc", LOG_LEVEL_ALL);
    // LogComponentEnable ("QuicSocketBase", LOG_LEVEL_ALL);
//     LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
//     LogComponentEnable("QuicL5Protocol", LOG_LEVEL_ALL);

    NodeContainer wifiTcpStaNodes;  wifiTcpStaNodes.Create( nTcp);
    NodeContainer wifiQuicStaNodes; wifiQuicStaNodes.Create( nQuic);
    NodeContainer wifiStaNodes;
    for (int i = 0; i < nTcp; i++){
        wifiStaNodes.Add(wifiTcpStaNodes.Get(i));
    }
    for (int i = 0; i < nQuic; i++){
        wifiStaNodes.Add(wifiQuicStaNodes.Get(i));
    }
    NodeContainer wifiApNodes;      wifiApNodes.Create(1);
    NodeContainer gwServerNode;     gwServerNode.Create(1);
    NodeContainer tcpServerNode;    tcpServerNode.Create(1);
    NodeContainer quicServerNode;   quicServerNode.Create(1);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0, 10, 0.0));  //AP
    for(int i = 0; i < nTcp + nQuic; i++){
        positionAlloc->Add(Vector(1, 10, 0.0)); //STA
    }
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNodes);
    for(int i = 0; i < nTcp + nQuic; i++){
        mobility.Install(wifiStaNodes.Get(i));
    }

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    YansWifiPhyHelper wifiPhy;
    YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
    wifiPhy.SetChannel(wifiChannel.Create());
//    wifiPhy.Set("ChannelSettings", StringValue("{0, 0, BAND_5GHZ, 0}"));
    wifiPhy.Set("ChannelSettings", StringValue("{0, 0, BAND_2_4GHZ, 0}"));
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");

    WifiMacHelper wifiMac;
    Ssid ssid = Ssid("AP");
    wifiMac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));//STA
    NetDeviceContainer wifiStaDevices;
    for(int i = 0; i < nTcp + nQuic; i++){
        wifiStaDevices.Add(wifi.Install(wifiPhy, wifiMac, wifiStaNodes.Get(i)));
    }
    wifiMac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));//AP
    NetDeviceContainer wifiApDevices = wifi.Install(wifiPhy, wifiMac, wifiApNodes);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue(p2pDataRate));
    pointToPoint.SetChannelAttribute("Delay", StringValue(p2pDelay));
    NetDeviceContainer apToGw = pointToPoint.Install(wifiApNodes.Get(0), gwServerNode.Get(0));
    NetDeviceContainer gwToTcp = pointToPoint.Install(gwServerNode.Get(0), tcpServerNode.Get(0));
    NetDeviceContainer gwToQuic = pointToPoint.Install(gwServerNode.Get(0), quicServerNode.Get(0));

//    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
//    em->SetAttribute("ErrorRate", DoubleValue(0.0005));
//    apToGw.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
//    Config::SetDefault("ns3::RateErrorModel::ErrorRate", DoubleValue(0.0005));
//    Config::SetDefault("ns3::RateErrorModel::ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
//    Config::SetDefault("ns3::BurstErrorModel::ErrorRate", DoubleValue(0.0005));
//    Config::SetDefault("ns3::BurstErrorModel::BurstSize",StringValue("ns3::UniformRandomVariable[Min=1|Max=3]"));

    InternetStackHelper stack;
    stack.Install(wifiTcpStaNodes);
    stack.Install(wifiApNodes);
    stack.Install(gwServerNode);
    stack.Install(tcpServerNode);
    QuicHelper quic;
    quic.InstallQuic(wifiQuicStaNodes);
    quic.InstallQuic(quicServerNode);

    Ipv4AddressHelper address;
    address.SetBase("10.0.1.0", "255.255.255.0"); //STA & AP
    Ipv4InterfaceContainer staIf = address.Assign(wifiStaDevices);
    Ipv4InterfaceContainer apIf = address.Assign(wifiApDevices);
    address.SetBase("10.1.2.0", "255.255.255.0"); //AP to GW
    Ipv4InterfaceContainer apGwIf = address.Assign(apToGw);
    address.SetBase("10.2.3.0", "255.255.255.0"); //GW to TCP
    Ipv4InterfaceContainer gwTcpIf = address.Assign(gwToTcp);
    address.SetBase("10.2.4.0", "255.255.255.0"); //GW to TCP
    Ipv4InterfaceContainer gwQuicIf = address.Assign(gwToQuic);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",TypeIdValue(TypeId::LookupByName(transport_prot)));;
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(1));
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",TypeIdValue(TypeId::LookupByName("ns3::TcpClassicRecovery")));
    Config::SetDefault("ns3::QuicL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName (transport_prot)));
//    Config::SetDefault ("ns3::QuicL4Protocol::SocketType", TypeIdValue(QuicCongestionOps::GetTypeId()));


    Config::SetDefault ("ns3::QuicSocketBase::SocketRcvBufSize", UintegerValue (1 << 21));
    Config::SetDefault ("ns3::QuicSocketBase::SocketSndBufSize", UintegerValue (1 << 21));
    Config::SetDefault ("ns3::QuicStreamBase::StreamSndBufSize", UintegerValue (1 << 21));
    Config::SetDefault ("ns3::QuicStreamBase::StreamRcvBufSize", UintegerValue (1 << 21));

    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 21));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 21));
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));

    //TCP onoff client
    OnOffHelper onoff("ns3::TcpSocketFactory", InetSocketAddress(gwTcpIf.GetAddress(1)/*server address*/, port));
    onoff.SetConstantRate(DataRate(onOffDataRate), onOffPktSize);
    ApplicationContainer tcpClient;
    for(int i = 0; i < nTcp; i++){
        tcpClient.Add(onoff.Install(wifiTcpStaNodes.Get(i)));
    }
    //TCP onoff server
    PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(gwTcpIf.GetAddress(1)/*server address*/, port));
    ApplicationContainer tcpServer = sink.Install(tcpServerNode.Get(0));

    //QUIC onoff server
    OnOffHelper quicOnoff("ns3::QuicSocketFactory", InetSocketAddress(gwQuicIf.GetAddress(1)/*server address*/, port));
    quicOnoff.SetConstantRate(DataRate(onOffDataRate), onOffPktSize);
    ApplicationContainer quicClient;
    for(int i = 0; i < nQuic; i++){
        quicClient.Add(quicOnoff.Install(wifiQuicStaNodes.Get(i)));
    }
    //QUIC onoff server
    PacketSinkHelper quicSink("ns3::QuicSocketFactory", InetSocketAddress(gwQuicIf.GetAddress(1)/*server address*/, port));
    ApplicationContainer quicServer = quicSink.Install(quicServerNode.Get(0));

    if(nTcp > 0){
        tcpServer.Start(Seconds(0.5));
        tcpClient.Start(Seconds(0.5));
        tcpServer.Stop(Seconds(simuTime));
        tcpClient.Stop(Seconds(simuTime));
    }
    if(nQuic > 0){
        quicServer.Start(Seconds(0.5));
        quicClient.Start(Seconds(0.5));
        quicServer.Stop(Seconds(simuTime));
        quicClient.Stop(Seconds(simuTime));
    }

    std::vector<NodeStatistics*> nodesStats;
//    nodesStats.reserve(nTcp + nQuic);
    for(int i = 0; i < nTcp; i++){
        NodeStatistics* nodeStat = new NodeStatistics(NodeContainer(wifiTcpStaNodes.Get(i), tcpServerNode.Get(0)), "aa");
        nodesStats.push_back(nodeStat);
        Simulator::Schedule(Seconds(0.5 + stepsTime),
                            &NodeStatistics::AdvancePosition,
                            nodesStats.at(i),
                            wifiTcpStaNodes.Get(i),
                            stepsSize,
                            stepsTime);
        Config::Connect("/NodeList/*/ApplicationList/*/$ns3::PacketSink/Rx",MakeCallback(&NodeStatistics::RxCallback, nodesStats.at(i)));
    }
    for(int i = 0; i <  nQuic; i++){
        NodeStatistics* nodeStat = new NodeStatistics(NodeContainer(wifiQuicStaNodes.Get(i), quicServerNode.Get(0)), "bb");
        nodesStats.push_back(nodeStat);
        Simulator::Schedule(Seconds(0.5 + stepsTime),
                            &NodeStatistics::AdvancePosition,
                            nodesStats.at(i),
                            wifiQuicStaNodes.Get(i),
                            stepsSize,
                            stepsTime);
        Config::Connect("/NodeList/*/ApplicationList/*/$ns3::PacketSink/Rx",MakeCallback(&NodeStatistics::RxCallback, nodesStats.at(i)));
    }

    pointToPoint.EnablePcap("pcap", apToGw);

    Simulator::Stop(Seconds(simuTime));
    Simulator::Run();

//    for(int i = 0; i < nTcp + nQuic; i++){
//        std::ofstream outfile("node" + std::to_string(i) + "-" + outputFileName + ".plt");
//        Gnuplot gnuplot = Gnuplot("node" + std::to_string(i) + "-"  + outputFileName + ".eps", "Throughput Node " + std::to_string(i));
//        gnuplot.SetTerminal("post eps color enhanced");
//        gnuplot.SetLegend("Distance (meter)", "Throughput (Mb/s)");
//        gnuplot.SetTitle("Throughput (AP to STA) vs distance");
//        gnuplot.AddDataset(nodesStats.at(i)->GetDatafile());
//        gnuplot.GenerateOutput(outfile);
//    }

    Simulator::Destroy();

    return 0;
}
