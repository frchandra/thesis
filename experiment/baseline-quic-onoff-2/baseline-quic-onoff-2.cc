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

NS_LOG_COMPONENT_DEFINE("BaselineQuicOnoff2");

/** Node statistics */
class NodeStatistics
{
  public:
    std::string flowName;
    int stepItr = 0;
    FlowMonitorHelper fh;
    Ptr<FlowMonitor> monitor;
    AsciiTraceHelper asciiHelper;
    NodeContainer nodes;
    Ptr<OutputStreamWrapper> serverMetrics;
    Ptr<OutputStreamWrapper> clientMetrics;
    ns3::SignalNoiseDbm signalNoise;

    NodeStatistics(NodeContainer nodes, std::string flowName);
    void SetPosition(Ptr<Node> node, Vector position);
    void AdvancePosition(Ptr<Node> node, int stepsSize, int stepsTime);
    Vector GetPosition(Ptr<Node> node);
    void Metrics(int distance);
    void MonitorSnifferRxCallback(std::string context, Ptr< const Packet > packet, uint16_t channelFreqMhz, WifiTxVector txVector, MpduInfo aMpdu, SignalNoiseDbm signalNoise, uint16_t staId);
};

NodeStatistics::NodeStatistics(NodeContainer nodes, std::string flowName): fh(), asciiHelper(){
    this->nodes = nodes;
    monitor = fh.Install(this->nodes);
    //    monitor.

    this->flowName = flowName;
    std::ostringstream client; client << flowName << "-client.csv";
    clientMetrics = asciiHelper.CreateFileStream(client.str().c_str());
    *clientMetrics->GetStream() << "dist,kbps,jitter_mils,plr,pdr,delay_mils,pkt_sent,pkt_rcv,pkt_loss,signal,noise,source,dest" << std::endl;
    std::ostringstream server; server << flowName << "-server.csv";
    serverMetrics = asciiHelper.CreateFileStream(server.str().c_str());
    *serverMetrics->GetStream() << "dist,kbps,jitter_mils,plr,pdr,delay_mils,pkt_sent,pkt_rcv,pkt_loss,signal,noise,source,dest" << std::endl;
}

void NodeStatistics::SetPosition(Ptr<Node> node, Vector position){
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    mobility->SetPosition(position);
}

Vector NodeStatistics::GetPosition(Ptr<Node> node){
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    return mobility->GetPosition();
}

void NodeStatistics::MonitorSnifferRxCallback(std::string context, Ptr<const ns3::Packet> packet, uint16_t channelFreqMhz, ns3::WifiTxVector txVector, ns3::MpduInfo aMpdu, ns3::SignalNoiseDbm sigNoi, uint16_t staId){
    this->signalNoise = sigNoi;
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
        NS_LOG_UNCOND(" dist\tkbps\tjitter_mils\t\tplr\tpdr\tdelay_mils\t\tpkt_sent\t\tpkt_rcv\t\tpkt_loss\t\tsignal\t\tnoise\t\tsource\t\tdest");
        NS_LOG_UNCOND(" " << distance << "\t\t"
                          << iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds() - iter->second.timeFirstTxPacket.GetSeconds())/1024 << "\t"
                          << iter->second.jitterSum.GetMilliSeconds() << "\t\t\t\t" //jitter_mils
                          << std::abs((int)iter->second.txPackets - (int)iter->second.rxPackets)*100/(iter->second.txPackets + 0.01) << "\t" //packet loss ratio
                          << iter->second.rxPackets*100/(iter->second.txPackets + 0.01) << "\t" //packet delivery ratio
                          << iter->second.lastDelay.GetMilliSeconds() << "\t\t\t\t" //delay_mils
                          << iter->second.txPackets << "\t\t\t" //packet sent
                          << iter->second.rxPackets << "\t\t" //packet receive
                          << std::abs((int)iter->second.txPackets - (int)iter->second.rxPackets) << "\t\t" // packet loss
                          << this->signalNoise.signal << "\t\t"
                          << this->signalNoise.noise << "\t\t"
                          << t.sourceAddress  << "\t\t" //source
                          << t.destinationAddress ); //dest
        if(i==0){
            *clientMetrics->GetStream() << distance << ","
                                        << iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds())/1024 << ","
                                        << iter->second.jitterSum.GetMilliSeconds() << ","
                                        << std::abs((int)iter->second.txPackets - (int)iter->second.rxPackets)*100/(iter->second.txPackets + 0.01) << ","
                                        << iter->second.rxPackets*100/(iter->second.txPackets + 0.01) << ","
                                        << iter->second.lastDelay.GetMilliSeconds() << ","
                                        << iter->second.txPackets << ","
                                        << iter->second.rxPackets << ","
                                        << std::abs((int)iter->second.txPackets - (int)iter->second.rxPackets) << ","
                                        << this->signalNoise.signal << ","
                                        << this->signalNoise.noise << ","
                                        << t.sourceAddress  << ","
                                        << t.destinationAddress << std::endl;
        }
        else{
            *serverMetrics->GetStream() << distance << ","
                                        << iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds())/1024 << ","
                                        << iter->second.jitterSum.GetMilliSeconds() << ","
                                        << std::abs((int)iter->second.txPackets - (int)iter->second.rxPackets)*100/(iter->second.txPackets + 0.01) << ","
                                        << iter->second.rxPackets*100/(iter->second.txPackets + 0.01) << ","
                                        << iter->second.lastDelay.GetMilliSeconds() << ","
                                        << iter->second.txPackets << ","
                                        << iter->second.rxPackets << ","
                                        << std::abs((int)iter->second.txPackets - (int)iter->second.rxPackets) << ","
                                        << this->signalNoise.signal << ","
                                        << this->signalNoise.noise << ","
                                        << t.sourceAddress  << ","
                                        << t.destinationAddress << std::endl;
        }
        i++;
    }
    this->monitor->ResetAllStats();
}

