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

NS_LOG_COMPONENT_DEFINE("BaseOnoff");

/** Node statistics */
class NodeStatistics{
  public:
    bool isDoubleStream;
    std::string flowName;
    int stepItr = 0;
    FlowMonitorHelper fh;
    Ptr<FlowMonitor> monitor;
    AsciiTraceHelper asciiHelper;
    NodeContainer nodes;
    Ptr<OutputStreamWrapper> serverMetrics;
    Ptr<OutputStreamWrapper> clientMetrics;
    ns3::SignalNoiseDbm signalNoise;

    NodeStatistics(NodeContainer nodes, std::string flowName, bool isDoubleStream);
    void SetPosition(Ptr<Node> node, Vector position);
    void AdvancePosition(Ptr<Node> node, int stepsSize, int stepsTime);
    Vector GetPosition(Ptr<Node> node);
    void Metrics();
    void MonitorSnifferRxCallback(std::string context, Ptr< const Packet > packet, uint16_t channelFreqMhz, WifiTxVector txVector, MpduInfo aMpdu, SignalNoiseDbm signalNoise, uint16_t staId);
};

NodeStatistics::NodeStatistics(NodeContainer nodes, std::string flowName, bool isDoubleStream): fh(), asciiHelper(){
    this->isDoubleStream = isDoubleStream;
    this->nodes = nodes;
    monitor = fh.Install(this->nodes);

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
    Metrics();
    pos.x += stepsSize;
    SetPosition(node, pos);
    Simulator::Schedule(Seconds(stepsTime),
                        &NodeStatistics::AdvancePosition,
                        this,
                        node,
                        stepsSize,
                        stepsTime);
}

void NodeStatistics::Metrics(){
    int i = 0;
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (fh.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin(); iter != stats.end(); ++iter){
        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(iter->first);
        NS_LOG_UNCOND(" " << stepItr << "|kbps:"
                          << iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds() + 0.01 - iter->second.timeFirstTxPacket.GetSeconds())/1024 << "|jitter_mils:"
                          << iter->second.jitterSum.GetMilliSeconds() << "|plr:" //jitter_mils
                          << std::abs((int)iter->second.txPackets - (int)iter->second.rxPackets)*100/(iter->second.txPackets + 0.01) << "|pdr:" //packet loss ratio
                          << iter->second.rxPackets*100/(iter->second.txPackets + 0.01) << "|delay_mils:" //packet delivery ratio
                          << iter->second.lastDelay.GetMilliSeconds() << "|pkt_sent:" //delay_mils
                          << iter->second.txPackets << "|pkt_rcv:" //packet sent
                          << iter->second.rxPackets << "|pkt_loss:" //packet receive
                          << std::abs((int)iter->second.txPackets - (int)iter->second.rxPackets) << "|signal:" // packet loss
                          << tuple.sourceAddress  << "|dest:" //source
                          << tuple.destinationAddress ); //dest

        if(i==0){
            *clientMetrics->GetStream() << stepItr << ","
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
                                        << tuple.sourceAddress  << ","
                                        << tuple.destinationAddress << std::endl;
            if(isDoubleStream == false)break;
        }
        else{
            *serverMetrics->GetStream() << stepItr << ","
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
                                        << tuple.sourceAddress  << ","
                                        << tuple.destinationAddress << std::endl;
            break;
        }
        i++;
    }
    this->monitor->ResetAllStats();
}

