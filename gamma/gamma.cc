#include "ns3/applications-module.h"
#include "ns3/config-store.h"
#include "ns3/core-module.h"
#include "ns3/enum.h"
#include "ns3/error-model.h"
#include "ns3/event-id.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/quic-module.h"
#include "ns3/ssid.h"
#include "ns3/tcp-header.h"
#include "ns3/traffic-control-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/udp-header.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"

#include <ctime>
#include <filesystem>
#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Gamma");

/** Node statistics */
class NodeStatistics{
  public:
    std::string flowName;
    int stepItr = 0;
    FlowMonitorHelper fh;
    Ptr<FlowMonitor> monitor;
    AsciiTraceHelper asciiHelper;
    NodeContainer nodes;
    std::string tcpNodes[4] = {"0", "1", "5", "6"};
    std::string quicNodes[4] = {"2", "3", "7", "8"};

    Ptr<OutputStreamWrapper> metricsCsv;
    Ptr<OutputStreamWrapper> cwndCsv;
    std::map<std::string,Ptr<OutputStreamWrapper>> nodeCwnd;
    std::map<std::string,Ptr<OutputStreamWrapper>> nodeSsth;
    std::map<std::string,Ptr<OutputStreamWrapper>> nodeHandshake;


    NodeStatistics(NodeContainer nodes, std::string flowName, int tcpOrQuicOrUdp);
    void Metrics();
    std::string GetNodeIdFromContext(std::string context);
    void CwndTracer(std::string context, uint32_t oldval, uint32_t newval);
    void SsThTracer(std::string context, uint32_t oldval, uint32_t newval);
    void TcpHandshakeTracer(std::string context, const TcpSocket::TcpStates_t oldState, const TcpSocket::TcpStates_t newState);
    void QuicHandshakeTracer(std::string context,  const QuicSocket::QuicStates_t oldState, const QuicSocket::QuicStates_t newState);
    void RegisterTcpCwnd(std::string node);
    void RegisterQuicCwnd(std::string node);
    void AdvancePosition(int stepsTime);
    std::string TcpStateToString(TcpSocket::TcpStates_t state);
    std::string QuicStateToString(QuicSocket::QuicStates_t state);

};

NodeStatistics::NodeStatistics(NodeContainer nodes, std::string flowName, int tcpOrQuicOrUdp): fh(), asciiHelper(){
    this->nodes = nodes;
    this->flowName = flowName;
    monitor = fh.Install(this->nodes);

    std::ostringstream metrics; metrics << flowName << "-metrics.csv";
    metricsCsv = asciiHelper.CreateFileStream(metrics.str().c_str());
    *metricsCsv->GetStream() << "step,kbps,jtr,plr,del,sen,rcv,source" << std::endl;


        for(int i = 0; i <4; i++){
            std::string node = "";
            if (tcpOrQuicOrUdp == 0){
                node = tcpNodes[i];
                Simulator::Schedule(Seconds(1.2), &NodeStatistics::RegisterTcpCwnd, this, node);

            } else if (tcpOrQuicOrUdp == 1){
                node = quicNodes[i];
                Simulator::Schedule(Seconds(1.2), &NodeStatistics::RegisterQuicCwnd, this, node);
            }
            if (tcpOrQuicOrUdp == 0 || tcpOrQuicOrUdp == 1)
            {
                std::ostringstream cwnd;
                cwnd << flowName << "-node" << node << "-cwnd.csv";
                nodeCwnd[node] = asciiHelper.CreateFileStream(cwnd.str().c_str());
                *nodeCwnd[node]->GetStream() << "time,old,new" << std::endl;

                std::ostringstream ssth;
                ssth << flowName << "-node" << node << "-ssth.csv";
                nodeSsth[node] = asciiHelper.CreateFileStream(ssth.str().c_str());
                *nodeSsth[node]->GetStream() << "time,old,new" << std::endl;

                std::ostringstream handshake;
                handshake << flowName << "-node" << node << "-handshake.csv";
                nodeHandshake[node] = asciiHelper.CreateFileStream(handshake.str().c_str());
                *nodeHandshake[node]->GetStream() << "time,old,new" << std::endl;
            }
        }




}

