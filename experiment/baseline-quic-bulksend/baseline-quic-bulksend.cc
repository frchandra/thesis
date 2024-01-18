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
#include <filesystem>
#include <fstream>
#include <iostream>

#include "ns3/applications-module.h"
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

NS_LOG_COMPONENT_DEFINE("BaselineQuicBulksend");

/** Node statistics */
class NodeStatistics
{
  public:
    std::string flowName;
    int stepItr = 0;
    NodeStatistics(NodeContainer nodes, std::string flowName);
    void SetPosition(Ptr<Node> node, Vector position);
    void AdvancePosition(Ptr<Node> node, int stepsSize, int stepsTime);
    Vector GetPosition(Ptr<Node> node);
    Gnuplot2dDataset GetDatafile();
    FlowMonitorHelper fh;
    Ptr<FlowMonitor> monitor;
    void Metrics(int distance);
    Ptr<OutputStreamWrapper> serverMetrics;
    Ptr<OutputStreamWrapper> clientMetrics;
    AsciiTraceHelper asciiHelper;
    NodeContainer nodes;
};

NodeStatistics::NodeStatistics(NodeContainer nodes, std::string flowName): fh(), asciiHelper(){
    this->nodes = nodes;
    monitor = fh.Install(this->nodes);

    this->flowName = flowName;
    std::ostringstream client; client << flowName << "-client.csv";
    clientMetrics = asciiHelper.CreateFileStream(client.str().c_str());
    *clientMetrics->GetStream() << "dist,kbps,jitter_mics,plr,pdr,delay_mics,pkt_sent,pkt_rcv,pkt_loss,source,dest" << std::endl;
    std::ostringstream server; server << flowName << "-server.csv";
    serverMetrics = asciiHelper.CreateFileStream(server.str().c_str());
    *serverMetrics->GetStream() << "dist,kbps,jitter_mics,plr,pdr,delay_mics,pkt_sent,pkt_rcv,pkt_loss,source,dest" << std::endl;
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
    NS_LOG_INFO("### ADVANCING: STEP " << stepItr << "; STA NODE_NAME: " << this->flowName << " ###");
    stepItr++;
    Vector pos = GetPosition(node);
    Metrics(pos.x);
    pos.x += stepsSize;
    SetPosition(node, pos);
    Simulator::Schedule(Seconds(stepsTime),
                        &NodeStatistics::AdvancePosition,
                        this,
                        node,
                        stepsSize,
                        stepsTime);
}

void NodeStatistics::Metrics(int distance){
    int i = 0;
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (fh.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin(); iter != stats.end(); ++iter){
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter->first);
        //        if(i==0)NS_LOG_UNCOND(" Flow :" << this->flowName << "-client");else NS_LOG_UNCOND(" Flow :" << this->flowName << "-server");
        NS_LOG_UNCOND(" dist\tkbps\tjitter_mics\t\tplr\tpdr\tdelay_mics\t\tpkt_sent\t\tpkt_rcv\t\tpkt_loss\t\tsource\t\tdest");
        NS_LOG_UNCOND(" " << distance << "\t\t"
                          << iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds() - iter->second.timeFirstTxPacket.GetSeconds())/1024 << "\t"
                          << iter->second.jitterSum.GetMicroSeconds() << "\t\t\t" //jiter_mics
                          << std::abs((int)iter->second.txPackets - (int)iter->second.rxPackets)*100/iter->second.txPackets << "\t" //packet loss ratio
                          << iter->second.rxPackets*100/iter->second.txPackets << "\t" //packet delivery ratio
                          << iter->second.delaySum.GetMicroSeconds() << "\t\t" //delay_mics
                          << iter->second.txPackets << "\t\t\t" //packet sent
                          << iter->second.rxPackets << "\t\t" //packet receive
                          << std::abs((int)iter->second.txPackets - (int)iter->second.rxPackets) << "\t\t\t" // packet loss
                          << t.sourceAddress  << "\t\t" //source
                          << t.destinationAddress ); //dest
        if(i==0){
            *clientMetrics->GetStream() << distance << ","
                                        << iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds())/1024 << ","
                                        << iter->second.jitterSum << ","
                                        << (iter->second.txPackets-iter->second.rxPackets)*100/iter->second.txPackets << ","
                                        << iter->second.rxPackets*100/iter->second.txPackets << ","
                                        << iter->second.delaySum << ","
                                        << iter->second.txPackets << ","
                                        << iter->second.rxPackets << ","
                                        << iter->second.txPackets-iter->second.rxPackets << ","
                                        << t.sourceAddress  << ","
                                        << t.destinationAddress << std::endl;
        }
        else{
            *serverMetrics->GetStream() << distance << ","
                                        << iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds())/1024 << ","
                                        << iter->second.jitterSum << ","
                                        << (iter->second.txPackets-iter->second.rxPackets)*100/iter->second.txPackets << ","
                                        << iter->second.rxPackets*100/iter->second.txPackets << ","
                                        << iter->second.delaySum << ","
                                        << iter->second.txPackets << ","
                                        << iter->second.rxPackets << ","
                                        << iter->second.txPackets-iter->second.rxPackets << ","
                                        << t.sourceAddress  << ","
                                        << t.destinationAddress << std::endl;
        }
        i++;
    }
    this->monitor->ResetAllStats();
}