int main(){
    LogComponentEnable("BaseOnoff", LOG_LEVEL_INFO);

    std::string transport_prot = "ns3::TcpNewReno";
    int nQuic = 0;
    int nTcp = 1;
    int nUdp = 1;
    int steps = 20;
    int initPos = 5;
    int stepsSize = 0; //1m
    int stepsTime = 1; //1s
    double tcpStart = 0.5;
    double udpStart = 10.5;
    double quicStart = 0.5;
    int simuTime = steps * stepsTime + stepsTime;
    uint16_t port = 443;
    std::string propagationDelay = "ns3::ConstantSpeedPropagationDelayModel";
    std::string propagationLoss = "ns3::FriisPropagationLossModel";
    std::string p2pApGwDataRate = "1Gbps";
    std::string p2pApGwDelay = "2ms";
    std::string p2pGwServerDataRate = "1Gbps";
    std::string p2pGwServerDelay = "2ms";

    bool isUpstream = true;
    bool isDownstream = false;
    bool isDoubleStream = false;

    std::string onOffDownRate = "100Mb/s";
    std::string onOffUpRate = "100Mb/s";
    std::string ofOnTime = "1";
    std::string ofOffTime = "1";
    int onOffPktSize = 1420;

    double errorRate = 0.0000;

    std::time_t unixNow = std::time(0);
    struct tm *localTime = localtime(&unixNow);
    std::stringstream formattedTime; formattedTime << std::put_time(localTime, "%Y-%m-%d_%H:%M:%S");
    std::string folderName = formattedTime.str();
    std::filesystem::path directoryPath = "./"+ folderName +"/";
    std::filesystem::create_directory(directoryPath);

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
        positionAlloc->Add(Vector(initPos, 10, 0.0)); //STA
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
    //    wifiPhy.Set("ChannelSettings", StringValue("{0, 0, BAND_5GHZ, 0}"));
    wifiPhy.Set("ChannelSettings", StringValue("{0, 0, BAND_2_4GHZ, 0}"));
    wifiChannel.SetPropagationDelay(propagationDelay);
    wifiChannel.AddPropagationLoss(propagationLoss);

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
    Config::SetDefault("ns3::BurstErrorModel::BurstSize",StringValue("ns3::UniformRandomVariable[Min=1|Max=4]"));

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
    Config::SetDefault ("ns3::QuicL4Protocol::0RTT-Handshake", BooleanValue(true));
    Config::SetDefault ("ns3::QuicSocketBase::InitialVersion", UintegerValue (QUIC_VERSION_NS3_IMPL));

    Config::SetDefault ("ns3::QuicSocketBase::SocketRcvBufSize", UintegerValue (1 << 21));
    Config::SetDefault ("ns3::QuicSocketBase::SocketSndBufSize", UintegerValue (1 << 21));
    Config::SetDefault ("ns3::QuicStreamBase::StreamSndBufSize", UintegerValue (1 << 21));
    Config::SetDefault ("ns3::QuicStreamBase::StreamRcvBufSize", UintegerValue (1 << 21));

    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 21));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 21));
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));

    ApplicationContainer tcpClient;
    ApplicationContainer tcpServer;
    if(isUpstream == true){
        OnOffHelper tcpOnoffUp("ns3::TcpSocketFactory", InetSocketAddress(gwTcpIf.GetAddress(1)/*target: server address*/, port));
        tcpOnoffUp.SetConstantRate(DataRate(onOffUpRate), onOffPktSize);
        for(int i = 0; i < nTcp; i++){
            tcpClient.Add(tcpOnoffUp.Install(wifiTcpStaNodes.Get(i)));
        }
        PacketSinkHelper tcpSinkUp("ns3::TcpSocketFactory", InetSocketAddress(gwTcpIf.GetAddress(1)/*target: server address*/, port));
        tcpServer.Add(tcpSinkUp.Install(tcpServerNode.Get(0)));
    }
    if (isDownstream == true) {
        for(int i = 0; i < nTcp; i++){
            OnOffHelper tcpOnoffDown("ns3::TcpSocketFactory", InetSocketAddress(staIf.GetAddress(i)/*target: client address*/, port));
            tcpOnoffDown.SetConstantRate(DataRate(onOffDownRate), onOffPktSize);
            tcpServer.Add(tcpOnoffDown.Install(tcpServerNode.Get(0)));
            PacketSinkHelper tcpSinkDown("ns3::TcpSocketFactory", InetSocketAddress(staIf.GetAddress(i)/*target: client address*/, port));
            tcpClient = tcpSinkDown.Install(wifiTcpStaNodes.Get(i));
        }
    }


    ApplicationContainer quicClient;
    ApplicationContainer quicServer;
    if(isUpstream == true){
        OnOffHelper quicOnoffUp("ns3::QuicSocketFactory", InetSocketAddress(gwQuicIf.GetAddress(1)/*target: server address*/, port));
        quicOnoffUp.SetConstantRate(DataRate(onOffUpRate), onOffPktSize);
        for(int i = 0; i < nQuic; i++){
            quicClient.Add(quicOnoffUp.Install(wifiQuicStaNodes.Get(i)));
        }
        PacketSinkHelper quicSinkUp("ns3::QuicSocketFactory", InetSocketAddress(gwQuicIf.GetAddress(1)/*target: server address*/, port));
        quicServer.Add(quicSinkUp.Install(quicServerNode.Get(0)));
    }
    if (isDownstream == true) {
        for(int i = 0; i < nQuic; i++){
            OnOffHelper quicOnoffDown("ns3::QuicSocketFactory", InetSocketAddress(staIf.GetAddress(nTcp + i)/*target: client address*/, port));
            quicOnoffDown.SetConstantRate(DataRate(onOffDownRate), onOffPktSize);
            quicServer.Add(quicOnoffDown.Install(quicServerNode.Get(0)));
            PacketSinkHelper quicSinkDown("ns3::QuicSocketFactory", InetSocketAddress(staIf.GetAddress(nTcp + i)/*target: client address*/, port));
            quicClient = quicSinkDown.Install(wifiQuicStaNodes.Get(i));
        }
    }


    ApplicationContainer udpClient;
    ApplicationContainer udpServer;
    if(isUpstream == true){
        OnOffHelper udpOnoffUp("ns3::UdpSocketFactory", InetSocketAddress(gwUdpIf.GetAddress(1)/*target: server address*/, port));
        udpOnoffUp.SetConstantRate(DataRate(onOffUpRate), onOffPktSize);
        for(int i = 0; i < nUdp; i++){
            udpClient.Add(udpOnoffUp.Install(wifiUdpStaNodes.Get(i)));
        }
        PacketSinkHelper udpSinkUp("ns3::UdpSocketFactory", InetSocketAddress(gwUdpIf.GetAddress(1)/*target: server address*/, port));
        udpServer.Add(udpSinkUp.Install(udpServerNode.Get(0)));
        udpOnoffUp.SetConstantRate(DataRate(onOffUpRate), onOffPktSize);
        udpOnoffUp.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant="+ ofOnTime +"]"));
        udpOnoffUp.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant="+ ofOffTime +"]"));
    }
    if (isDownstream == true) {
        for(int i = 0; i < nUdp; i++){
            OnOffHelper udpOnoffDown("ns3::UdpSocketFactory", InetSocketAddress(staIf.GetAddress(nTcp + nQuic + i)/*target: client address*/, port));
            udpOnoffDown.SetConstantRate(DataRate(onOffDownRate), onOffPktSize);
            udpServer.Add(udpOnoffDown.Install(udpServerNode.Get(0)));
            PacketSinkHelper udpSinkDown("ns3::UdpSocketFactory", InetSocketAddress(staIf.GetAddress(nTcp + nQuic + i)/*target: client address*/, port));
            udpClient = udpSinkDown.Install(wifiUdpStaNodes.Get(i));
            udpOnoffDown.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant="+ ofOnTime +"]"));
            udpOnoffDown.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant="+ ofOffTime +"]"));
        }
    }

    if(nTcp > 0){
        tcpServer.Start(Seconds(tcpStart));
        tcpClient.Start(Seconds(tcpStart));
        tcpServer.Stop(Seconds(simuTime));
        tcpClient.Stop(Seconds(simuTime));
    }
    if(nQuic > 0){
        quicServer.Start(Seconds(quicStart));
        quicClient.Start(Seconds(quicStart));
        quicServer.Stop(Seconds(simuTime));
        quicClient.Stop(Seconds(simuTime));
    }
    if(nUdp > 0){
        udpServer.Start(Seconds(udpStart));
        udpClient.Start(Seconds(udpStart));
        udpServer.Stop(Seconds(simuTime));
        udpClient.Stop(Seconds(simuTime));
    }

    for(int i = 0; i < nTcp; i++){
        NodeStatistics* nodeStat = new NodeStatistics(
            NodeContainer(wifiTcpStaNodes.Get(i), tcpServerNode.Get(0)),
            "./" + folderName +  "/" + "tcp-flow"+std::to_string(i), isDoubleStream);
        Simulator::Schedule(Seconds(0.5 + stepsTime),
                            &NodeStatistics::AdvancePosition,
                            nodeStat,
                            wifiTcpStaNodes.Get(i),
                            stepsSize,
                            stepsTime);
        int nodeNum = i;
        Config::Connect("/NodeList/" + std::to_string(nodeNum) + "/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferRx",
                        MakeCallback(&NodeStatistics::MonitorSnifferRxCallback, nodeStat));
    }
    for(int i = 0; i < nQuic; i++){
        NodeStatistics* nodeStat = new NodeStatistics(
            NodeContainer(wifiQuicStaNodes.Get(i), quicServerNode.Get(0)),
            "./" + folderName +  "/" + "quic-flow"+std::to_string(i), isDoubleStream);
        Simulator::Schedule(Seconds(0.5 + stepsTime),
                            &NodeStatistics::AdvancePosition,
                            nodeStat,
                            wifiQuicStaNodes.Get(i),
                            stepsSize,
                            stepsTime);
        int nodeNum = nTcp + i;
        Config::Connect("/NodeList/" + std::to_string(nodeNum) + "/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferRx",
                        MakeCallback(&NodeStatistics::MonitorSnifferRxCallback, nodeStat));
    }
    for(int i = 0; i < nUdp; i++){
        NodeStatistics* nodeStat = new NodeStatistics(
            NodeContainer(wifiUdpStaNodes.Get(i), udpServerNode.Get(0)),
            "./" + folderName +  "/" + "udp-flow"+std::to_string(i),isDoubleStream);
        Simulator::Schedule(Seconds(0.5 + stepsTime),
                            &NodeStatistics::AdvancePosition,
                            nodeStat,
                            wifiUdpStaNodes.Get(i),
                            stepsSize,
                            stepsTime);
        int nodeNum = nTcp + nQuic + i;
        Config::Connect("/NodeList/" + std::to_string(nodeNum) + "/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferRx",
                        MakeCallback(&NodeStatistics::MonitorSnifferRxCallback, nodeStat));
    }

//    p2pApGw.EnablePcap( "./" + folderName + "/" + "apToGw", apToGw);

    Simulator::Stop(Seconds(simuTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}