void NodeStatistics::RegisterTcpCwnd(std::string node){
    Config::Connect("/NodeList/"+(node)+"/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&NodeStatistics::CwndTracer, this));
    Config::Connect("/NodeList/"+(node)+"/$ns3::TcpL4Protocol/SocketList/0/SlowStartThreshold", MakeCallback (&NodeStatistics::SsThTracer, this));
    Config::Connect("/NodeList/"+(node)+"/$ns3::TcpL4Protocol/SocketList/0/State", MakeCallback (&NodeStatistics::TcpHandshakeTracer, this));
}

void NodeStatistics::RegisterQuicCwnd(std::string node){
    Config::Connect ("/NodeList/"+(node)+"/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/CongestionWindow", MakeCallback (&NodeStatistics::CwndTracer, this));
    Config::Connect ("/NodeList/"+(node)+"/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/SlowStartThreshold", MakeCallback (&NodeStatistics::SsThTracer, this));
    Config::Connect ("/NodeList/"+(node)+"/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/State", MakeCallback (&NodeStatistics::QuicHandshakeTracer, this));
}

void NodeStatistics::AdvancePosition(int stepsTime){
    NS_LOG_INFO("### Advancing: step " << stepItr << "; Protocol: " << this->flowName << " ###");
    Metrics();
    stepItr++;
    Simulator::Schedule(Seconds(stepsTime),
                        &NodeStatistics::AdvancePosition,
                        this,
                        stepsTime);
}

void NodeStatistics::Metrics(){
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (fh.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin(); iter != stats.end(); ++iter){
        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(iter->first);
        int64_t new_kbps = iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds() + 0.01 - iter->second.timeFirstTxPacket.GetSeconds())/1024;
        int64_t new_jtr = iter->second.jitterSum.GetMilliSeconds();
        int64_t new_plr = std::abs((int)iter->second.txPackets - (int)iter->second.rxPackets)*100/(iter->second.txPackets + 0.01);
        int64_t new_del = iter->second.lastDelay.GetMilliSeconds();
        int64_t new_sen = iter->second.txPackets;
        int64_t new_rcv = iter->second.rxPackets;

        NS_LOG_UNCOND(" " << stepItr << "|kbps:"
                          << new_kbps << "|jtr:"
                          << new_jtr << "|plr:" //jitter_mils
                          << new_plr << "|del:" //packet loss ratio
                          << new_del << "|sen:" //delay_mils
                          << new_sen << "|rcv:" //packet sent
                          << new_rcv << "|source:" //packet receive
                          << tuple.sourceAddress
        );

        *metricsCsv->GetStream() << stepItr << ","
                                 << new_kbps << ","
                                 << new_jtr << ","
                                 << new_plr << ","
                                 << new_del << ","
                                 << new_sen << ","
                                 << new_rcv << ","
                                 << tuple.sourceAddress << std::endl;

    }

    this->monitor->ResetAllStats();
}

std::string
NodeStatistics::GetNodeIdFromContext(std::string context)
{
    const std::size_t n1 = context.find_first_of('/', 1);
    const std::size_t n2 = context.find_first_of('/', n1 + 1);
    return context.substr(n1 + 1, n2 - n1 - 1);
}

void
NodeStatistics::CwndTracer(std::string context, uint32_t oldval, uint32_t newval){
    std::string nodeId = GetNodeIdFromContext(context);
    *nodeCwnd[nodeId]->GetStream() << Simulator::Now().GetSeconds() << "," << oldval << "," << newval << std::endl;
}

void
NodeStatistics::SsThTracer(std::string context, uint32_t oldval, uint32_t newval){
    std::string nodeId = GetNodeIdFromContext(context);
    *nodeSsth[nodeId]->GetStream() << Simulator::Now().GetSeconds() << "," << oldval << "," << newval << std::endl;
}

void
NodeStatistics::TcpHandshakeTracer(std::string context,  const TcpSocket::TcpStates_t oldState, const TcpSocket::TcpStates_t newState){
    std::string nodeId = GetNodeIdFromContext(context);
    *nodeHandshake[nodeId]->GetStream() << Simulator::Now().GetSeconds() << "," << TcpStateToString(oldState) << "," << TcpStateToString(newState) << std::endl;
}

void
NodeStatistics::QuicHandshakeTracer(std::string context,  const QuicSocket::QuicStates_t oldState, const QuicSocket::QuicStates_t newState){
    std::string nodeId = GetNodeIdFromContext(context);
    *nodeHandshake[nodeId]->GetStream() << Simulator::Now().GetSeconds() << "," << QuicStateToString(oldState) << "," << QuicStateToString(newState) << std::endl;
}

std::string
NodeStatistics::TcpStateToString(TcpSocket::TcpStates_t state)
{
    switch (state)
    {
    case TcpSocket::CLOSED:
        return "CLOSED";
    case TcpSocket::LISTEN:
        return "LISTEN";
    case TcpSocket::SYN_SENT:
        return "SYN_SENT";
    case TcpSocket::SYN_RCVD:
        return "SYN_RCVD";
    case TcpSocket::ESTABLISHED:
        return "ESTABLISHED";
    case TcpSocket::CLOSE_WAIT:
        return "CLOSE_WAIT";
    case TcpSocket::LAST_ACK:
        return "LAST_ACK";
    case TcpSocket::FIN_WAIT_1:
        return "FIN_WAIT_1";
    case TcpSocket::FIN_WAIT_2:
        return "FIN_WAIT_2";
    case TcpSocket::CLOSING:
        return "CLOSING";
    case TcpSocket::TIME_WAIT:
        return "TIME_WAIT";
    default:
        return "UNKNOWN";
    }
}