int main(){
    LogComponentEnable("BaselineQuicOnoff2", LOG_LEVEL_INFO);

    std::string transport_prot = "ns3::TcpNewReno";
    int nQuic = 1;
    int nTcp = 0;
    int nUdp = 0;
    int steps = 60;
    int stepsSize = 1; //1m
    int stepsTime = 1; //1s
    int simuTime = steps * stepsTime + stepsTime;
    uint16_t port = 443;
    std::string p2pApGwDataRate = "1Gbps";
    std::string p2pApGwDelay = "2ms";
    std::string p2pGwServerDataRate = "1Gbps";
    std::string p2pGwServerDelay = "2ms";
    std::string onOffDataRate = "100Mb/s";
    std::string ofOnTime = "1.0";
    std::string ofOffTime = "1.0";
    int onOffPktSize = 1420;
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
    NodeContainer wifiUdpStaNodes; wifiUdpStaNodes.Create( nUdp);
    NodeContainer wifiStaNodes;
    for (int i = 0; i < nTcp; i++){
        wifiStaNodes.Add(wifiTcpStaNodes.Get(i));
    }
    for (int i = 0; i < nQuic; i++){
        wifiStaNodes.Add(wifiQuicStaNodes.Get(i));
    }
    for (int i = 0; i < nUdp; i++){
        wifiStaNodes.Add(wifiUdpStaNodes.Get(i));
    }
    NodeContainer wifiApNodes;      wifiApNodes.Create(1);
    NodeContainer gwServerNode;     gwServerNode.Create(1);
    NodeContainer tcpServerNode;    tcpServerNode.Create(1);
    NodeContainer quicServerNode;   quicServerNode.Create(1);
    NodeContainer udpServerNode;   udpServerNode.Create(1);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0, 10, 0.0));  //AP
    for(int i = 0; i < nTcp + nQuic + nUdp; i++){
        positionAlloc->Add(Vector(1, 10, 0.0)); //STA
    }
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNodes);
    for(int i = 0; i < nTcp + nQuic + nUdp ; i++){
        mobility.Install(wifiStaNodes.Get(i));
    }

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    YansWifiPhyHelper wifiPhy;
    YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
    wifiPhy.SetChannel(wifiChannel.Create());
    wifiPhy.Set("ChannelSettings", StringValue("{0, 0, BAND_2_4GHZ, 0}"));
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");

    WifiMacHelper wifiMac;
    Ssid ssid = Ssid("AP");
    wifiMac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));//STA
    NetDeviceContainer wifiStaDevices;
    for(int i = 0; i < nTcp + nQuic + nUdp; i++){
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
    NetDeviceContainer gwToUdp = p2pGwServer.Install(gwServerNode.Get(0), udpServerNode.Get(0));

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(errorRate));
    apToGw.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    Config::SetDefault("ns3::RateErrorModel::ErrorRate", DoubleValue(errorRate));
    Config::SetDefault("ns3::RateErrorModel::ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
    Config::SetDefault("ns3::BurstErrorModel::ErrorRate", DoubleValue(errorRate));
    Config::SetDefault("ns3::BurstErrorModel::BurstSize",StringValue("ns3::UniformRandomVariable[Min=1|Max=3]"));

    InternetStackHelper stack;
    stack.Install(wifiTcpStaNodes);
    stack.Install(wifiUdpStaNodes);
    stack.Install(wifiApNodes);
    stack.Install(gwServerNode);
    stack.Install(tcpServerNode);
    stack.Install(udpServerNode);
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
    address.SetBase("10.2.4.0", "255.255.255.0"); //GW to QUIC
    Ipv4InterfaceContainer gwQuicIf = address.Assign(gwToQuic);
    address.SetBase("10.2.5.0", "255.255.255.0"); //GW to UDP
    Ipv4InterfaceContainer gwUdpIf = address.Assign(gwToUdp);

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

    //TCP onoff client - server
    ApplicationContainer tcpServer;
    ApplicationContainer tcpClient;
    for(int i = 0; i < nTcp; i++){
        int nodeNum = i;
        OnOffHelper *onoff = new OnOffHelper("ns3::TcpSocketFactory", InetSocketAddress(staIf.GetAddress(nodeNum)/*client address*/, port));
        onoff->SetConstantRate(DataRate(onOffDataRate), onOffPktSize);
        tcpServer.Add(onoff->Install(tcpServerNode.Get(0)));

        PacketSinkHelper *sink = new  PacketSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(staIf.GetAddress(nodeNum)/*client address*/, port));
        tcpClient.Add(sink->Install(wifiStaNodes.Get(nodeNum)));
    }


    //QUIC onoff client - server
    OnOffHelper quicOnoff("ns3::QuicSocketFactory", InetSocketAddress(gwQuicIf.GetAddress(1)/*server address*/, port));
    quicOnoff.SetConstantRate(DataRate(onOffDataRate), onOffPktSize);
    ApplicationContainer quicClient;
    for(int i = 0; i < nQuic; i++){
        quicClient.Add(quicOnoff.Install(wifiQuicStaNodes.Get(i)));
    } //server
    PacketSinkHelper quicSink("ns3::QuicSocketFactory", InetSocketAddress(gwQuicIf.GetAddress(1)/*server address*/, port));
    ApplicationContainer quicServer = quicSink.Install(quicServerNode.Get(0));