int main(){
    LogComponentEnable("BaselineQuicBulksend", LOG_LEVEL_INFO);

    std::string transport_prot = "ns3::TcpNewReno";
    int nQuic = 1;
    int nTcp = 0;
    int steps = 60;
    int stepsSize = 1; //1m
    int stepsTime = 1; //1s
    int simuTime = steps * stepsTime + stepsTime;
    uint16_t port = 443;
    std::string p2pApGwDataRate = "100Mbps";
    std::string p2pApGwDelay = "2ms";
    std::string p2pGwServerDataRate = "100Mbps";
    std::string p2pGwServerDelay = "2ms";
    int bsMaxByte = 0;
    //    std::string onOffDataRate = "100Mb/s";
    //    int onOffPktSize = 1420;
    double errorRate = 0.0000;

    std::time_t unixNow = std::time(0);
    struct tm *localTime = localtime(&unixNow);
    std::stringstream formattedTime; formattedTime << std::put_time(localTime, "%Y-%m-%d_%H:%M:%S");
    std::string folderName = formattedTime.str();
    std::filesystem::path directoryPath = "./"+ folderName +"/";
    std::filesystem::create_directory(directoryPath);

    // User may find it convenient to enable logging
    //    Time::SetResolution (Time::NS);
    //    LogComponentEnableAll (LOG_PREFIX_TIME);
    //    LogComponentEnableAll (LOG_PREFIX_FUNC);
    //    LogComponentEnableAll (LOG_PREFIX_NODE);
    //    LogComponentEnable("QuicVariantsComparison", LOG_LEVEL_ALL);
    //    LogComponentEnable("BulkSendApplication", LOG_LEVEL_INFO);
    //    LogComponentEnable("PfifoFastQueueDisc", LOG_LEVEL_ALL);
    //    LogComponentEnable ("QuicSocketBase", LOG_LEVEL_ALL);
    //    LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
    //    LogComponentEnable("QuicL5Protocol", LOG_LEVEL_ALL);

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

    PointToPointHelper p2pApGw;
    p2pApGw.SetDeviceAttribute("DataRate", StringValue(p2pApGwDataRate));
    p2pApGw.SetChannelAttribute("Delay", StringValue(p2pApGwDelay));
    NetDeviceContainer apToGw = p2pApGw.Install(wifiApNodes.Get(0), gwServerNode.Get(0));
    PointToPointHelper p2pGwServer;
    p2pGwServer.SetDeviceAttribute("DataRate", StringValue(p2pGwServerDataRate));
    p2pGwServer.SetChannelAttribute("Delay", StringValue(p2pGwServerDelay));
    NetDeviceContainer gwToTcp = p2pGwServer.Install(gwServerNode.Get(0), tcpServerNode.Get(0));
    NetDeviceContainer gwToQuic = p2pGwServer.Install(gwServerNode.Get(0), quicServerNode.Get(0));

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(errorRate));
    apToGw.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    Config::SetDefault("ns3::RateErrorModel::ErrorRate", DoubleValue(errorRate));
    Config::SetDefault("ns3::RateErrorModel::ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
    Config::SetDefault("ns3::BurstErrorModel::ErrorRate", DoubleValue(errorRate));
    Config::SetDefault("ns3::BurstErrorModel::BurstSize",StringValue("ns3::UniformRandomVariable[Min=1|Max=3]"));

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
    BulkSendHelper bs("ns3::TcpSocketFactory", InetSocketAddress(gwTcpIf.GetAddress(1)/*server address*/, port));
    bs.SetAttribute("MaxBytes", UintegerValue(bsMaxByte));
    ApplicationContainer tcpClient;
    for(int i = 0; i < nTcp; i++){
        tcpClient.Add(bs.Install(wifiTcpStaNodes.Get(i)));
    }
    //TCP onoff server
    PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(gwTcpIf.GetAddress(1)/*server address*/, port));
    ApplicationContainer tcpServer = sink.Install(tcpServerNode.Get(0));

    //QUIC onoff server
    BulkSendHelper quicBs("ns3::QuicSocketFactory", InetSocketAddress(gwQuicIf.GetAddress(1)/*server address*/, port));
    quicBs.SetAttribute("MaxBytes", UintegerValue(bsMaxByte));
    ApplicationContainer quicClient;
    for(int i = 0; i < nQuic; i++){
        quicClient.Add(quicBs.Install(wifiQuicStaNodes.Get(i)));
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

    std::vector<NodeStatistics*> tcpStats;
    for(int i = 0; i < nTcp; i++){
        NodeStatistics* nodeStat = new NodeStatistics(
            NodeContainer(wifiTcpStaNodes.Get(i), tcpServerNode.Get(0)),
            "./" + folderName +  "/" + "tcp-flow"+std::to_string(i));
        tcpStats.push_back(nodeStat);
        Simulator::Schedule(Seconds(0.5 + stepsTime),
                            &NodeStatistics::AdvancePosition,
                            tcpStats.at(i),
                            wifiTcpStaNodes.Get(i),
                            stepsSize,
                            stepsTime);
    }
    std::vector<NodeStatistics*> quicStats;
    for(int i = 0; i < nQuic; i++){
        NodeStatistics* nodeStat = new NodeStatistics(
            NodeContainer(wifiQuicStaNodes.Get(i), quicServerNode.Get(0)),
            "./" + folderName +  "/" + "quic-flow"+std::to_string(i));
        quicStats.push_back(nodeStat);
        Simulator::Schedule(Seconds(0.5 + stepsTime),
                            &NodeStatistics::AdvancePosition,
                            quicStats.at(i),
                            wifiQuicStaNodes.Get(i),
                            stepsSize,
                            stepsTime);
    }

    p2pApGw.EnablePcap( "./" + folderName + "/" + "apToGw", apToGw);

    Simulator::Stop(Seconds(simuTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}