std::string
NodeStatistics::QuicStateToString(QuicSocket::QuicStates_t state){
    switch (state)
    {
        case QuicSocket::IDLE:
            return "IDLE";
        case QuicSocket::LISTENING:
            return "LISTENING";
        case QuicSocket::CONNECTING_SVR:
            return "CONNECTING_SVR";
        case QuicSocket::CONNECTING_CLT:
            return "CONNECTING_CLT";
        case QuicSocket::OPEN:
            return "OPEN";
        case QuicSocket::CLOSING:
            return "CLOSING";
        case QuicSocket::LAST_STATE:
            return "LAST_STATE";
        default:
            return "UNKNOWN";
    }
}

int main(){
    LogComponentEnable("Gamma", LOG_LEVEL_INFO);

    std::string p2pGwDataRate = "1Gbps";
    std::string p2pGwDelay = "2ms";

    std::string transport_prot = "ns3::TcpNewReno";

    std::string transportProts[9] = {
        "ns3::TcpNewReno",
        "ns3::TcpNewReno",
        "ns3::TcpNewReno",
        "ns3::TcpNewReno",

        "",

        "ns3::TcpNewReno",
        "ns3::TcpNewReno",
        "ns3::TcpNewReno",
        "ns3::TcpNewReno"


    };

    double error_p = 0.0;

    double duration = 10.0;
    double fileSize = 0;
    double packetSize = 1024;

    int ATcpFlowNum = 0; //0/1/2
    int AQuicFlowNum = 0; //0/1/2
    int AUdpFlowNum = 0; //0/1

    int BTcpFlowNum = 0; //0/1/2
    int BQuicFlowNum = 1; //0/1/2
    int BUdpFlowNum = 0; //0/1


    int Dist = 10;
    double speed = 0;       // m/s
    double initXPos = 5;

    Vector speedA = Vector (-speed, 0, 0);
    Vector speedB = Vector (-speed, 0, 0);

    //Create BulkSend application for both TCP and QUIC clients
    uint16_t dlPort_tcp = 443;
    uint16_t dlPort_quic = 443;

    std::string propagationDelay = "ns3::ConstantSpeedPropagationDelayModel";
    std::string propagationLoss = "ns3::FriisPropagationLossModel";

    double p2pDelay = 0.0; //milis

    NodeContainer Tcp1ANodes; Tcp1ANodes.Create(1); Vector Tcp1APos = Vector (initXPos, 8, 0);
    NodeContainer Tcp2ANodes; Tcp2ANodes.Create(1); Vector Tcp2APos = Vector (initXPos, 9, 0);
    NodeContainer Quic1ANodes; Quic1ANodes.Create(1);  Vector Quic1APos = Vector (initXPos, 10, 0);
    NodeContainer Quic2ANodes; Quic2ANodes.Create(1);  Vector Quic2APos = Vector (initXPos, 11, 0);
    NodeContainer UdpANodes; UdpANodes.Create(1);  Vector UdpAPos = Vector (initXPos, 12, 0);

    NodeContainer Tcp1BNodes; Tcp1BNodes.Create(1); Vector Tcp1BPos = Vector (initXPos, Dist + 8, 0);
    NodeContainer Tcp2BNodes; Tcp2BNodes.Create(1); Vector Tcp2BPos = Vector (initXPos, Dist + 9, 0);
    NodeContainer Quic1BNodes; Quic1BNodes.Create(1); Vector Quic1BPos = Vector (initXPos, Dist + 10, 0);
    NodeContainer Quic2BNodes; Quic2BNodes.Create(1); Vector Quic2BPos = Vector (initXPos, Dist + 11, 0);
    NodeContainer UdpBNodes; UdpBNodes.Create(1); Vector UdpBPos = Vector (initXPos, Dist + 12, 0);

    NodeContainer apANodes; apANodes.Create(1); Vector eNBAPos = Vector (10, 10, 0);
    NodeContainer apBNodes; apBNodes.Create(1); Vector eNBBPos = Vector (10, Dist + 10, 0);

    NodeContainer gwANodes; gwANodes.Create(1); Vector gwAPos = Vector (15, (Dist/2)+10, 0);
    NodeContainer gwBNodes; gwBNodes.Create(1); Vector gwBPos = Vector (20, (Dist/2)+10, 0);

    NodeContainer TcpSrvNodes; TcpSrvNodes.Create(1); Vector TcpSrvPos = Vector (25, 10, 0);
    NodeContainer QuicSrvNodes; QuicSrvNodes.Create(1); Vector QuicSrvPos = Vector (25, Dist + 10, 0);
    NodeContainer UdpSrvNodes; UdpSrvNodes.Create(1); Vector UdpSrvPos = Vector (25, Dist + 20, 0);

    NodeContainer TcpUeNodes = NodeContainer(Tcp1ANodes, Tcp2ANodes, Tcp1BNodes, Tcp2BNodes);
    NodeContainer QuicUeNodes = NodeContainer(Quic1ANodes, Quic2ANodes, Quic1BNodes, Quic2BNodes);
    NodeContainer UdpUeNodes = NodeContainer(UdpANodes, UdpBNodes);

    NodeContainer TcpAUeNodes = NodeContainer(Tcp1ANodes, Tcp2ANodes);
    NodeContainer TcpBUeNodes = NodeContainer(Tcp1BNodes, Tcp2BNodes);

    NodeContainer QuicAUeNodes = NodeContainer(Quic1ANodes, Quic2ANodes);
    NodeContainer QuicBUeNodes = NodeContainer(Quic1BNodes, Quic2BNodes);

    //Set Propagation Loss
    //lteHelper->SetAttribute("PathlossModel", StringValue("ns3::FriisPropagationLossModel"));
    //lteHelper->SetPathlossModelAttribute("Frequency", DoubleValue(2145000000));

    // Install Mobility Model in UE
    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    ueMobility.Install(Tcp1ANodes); Tcp1ANodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Tcp1APos); Tcp1ANodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedA);
    ueMobility.Install(Tcp2ANodes); Tcp2ANodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Tcp2APos); Tcp2ANodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedA);
    ueMobility.Install(Quic1ANodes); Quic1ANodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Quic1APos); Quic1ANodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedA);
    ueMobility.Install(Quic2ANodes); Quic2ANodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Quic2APos); Quic2ANodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedA);
    ueMobility.Install(UdpANodes); UdpANodes.Get(0)->GetObject<MobilityModel>()->SetPosition(UdpAPos); UdpANodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedA);

    ueMobility.Install(Tcp1BNodes); Tcp1BNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Tcp1BPos); Tcp1BNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedB);
    ueMobility.Install(Tcp2BNodes); Tcp2BNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Tcp2BPos); Tcp2BNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedB);
    ueMobility.Install(Quic1BNodes); Quic1BNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Quic1BPos); Quic1BNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedB);
    ueMobility.Install(Quic2BNodes); Quic2BNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Quic2BPos); Quic2BNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedB);
    ueMobility.Install(UdpBNodes); UdpBNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(UdpBPos); UdpBNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedB);

    //Set mobility for eNB nodes and UE nodes
    MobilityHelper eNBmobility;
    Ptr<ListPositionAllocator> eNBposition_allocator = CreateObject<ListPositionAllocator> ();
    eNBposition_allocator->Add(eNBAPos);
    eNBposition_allocator->Add(eNBBPos);
    eNBmobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    eNBmobility.SetPositionAllocator(eNBposition_allocator);
    eNBmobility.Install(apANodes.Get(0));
    eNBmobility.Install(apBNodes.Get(0));

    MobilityHelper PGWmobility;
    Ptr<ListPositionAllocator> PGWposition_allocator = CreateObject<ListPositionAllocator> ();
    PGWposition_allocator->Add(gwAPos);
    PGWposition_allocator->Add(gwBPos);
    PGWmobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    PGWmobility.SetPositionAllocator(PGWposition_allocator);
    PGWmobility.Install(gwANodes);
    PGWmobility.Install(gwBNodes);

    //Set mobility for TCP server node
    MobilityHelper Tcpservermobility;
    Ptr<ListPositionAllocator> TcpserverPosition_allocator = CreateObject<ListPositionAllocator> ();
    TcpserverPosition_allocator->Add(TcpSrvPos);
    Tcpservermobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Tcpservermobility.SetPositionAllocator(TcpserverPosition_allocator);
    Tcpservermobility.Install(TcpSrvNodes);

    //Set mobility for QUIC server node
    MobilityHelper Quicservermobility;
    Ptr<ListPositionAllocator> QuicserverPosition_allocator = CreateObject<ListPositionAllocator> ();
    QuicserverPosition_allocator->Add(QuicSrvPos);
    Quicservermobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Quicservermobility.SetPositionAllocator(QuicserverPosition_allocator);
    Quicservermobility.Install(QuicSrvNodes);

    //Set mobility for UDP server node
    MobilityHelper Udpservermobility;
    Ptr<ListPositionAllocator> UdpserverPosition_allocator = CreateObject<ListPositionAllocator> ();
    UdpserverPosition_allocator->Add(UdpSrvPos);
    Udpservermobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Udpservermobility.SetPositionAllocator(UdpserverPosition_allocator);
    Udpservermobility.Install(UdpSrvNodes);

    //Create Error Model for Internet Link
    Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable> ();
    RateErrorModel error_model;
    error_model.SetRandomVariable (uv);
    error_model.SetUnit (RateErrorModel::ERROR_UNIT_PACKET);
    error_model.SetRate (error_p);

    //Create remote server and add IP stack
    QuicHelper stackQuic;
    stackQuic.InstallQuic( QuicSrvNodes);
    stackQuic.InstallQuic( QuicAUeNodes);
    stackQuic.InstallQuic( QuicBUeNodes);
    InternetStackHelper stack;
    stack.Install(TcpAUeNodes);
    stack.Install(UdpANodes);
    stack.Install(apANodes);

    stack.Install(TcpBUeNodes);
    stack.Install(UdpBNodes);
    stack.Install(apBNodes);


    stack.Install(gwANodes);
    stack.Install(gwBNodes);

    stack.Install(TcpSrvNodes);
    stack.Install(UdpSrvNodes);

    // Create the TCP Internet link
    PointToPointHelper TcpremoteServerp2p;//point to point line between PGW and remote server
    TcpremoteServerp2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    TcpremoteServerp2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(p2pDelay)));
    TcpremoteServerp2p.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (&error_model));
    NetDeviceContainer TcpinternetDevices;
    TcpinternetDevices = TcpremoteServerp2p.Install(gwBNodes.Get(0), TcpSrvNodes.Get(0));
    Ipv4AddressHelper Tcpipv4helper;
    Tcpipv4helper.SetBase("1.0.0.0", "255.255.255.252");
    Ipv4InterfaceContainer TcpinternetIpIfaces = Tcpipv4helper.Assign(TcpinternetDevices);

    // Create the QUIC Internet link
    PointToPointHelper QuicremoteServerp2p;//point to point line between PGW and remote server
    QuicremoteServerp2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    QuicremoteServerp2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(p2pDelay)));
    QuicremoteServerp2p.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (&error_model));
    NetDeviceContainer QuicinternetDevices;
    QuicinternetDevices = QuicremoteServerp2p.Install(gwBNodes.Get(0), QuicSrvNodes.Get(0));
    Ipv4AddressHelper Quicipv4helper;
    Quicipv4helper.SetBase("2.0.0.0", "255.255.255.252");
    Ipv4InterfaceContainer QuicinternetIpIfaces = Quicipv4helper.Assign(QuicinternetDevices);

    // Create the UDP Internet link
    PointToPointHelper UdpremoteServerp2p;//point to point line between PGW and remote server
    UdpremoteServerp2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    UdpremoteServerp2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(p2pDelay)));
    UdpremoteServerp2p.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (&error_model));
    NetDeviceContainer UdpinternetDevices;
    UdpinternetDevices = UdpremoteServerp2p.Install(gwBNodes.Get(0), UdpSrvNodes.Get(0));
    Ipv4AddressHelper Udpipv4helper;
    Udpipv4helper.SetBase("3.0.0.0", "255.255.255.252");
    Ipv4InterfaceContainer UdpinternetIpIfaces = Udpipv4helper.Assign(UdpinternetDevices);

    // Create link between GW
    PointToPointHelper gwToGwP2p;
    gwToGwP2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    gwToGwP2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(p2pDelay)));
    gwToGwP2p.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (&error_model));
    NetDeviceContainer gwNetDevices;
    gwNetDevices = gwToGwP2p.Install(gwBNodes.Get(0), gwANodes.Get(0));
    Ipv4AddressHelper gwIpV4Helper;
    gwIpV4Helper.SetBase("10.0.1.0", "255.255.255.252");
    Ipv4InterfaceContainer gwInternetIpIfaces = gwIpV4Helper.Assign(gwNetDevices);


    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    YansWifiPhyHelper wifiPhy;
    YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
    wifiPhy.SetChannel(wifiChannel.Create());
    wifiPhy.Set("ChannelSettings", StringValue("{0, 0, BAND_2_4GHZ, 0}"));
    wifiChannel.SetPropagationDelay(propagationDelay);
    wifiChannel.AddPropagationLoss(propagationLoss);
    WifiMacHelper wifiMac;
    Ssid ssid1 = Ssid("AP1");
    wifiMac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid1));//STA1
    NetDeviceContainer wifiATcpStaDevices;
    wifiATcpStaDevices.Add(wifi.Install(wifiPhy, wifiMac, TcpAUeNodes));
    NetDeviceContainer wifiAQuicStaDevices;
    wifiAQuicStaDevices.Add(wifi.Install(wifiPhy, wifiMac, QuicAUeNodes));
    NetDeviceContainer wifiAUdpStaDevices;
    wifiAUdpStaDevices.Add(wifi.Install(wifiPhy, wifiMac, UdpANodes));
    wifiMac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid1));//AP
    NetDeviceContainer wifiApDevices = wifi.Install(wifiPhy, wifiMac, apANodes);


    WifiHelper wifi2;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    YansWifiPhyHelper wifiPhy2;
    YansWifiChannelHelper wifiChannel2 = YansWifiChannelHelper::Default();
    wifiPhy2.SetChannel(wifiChannel2.Create());
    wifiPhy2.Set("ChannelSettings", StringValue("{0, 0, BAND_2_4GHZ, 0}"));
    wifiChannel2.SetPropagationDelay(propagationDelay);
    wifiChannel2.AddPropagationLoss(propagationLoss);
    WifiMacHelper wifiMac2;
    Ssid ssid2 = Ssid("AP2");
    wifiMac2.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid2));//STA2
    NetDeviceContainer wifiBTcpStaDevices;
    wifiBTcpStaDevices.Add(wifi.Install(wifiPhy2, wifiMac2, TcpBUeNodes));
    NetDeviceContainer wifiBQuicStaDevices;
    wifiBQuicStaDevices.Add(wifi.Install(wifiPhy2, wifiMac2, QuicBUeNodes));
    NetDeviceContainer wifiBUdpStaDevices;
    wifiBUdpStaDevices.Add(wifi.Install(wifiPhy2, wifiMac2, UdpBNodes));
    wifiMac2.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid2));//AP
    NetDeviceContainer wifiBApDevices = wifi.Install(wifiPhy2, wifiMac2, apBNodes);


    PointToPointHelper APAP2p;//point to point line between AP A and GW A
    APAP2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    APAP2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(p2pDelay)));
    APAP2p.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (&error_model));
    NetDeviceContainer APADevices;
    APADevices = APAP2p.Install(gwANodes.Get(0), apANodes.Get(0));
    Ipv4AddressHelper APAipv4helper;
    APAipv4helper.SetBase("10.1.22.0", "255.255.255.252");
    Ipv4InterfaceContainer APAIpIfaces = APAipv4helper.Assign(APADevices);


    PointToPointHelper APBP2p;//point to point line between AP A and GW A
    APBP2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    APBP2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(p2pDelay)));
    APBP2p.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (&error_model));
    NetDeviceContainer APBDevices;
    APBDevices = APBP2p.Install(gwANodes.Get(0), apBNodes.Get(0));
    Ipv4AddressHelper APBipv4helper;
    APBipv4helper.SetBase("10.2.22.0", "255.255.255.252");
    Ipv4InterfaceContainer APBIpIfaces = APBipv4helper.Assign(APBDevices);


    //Create Routing Information
    Ipv4AddressHelper address;
    address.SetBase("10.2.2.0", "255.255.255.0"); //STA A
    Ipv4InterfaceContainer TcpUeAIf = address.Assign(wifiATcpStaDevices);
    Ipv4InterfaceContainer QuicUeAIf  = address.Assign(wifiAQuicStaDevices);
    Ipv4InterfaceContainer UdpUeAIf  = address.Assign(wifiAUdpStaDevices);
    Ipv4InterfaceContainer APAAIf  = address.Assign(wifiApDevices);
    address.SetBase("10.3.3.0", "255.255.255.0"); //STA B
    Ipv4InterfaceContainer TcpUeBIf  = address.Assign(wifiBTcpStaDevices);
    Ipv4InterfaceContainer QuicUeBIf  = address.Assign(wifiBQuicStaDevices);
    Ipv4InterfaceContainer UdpUeBIf  = address.Assign(wifiBUdpStaDevices);
    Ipv4InterfaceContainer APBIf  = address.Assign(wifiBApDevices);

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


    for(int i = 0; i < 2; i++) { //TCP A
        //Create bulksend apps
        BulkSendHelper tcpHelper ("ns3::TcpSocketFactory", Address(InetSocketAddress (TcpinternetIpIfaces.GetAddress (1) /*receiver address (server address)*/, dlPort_tcp)));
        if(i > ATcpFlowNum-1){
            tcpHelper.SetAttribute("MaxBytes", UintegerValue(1));
        } else {
            tcpHelper.SetAttribute("MaxBytes", UintegerValue(fileSize));
        }
        tcpHelper.SetAttribute("SendSize", UintegerValue(packetSize));
        ApplicationContainer tcpApp = tcpHelper.Install(TcpAUeNodes.Get(i) /*source nodes (STA)*/);
        int node = i;
        TypeId tid = TypeId::LookupByName(transportProts[node]);
        Config::Set("/NodeList/"+std::to_string(i)+"/$ns3::TcpL4Protocol/SocketType", TypeIdValue(tid));
        tcpApp.Get(0)->SetStartTime(Seconds(1.0 + (i*0.1)));
        tcpApp.Get(0)->SetStopTime(Seconds(duration-1.05));
    }
    for(int i = 0; i < 2; i++) { //TCP B
        //Create bulksend apps
        BulkSendHelper tcpHelper ("ns3::TcpSocketFactory", Address(InetSocketAddress (TcpinternetIpIfaces.GetAddress (1) /*receiver address (server address)*/, dlPort_tcp)));
        tcpHelper.SetAttribute("MaxBytes", UintegerValue(fileSize));
        if(i > BTcpFlowNum-1){
            tcpHelper.SetAttribute("MaxBytes", UintegerValue(1));
        } else {
            tcpHelper.SetAttribute("MaxBytes", UintegerValue(fileSize));
        }
        ApplicationContainer tcpApp = tcpHelper.Install(TcpBUeNodes.Get(i) /*source nodes (STA)*/);
        int node = 5+i;
        TypeId tid = TypeId::LookupByName(transportProts[node]);
        Config::Set("/NodeList/"+std::to_string(i)+"/$ns3::TcpL4Protocol/SocketType", TypeIdValue(tid));
        tcpApp.Get(0)->SetStartTime(Seconds(1.0 + (i*0.1)));
        tcpApp.Get(0)->SetStopTime(Seconds(duration-1.05));
    }
    //Create Packet Sink on UE
    PacketSinkHelper TcpSrvSinkHelper("ns3::TcpSocketFactory", Address(InetSocketAddress (TcpinternetIpIfaces.GetAddress (1) /*reveicer target (server address)*/, dlPort_tcp)));
    TcpSrvSinkHelper.SetAttribute ("Protocol", TypeIdValue (TcpSocketFactory::GetTypeId ()));
    ApplicationContainer TcpSrvSinkApp = TcpSrvSinkHelper.Install(TcpSrvNodes.Get(0) /*reveicer target (server address)*/);
    TcpSrvSinkApp.Get(0)->SetStartTime(Seconds(0.6));
    TcpSrvSinkApp.Get(0)->SetStopTime(Seconds(duration));


    for(int i = 0; i < 2; i++) {
        BulkSendHelper quicHelper ("ns3::QuicSocketFactory", Address(InetSocketAddress (QuicinternetIpIfaces.GetAddress (1 )  /*receiver address (server address)*/, dlPort_quic)));
        if(i > AQuicFlowNum-1){
            quicHelper.SetAttribute("MaxBytes", UintegerValue(1));
        } else {
            quicHelper.SetAttribute("MaxBytes", UintegerValue(fileSize));
        }
        quicHelper.SetAttribute("SendSize", UintegerValue(packetSize));
        ApplicationContainer quicApp = quicHelper.Install(QuicAUeNodes.Get(i));
        int node = 2+i;
        TypeId tid = TypeId::LookupByName(transportProts[node]);
        Config::Set("/NodeList/"+std::to_string(i)+"/$ns3::QuicL4Protocol/SocketType", TypeIdValue(tid));
        quicApp.Get(0)->SetStartTime(Seconds(1.05 + (i*0.1)));
        quicApp.Get(0)->SetStopTime(Seconds(duration-1.0));
    }
    for(uint16_t i = 0; i < 2; i++) {
        BulkSendHelper quicHelper ("ns3::QuicSocketFactory", Address(InetSocketAddress (QuicinternetIpIfaces.GetAddress (1 )  /*receiver address (server address)*/, dlPort_quic)));
        if(i > BQuicFlowNum-1){
            quicHelper.SetAttribute("MaxBytes", UintegerValue(1));
        } else {
            quicHelper.SetAttribute("MaxBytes", UintegerValue(fileSize));
        }
        quicHelper.SetAttribute("SendSize", UintegerValue(packetSize));
        ApplicationContainer quicApp = quicHelper.Install(QuicBUeNodes.Get(i));
        int node = 7+i;
        TypeId tid = TypeId::LookupByName(transportProts[node]);
        Config::Set("/NodeList/"+std::to_string(i)+"/$ns3::QuicL4Protocol/SocketType", TypeIdValue(tid));
        quicApp.Get(0)->SetStartTime(Seconds(1.05 + (i*0.1)));
        quicApp.Get(0)->SetStopTime(Seconds(duration-1.0));
    }
    //Create Packet Sink on UE
    PacketSinkHelper QuicSrvSinkHelper("ns3::QuicSocketFactory", Address(InetSocketAddress (QuicinternetIpIfaces.GetAddress (1) /*receiver address (server)*/, dlPort_quic)));
    QuicSrvSinkHelper.SetAttribute ("Protocol", TypeIdValue (QuicSocketFactory::GetTypeId ()));
    ApplicationContainer QuicSinkApp = QuicSrvSinkHelper.Install(QuicSrvNodes.Get(0));
    QuicSinkApp.Get(0)->SetStartTime(Seconds(0.6));
    QuicSinkApp.Get(0)->SetStopTime(Seconds(duration));


    OnOffHelper udpOnoffA("ns3::UdpSocketFactory", InetSocketAddress(UdpinternetIpIfaces.GetAddress(1), 3000));
    if (AUdpFlowNum < 1){
        udpOnoffA.SetConstantRate(DataRate("1b/s"), 1420);
        udpOnoffA.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        udpOnoffA.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    } else {
        udpOnoffA.SetConstantRate(DataRate("100Mb/s"), 1420);
        udpOnoffA.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        udpOnoffA.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    }
    ApplicationContainer udpSenderA; udpSenderA.Add(udpOnoffA.Install(UdpANodes.Get(0)));
    udpSenderA.Get(0)->SetStartTime(Seconds(1.5)); udpSenderA.Get(0)->SetStopTime(Seconds(duration - 1.0));

    OnOffHelper udpOnoffB("ns3::UdpSocketFactory", InetSocketAddress(UdpinternetIpIfaces.GetAddress(1), 3000));
    if (BUdpFlowNum < 1){
        udpOnoffB.SetConstantRate(DataRate("1b/s"), 1420);
        udpOnoffB.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        udpOnoffB.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    } else {
        udpOnoffB.SetConstantRate(DataRate("100Mb/s"), 1420);
        udpOnoffB.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        udpOnoffB.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    }
    ApplicationContainer udpSenderB; udpSenderB.Add(udpOnoffB.Install(UdpBNodes.Get(0)));
    udpSenderB.Get(0)->SetStartTime(Seconds(1.5)); udpSenderB.Get(0)->SetStopTime(Seconds(duration - 1.0));

    PacketSinkHelper udpReceiverSink("ns3::UdpSocketFactory", InetSocketAddress(UdpinternetIpIfaces.GetAddress(1), 3000));
    ApplicationContainer udpSinkApp = udpReceiverSink.Install(UdpSrvNodes.Get(0));
    udpSinkApp.Get(0)->SetStartTime(Seconds(0.6)); udpSinkApp.Get(0)->SetStopTime(Seconds(duration));



    std::time_t unixNow = std::time(0);
    struct tm *localTime = localtime(&unixNow);
    std::stringstream formattedTime; formattedTime << std::put_time(localTime, "%Y-%m-%d_%H:%M:%S");
    std::string folderName = formattedTime.str();
    std::filesystem::path directoryPath = "./"+ folderName +"/";
    std::filesystem::create_directory(directoryPath);
    TcpremoteServerp2p.EnablePcap( "./" + folderName + "/" + "apToGw", gwBNodes);
//    AnimationInterface anim("./" + folderName + "/gamma.xml");

    std::cout << "***Simulation is Starting***" << std::endl;
    Simulator::Stop(Seconds(duration+1));



    NodeStatistics* nodeStatTcp = new NodeStatistics(NodeContainer(TcpUeNodes,  TcpSrvNodes.Get(0)),"./"+ folderName +"/TCP", 0);
    Simulator::Schedule(Seconds(1),
                        &NodeStatistics::AdvancePosition,
                        nodeStatTcp,
                        1);

    NodeStatistics* nodeStatQuic = new NodeStatistics(NodeContainer(QuicUeNodes,  QuicSrvNodes.Get(0)),"./"+ folderName +"/QUIC", 1);
    Simulator::Schedule(Seconds(1),
                        &NodeStatistics::AdvancePosition,
                        nodeStatQuic,
                        1);

    NodeStatistics* nodeStatUdp = new NodeStatistics(NodeContainer(UdpUeNodes,  UdpSrvNodes.Get(0)),"./"+ folderName +"/UDP", 2);
    Simulator::Schedule(Seconds(1),
                        &NodeStatistics::AdvancePosition,
                        nodeStatUdp,
                        1);


    Simulator::Run();
    Simulator::Destroy();

    return 0;
}