//    ApplicationContainer quicServer;
//    ApplicationContainer quicClient;
//    for(int i = 0; i < nQuic; i++){
//        int nodeNum = i;
//        OnOffHelper *onoff = new OnOffHelper("ns3::QuicSocketFactory", InetSocketAddress(staIf.GetAddress(nodeNum)/*client address*/, port));
//        onoff->SetConstantRate(DataRate(onOffDataRate), onOffPktSize);
//        quicServer.Add(onoff->Install(quicServerNode.Get(0)));
//
//        PacketSinkHelper *sink = new  PacketSinkHelper("ns3::QuicSocketFactory", InetSocketAddress(staIf.GetAddress(nodeNum)/*client address*/, port));
//        quicClient.Add(sink->Install(wifiStaNodes.Get(nodeNum)));
//    }

    //UDP onoff client - server
    OnOffHelper udpOnoff("ns3::UdpSocketFactory", InetSocketAddress(gwUdpIf.GetAddress(1)/*server address*/, port));
    udpOnoff.SetConstantRate(DataRate(onOffDataRate), onOffPktSize);
    udpOnoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant="+ ofOnTime +"]"));
    udpOnoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant="+ ofOffTime +"]"));
    ApplicationContainer udpClient;
    for(int i = 0; i < nUdp; i++){
        udpClient.Add(udpOnoff.Install(wifiUdpStaNodes.Get(i)));
    } //server
    PacketSinkHelper udpSink("ns3::UdpSocketFactory", InetSocketAddress(gwUdpIf.GetAddress(1)/*server address*/, port));
    ApplicationContainer udpServer = udpSink.Install(udpServerNode.Get(0));

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
    if(nUdp > 0){
        udpServer.Start(Seconds(0.5));
        udpClient.Start(Seconds(0.5));
        udpServer.Stop(Seconds(simuTime));
        udpClient.Stop(Seconds(simuTime));
    }



    std::vector<NodeStatistics*> tcpStats;
    for(int i = 0; i < nTcp; i++){
        NodeStatistics* nodeStat = new NodeStatistics(
            NodeContainer(tcpServerNode.Get(0), wifiTcpStaNodes.Get(i)/*,wifiTcpStaNodes.Get(1)*/),
            "./" + folderName +  "/" + "tcp-flow"+std::to_string(i));
        tcpStats.push_back(nodeStat);
        Simulator::Schedule(Seconds(0.5 + stepsTime),
                            &NodeStatistics::AdvancePosition,
                            tcpStats.at(i),
                            wifiTcpStaNodes.Get(i),
                            stepsSize,
                            stepsTime);
        int nodeNum = i;
        Config::Connect("/NodeList/" + std::to_string(nodeNum) + "/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferRx",
                        MakeCallback(&NodeStatistics::MonitorSnifferRxCallback, nodeStat));
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
        int nodeNum = nTcp + i;
        Config::Connect("/NodeList/" + std::to_string(nodeNum) + "/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferRx",
                        MakeCallback(&NodeStatistics::MonitorSnifferRxCallback, nodeStat));
    }
    std::vector<NodeStatistics*> udpStats;
    for(int i = 0; i < nUdp; i++){
        NodeStatistics* nodeStat = new NodeStatistics(
            NodeContainer(wifiUdpStaNodes.Get(i), udpServerNode.Get(0)),
            "./" + folderName +  "/" + "udp-flow"+std::to_string(i));
        udpStats.push_back(nodeStat);
        Simulator::Schedule(Seconds(0.5 + stepsTime),
                            &NodeStatistics::AdvancePosition,
                            udpStats.at(i),
                            wifiUdpStaNodes.Get(i),
                            stepsSize,
                            stepsTime);
        int nodeNum = nTcp + nQuic + i;
        Config::Connect("/NodeList/" + std::to_string(nodeNum) + "/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferRx",
                        MakeCallback(&NodeStatistics::MonitorSnifferRxCallback, nodeStat));
    }

    p2pApGw.EnablePcap( "./" + folderName + "/" + "apToGw", apToGw);

    Simulator::Stop(Seconds(simuTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}

