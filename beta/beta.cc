#include <iostream>
#include <fstream>
#include <string>

#include "ns3/point-to-point-module.h"
#include "ns3/error-model.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/enum.h"
#include "ns3/event-id.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/lte-module.h"
#include "ns3/config-store.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/quic-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/traffic-control-helper.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

#include <arpa/inet.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Beta");

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
    uint32_t cwnd1;

    NodeStatistics(NodeContainer nodes, std::string flowName, bool isDoubleStream, bool tcpOrQuic);
    void Metrics();
    void CwndTracerA1(uint32_t oldval, uint32_t newval);
    void RegisterTcpCwnd();
    void AdvancePosition(int stepsTime);

};

NodeStatistics::NodeStatistics(NodeContainer nodes, std::string flowName, bool isDoubleStream, bool tcpOrQuic): fh(), asciiHelper(){
    this->isDoubleStream = isDoubleStream;
    this->nodes = nodes;
    this->flowName = flowName;
    monitor = fh.Install(this->nodes);
    this->cwnd1 = 0;

    if(tcpOrQuic == 0){ //TCP NODES: 0,1,4,5,10
        Simulator::Schedule(Seconds(2), &NodeStatistics::RegisterTcpCwnd, this);
    }

}

void NodeStatistics::RegisterTcpCwnd(){
    Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&NodeStatistics::CwndTracerA1, this));
    Config::ConnectWithoutContext ("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&NodeStatistics::CwndTracerA1, this));
    Config::ConnectWithoutContext ("/NodeList/4/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&NodeStatistics::CwndTracerA1, this));
    Config::ConnectWithoutContext ("/NodeList/5/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&NodeStatistics::CwndTracerA1, this));
    Config::ConnectWithoutContext ("/NodeList/10/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&NodeStatistics::CwndTracerA1, this));
}

void NodeStatistics::AdvancePosition(int stepsTime){
    NS_LOG_INFO("### ADVANCING: STEP " << stepItr << "; STA NODE_NAME: " << this->flowName << " ###");
    stepItr++;
    Metrics();
    Simulator::Schedule(Seconds(stepsTime),
                        &NodeStatistics::AdvancePosition,
                        this,
                        stepsTime);
}

void NodeStatistics::Metrics(){
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (fh.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    int64_t kbps = 0;
    int64_t jtr = 0;
    int64_t plr = 0;
    int64_t del = 0;
    int64_t sen = 0;
    int64_t rcv = 0;
    double flowNum = 0.00001;


    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin(); iter != stats.end(); ++iter){
        int64_t newKbps = iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds() + 0.01 - iter->second.timeFirstTxPacket.GetSeconds())/1024;
        if (newKbps > 0){
            kbps += iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds() + 0.01 - iter->second.timeFirstTxPacket.GetSeconds())/1024;
            jtr += iter->second.jitterSum.GetMilliSeconds();
            plr += std::abs((int)iter->second.txPackets - (int)iter->second.rxPackets)*100/(iter->second.txPackets + 0.01);
            del += iter->second.lastDelay.GetMilliSeconds();
            sen += iter->second.txPackets;
            rcv += iter->second.rxPackets;
        }
        flowNum++;
    }

    NS_LOG_UNCOND(" " << stepItr << "|kbps:"
                      << kbps/flowNum << "|jtr:"
                      << jtr/flowNum << "|plr:" //jitter_mils
                      << plr/flowNum << "|del:" //packet loss ratio
                      << del/flowNum << "|sen:" //delay_mils
                      << sen/flowNum << "|rcv:" //packet sent
                      << rcv/flowNum << "|cwnd:" //packet receive
                      << this->cwnd1 /5 << "|" //packet receive
    );
    this->monitor->ResetAllStats();
}

void
NodeStatistics::CwndTracerA1(uint32_t oldval, uint32_t newval){
    this->cwnd1 = newval;
}

int main(){
    LogComponentEnable("Beta", LOG_LEVEL_INFO);

    std::string p2pGwDataRate = "1Gbps";
    std::string p2pGwDelay = "2ms";

    std::string onOffDownRate = "100Mb/s";
    std::string onOffUpRate = "100Mb/s";
    std::string ofOnTime = "1";
    std::string ofOffTime = "0";

    uint64_t buffSize = 524288;
    bool use_0RTT = false;
    bool use_2RTT = true;

    std::string tcp_congestion_op = "ns3::TcpNewReno";

    double error_p = 0.0;

    double duration = 10.0;
    double fileSize = 0;
    double packetSize = 1024;

    int ATcpFlowNum = 2; //0/1/2
    int AQuicFlowNum = 2; //0/1/2
    int BTcpFlowNum = 2; //0/1/2
    int BQuicFlowNum = 2; //0/1/2

    int Dist = 400;
    double speed = (Dist/2)/(duration);       // m/s

    Vector speedA = Vector (0, speed, 0);
    Vector speedB = Vector (0, -speed, 0);

    uint16_t numberOfEnbs = 2;

    //Create BulkSend application for both TCP and QUIC clients
    uint16_t dlPort_tcp = 443;
    uint16_t dlPort_quic = 443;

    NodeContainer Tcp1ANodes; Tcp1ANodes.Create(1); Vector Tcp1APos = Vector (5, 8, 0);
    NodeContainer Tcp2ANodes; Tcp2ANodes.Create(1); Vector Tcp2APos = Vector (5, 9, 0);
    NodeContainer Quic1ANodes; Quic1ANodes.Create(1);  Vector Quic1APos = Vector (5, 10, 0);
    NodeContainer Quic2ANodes; Quic2ANodes.Create(1);  Vector Quic2APos = Vector (5, 11, 0);
//    NodeContainer UdpANodes; UdpANodes.Create(1);  Vector UdpAPos = Vector (5, 12, 0);

    NodeContainer Tcp1BNodes; Tcp1BNodes.Create(1); Vector Tcp1BPos = Vector (5, Dist + 8, 0);
    NodeContainer Tcp2BNodes; Tcp2BNodes.Create(1); Vector Tcp2BPos = Vector (5, Dist + 9, 0);
    NodeContainer Quic1BNodes; Quic1BNodes.Create(1); Vector Quic1BPos = Vector (5, Dist + 10, 0);
    NodeContainer Quic2BNodes; Quic2BNodes.Create(1); Vector Quic2BPos = Vector (5, Dist + 11, 0);
//    NodeContainer UdpBNodes; UdpBNodes.Create(1); Vector UdpBPos = Vector (5, 112, 0);

    NodeContainer eNBANodes; eNBANodes.Create(1); Vector eNBAPos = Vector (30, 10, 0);
    NodeContainer eNBBNodes; eNBBNodes.Create(1); Vector eNBBPos = Vector (30, Dist + 10, 0);

    NodeContainer TcpSrvNodes; TcpSrvNodes.Create(1); Vector TcpSrvPos = Vector (Dist, 10, 0);
    NodeContainer QuicSrvNodes; QuicSrvNodes.Create(1); Vector QuicSrvPos = Vector (Dist, Dist + 10, 0);
//    NodeContainer UdpSrvNodes; UdpSrvNodes.Create(1);

    NodeContainer TcpUeNodes = NodeContainer(Tcp1ANodes, Tcp2ANodes, Tcp1BNodes, Tcp2BNodes);
    NodeContainer QuicUeNodes = NodeContainer(Quic1ANodes, Quic2ANodes, Quic1BNodes, Quic2BNodes);

    NodeContainer TcpAUeNodes = NodeContainer(Tcp1ANodes, Tcp2ANodes);
    NodeContainer TcpBUeNodes = NodeContainer(Tcp1BNodes, Tcp2BNodes);

    NodeContainer QuicAUeNodes = NodeContainer(Quic1ANodes, Quic2ANodes);
    NodeContainer QuicBUeNodes = NodeContainer(Quic1BNodes, Quic2BNodes);

    //Create enb in between
    NodeContainer mideNb;
    mideNb.Create (numberOfEnbs);
    Ptr<ListPositionAllocator> midenbPosAlloc = CreateObject<ListPositionAllocator> ();
    int intervalDist = Dist/(numberOfEnbs+1);
    for (uint16_t i = 0; i < numberOfEnbs; i++){
        Vector enbPos(30, intervalDist, 0);
        midenbPosAlloc->Add(enbPos);
        intervalDist += intervalDist;
    }
    MobilityHelper enbMobility;
    enbMobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    enbMobility.SetPositionAllocator (midenbPosAlloc);
    enbMobility.Install (mideNb);


    //Set LTE physical layer parameters

    //Bandwith and Frequencies
    Config::SetDefault("ns3::LteEnbNetDevice::DlBandwidth", UintegerValue(100));
    Config::SetDefault("ns3::LteEnbNetDevice::UlBandwidth", UintegerValue(100));
    Config::SetDefault("ns3::LteEnbNetDevice::DlEarfcn", UintegerValue(2300));
    Config::SetDefault("ns3::LteEnbNetDevice::UlEarfcn", UintegerValue(20300));
    Config::SetDefault("ns3::LteUeNetDevice::DlEarfcn", UintegerValue(2300));

    //Transmission Power and Noise
    Config::SetDefault("ns3::LteEnbPhy::TxPower", DoubleValue(46));
    Config::SetDefault("ns3::LteEnbPhy::NoiseFigure", DoubleValue(5));
    Config::SetDefault("ns3::LteUePhy::TxPower", DoubleValue(23));
    Config::SetDefault("ns3::LteUePhy::NoiseFigure", DoubleValue(9));
    Config::SetDefault("ns3::LteUePhy::EnableUplinkPowerControl", BooleanValue(false));

    //LTE RRC/RLC parameters
    Config::SetDefault ("ns3::LteEnbRrc::DefaultTransmissionMode", UintegerValue (2));
    Config::SetDefault("ns3::LteEnbRrc::SrsPeriodicity", UintegerValue(40));
    Config::SetDefault("ns3::LteEnbRrc::EpsBearerToRlcMapping", StringValue("RlcAmAlways"));
    Config::SetDefault("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue(524288));

    //LTE AMC Model
    Config::SetDefault("ns3::LteAmc::AmcModel", EnumValue(LteAmc::PiroEW2010));
    Config::SetDefault("ns3::LteAmc::Ber", DoubleValue(0.00005));

    //Socket TCP/QUIC Parameters
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1460));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(2));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(buffSize));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(buffSize));
    Config::SetDefault("ns3::UdpSocket::RcvBufSize", UintegerValue(buffSize));
    Config::SetDefault("ns3::QuicSocketBase::InitialPacketSize", UintegerValue (1200));
    Config::SetDefault("ns3::QuicSocketBase::SocketRcvBufSize", UintegerValue (buffSize));
    Config::SetDefault("ns3::QuicSocketBase::SocketSndBufSize", UintegerValue (buffSize));
    Config::SetDefault("ns3::QuicStreamBase::StreamSndBufSize", UintegerValue (buffSize));
    Config::SetDefault("ns3::QuicStreamBase::StreamRcvBufSize", UintegerValue (buffSize));
    Config::SetDefault("ns3::QuicL4Protocol::0RTT-Handshake", BooleanValue (use_0RTT));

    Config::SetDefault("ns3::QuicSocketBase::kMinRTOTimeout", TimeValue (MilliSeconds(1000)));
    Config::SetDefault("ns3::QuicSocketBase::kUsingTimeLossDetection", BooleanValue (false));

    Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds(1000)));
    Config::SetDefault("ns3::RttEstimator::InitialEstimation", TimeValue (MilliSeconds(100)));

    if (use_2RTT == true) {
        Config::SetDefault ("ns3::QuicSocketBase::InitialVersion", UintegerValue (QUIC_VERSION_NEGOTIATION));
    }
    else {
        Config::SetDefault ("ns3::QuicSocketBase::InitialVersion", UintegerValue (QUIC_VERSION_NS3_IMPL));
    }

    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName (tcp_congestion_op)));
    Config::SetDefault ("ns3::QuicL4Protocol::SocketType", TypeIdValue(QuicCongestionOps::GetTypeId()));

    //Create LTE and EPC
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();

    Ptr<PointToPointEpcHelper>  epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);
    Ptr<Node> pgwNode = epcHelper->GetPgwNode(); Vector gwPos = Vector ((Dist*3/4), (Dist/2)+10, 0); //P-GW node
    Ptr<Node> sgwNode = epcHelper->GetSgwNode(); Vector sgwPos = Vector ((Dist/4), (Dist/2)+10, 0); //S-GW node



    epcHelper->SetAttribute("S1uLinkDelay", TimeValue(MilliSeconds(5)));
    epcHelper->SetAttribute("S1uLinkDataRate", DataRateValue(DataRate("1Gbps")));
    epcHelper->SetAttribute("S1uLinkMtu", UintegerValue(2000));
    epcHelper->SetAttribute("S1uLinkEnablePcap", BooleanValue(false));

    //Set RRC protocol
    lteHelper->SetAttribute("UseIdealRrc", BooleanValue(true));

    //Set Propagation Loss
    //lteHelper->SetAttribute("PathlossModel", StringValue("ns3::FriisPropagationLossModel"));
    //lteHelper->SetPathlossModelAttribute("Frequency", DoubleValue(2145000000));

    lteHelper->SetAttribute("PathlossModel", StringValue("ns3::LogDistancePropagationLossModel"));
    lteHelper->SetPathlossModelAttribute("Exponent", DoubleValue(2.42));
    lteHelper->SetPathlossModelAttribute("ReferenceLoss", DoubleValue(30.8));

    //Set Wireless Fading
//    lteHelper->SetFadingModel("ns3::TraceFadingLossModel");
////    lteHelper->SetFadingModelAttribute("TraceFilename", StringValue("/home/ns3/Desktop/quic-ns-3/scratch/fading_trace_modified_40s_EPA_3kmph.fad"));
//    lteHelper->SetFadingModelAttribute ("TraceLength", TimeValue (Seconds (40.0)));
//    lteHelper->SetFadingModelAttribute ("SamplesNum", UintegerValue (20000));
//    lteHelper->SetFadingModelAttribute ("WindowSize", TimeValue (Seconds (0.5)));
//    lteHelper->SetFadingModelAttribute ("RbNum", UintegerValue (100));

    //Set Antenna Type
    lteHelper->SetEnbAntennaModelType("ns3::IsotropicAntennaModel");
    lteHelper->SetUeAntennaModelType("ns3::IsotropicAntennaModel");

    //Set Scheduler Type
    lteHelper->SetAttribute("Scheduler", StringValue("ns3::RrFfMacScheduler"));
    lteHelper->SetSchedulerAttribute("HarqEnabled", BooleanValue(true));

    //Set mobility for eNB nodes and UE nodes
    MobilityHelper eNBmobility;
    Ptr<ListPositionAllocator> eNBposition_allocator = CreateObject<ListPositionAllocator> ();
    eNBposition_allocator->Add(eNBAPos);
    eNBposition_allocator->Add(eNBBPos);
    eNBmobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    eNBmobility.SetPositionAllocator(eNBposition_allocator);
    eNBmobility.Install(eNBANodes.Get(0));
    eNBmobility.Install(eNBBNodes.Get(0));

    // Install Mobility Model in UE
    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    ueMobility.Install(Tcp1ANodes); Tcp1ANodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Tcp1APos); Tcp1ANodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedA);
    ueMobility.Install(Tcp2ANodes); Tcp2ANodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Tcp2APos); Tcp2ANodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedA);
    ueMobility.Install(Quic1ANodes); Quic1ANodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Quic1APos); Quic1ANodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedA);
    ueMobility.Install(Quic2ANodes); Quic2ANodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Quic2APos); Quic2ANodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedA);
//    ueMobility.Install(UdpANodes); UdpANodes.Get(0)->GetObject<MobilityModel>()->SetPosition(UdpAPos); UdpANodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedA);



    ueMobility.Install(Tcp1BNodes); Tcp1BNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Tcp1BPos); Tcp1BNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedB);
    ueMobility.Install(Tcp2BNodes); Tcp2BNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Tcp2BPos); Tcp2BNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedB);
    ueMobility.Install(Quic1BNodes); Quic1BNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Quic1BPos); Quic1BNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedB);
    ueMobility.Install(Quic2BNodes); Quic2BNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Quic2BPos); Quic2BNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedB);
//    ueMobility.Install(UdpBNodes); UdpBNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Tcp1APos); UdpBNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(speedB);

    MobilityHelper PGWmobility;
    Ptr<ListPositionAllocator> PGWposition_allocator = CreateObject<ListPositionAllocator> ();
    PGWposition_allocator->Add(gwPos);
    PGWposition_allocator->Add(sgwPos);
    PGWmobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    PGWmobility.SetPositionAllocator(PGWposition_allocator);
    PGWmobility.Install(pgwNode);
    PGWmobility.Install(sgwNode);


    //Install LTE stack to UE and eNB nodes
    NetDeviceContainer eNBLteDevs = lteHelper->InstallEnbDevice(NodeContainer(eNBANodes, eNBBNodes, mideNb));
    NetDeviceContainer TcpALteDevs, QuicALteDevs, TcpBLteDevs, QuicBLteDevs;
    TcpALteDevs = lteHelper->InstallUeDevice(TcpAUeNodes);
    QuicALteDevs = lteHelper->InstallUeDevice(QuicAUeNodes);
    TcpBLteDevs = lteHelper->InstallUeDevice(TcpBUeNodes);
    QuicBLteDevs = lteHelper->InstallUeDevice(QuicBUeNodes);

    lteHelper->SetHandoverAlgorithmType ("ns3::A3RsrpHandoverAlgorithm");
    lteHelper->SetHandoverAlgorithmAttribute ("Hysteresis", DoubleValue (3.0));
    lteHelper->SetHandoverAlgorithmAttribute ("TimeToTrigger", TimeValue (MilliSeconds (256)));
    lteHelper->AddX2Interface (NodeContainer(eNBANodes, eNBBNodes, mideNb));

    //Create remote server and add IP stack
    QuicHelper stackQuic;
    stackQuic.InstallQuic(NodeContainer(TcpSrvNodes, QuicSrvNodes));

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

    //Create Error Model for Internet Link
    Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable> ();
    RateErrorModel error_model;
    error_model.SetRandomVariable (uv);
    error_model.SetUnit (RateErrorModel::ERROR_UNIT_PACKET);
    error_model.SetRate (error_p);

    // Create the TCP Internet link
    PointToPointHelper TcpremoteServerp2p;//point to point line between PGW and remote server
    TcpremoteServerp2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    TcpremoteServerp2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(12)));
    TcpremoteServerp2p.SetDeviceAttribute("Mtu", UintegerValue(2000));
    TcpremoteServerp2p.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (&error_model));

    NetDeviceContainer TcpinternetDevices;
    TcpinternetDevices = TcpremoteServerp2p.Install(pgwNode, TcpSrvNodes.Get(0));
    Ipv4AddressHelper Tcpipv4helper;
    Tcpipv4helper.SetBase("1.0.0.0", "255.255.255.252");
    Ipv4InterfaceContainer TcpinternetIpIfaces = Tcpipv4helper.Assign(TcpinternetDevices);
//    Ipv4Address TcpremoteServerAddr = TcpinternetIpIfaces.GetAddress (1);
    Ipv4Address TcppgwServerAddr = TcpinternetIpIfaces.GetAddress(0);

    Ipv4StaticRoutingHelper Tcpipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> TcpremoteServerStaticRouting = Tcpipv4RoutingHelper.GetStaticRouting (TcpSrvNodes.Get(0)->GetObject<Ipv4> ());
    TcpremoteServerStaticRouting->SetDefaultRoute(TcppgwServerAddr, 1);

    // Create the QUIC Internet link
    PointToPointHelper QuicremoteServerp2p;//point to point line between PGW and remote server
    QuicremoteServerp2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    QuicremoteServerp2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(12)));
    QuicremoteServerp2p.SetDeviceAttribute("Mtu", UintegerValue(2000));
    QuicremoteServerp2p.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (&error_model));

    NetDeviceContainer QuicinternetDevices;
    QuicinternetDevices = QuicremoteServerp2p.Install(pgwNode, QuicSrvNodes.Get(0));
    Ipv4AddressHelper Quicipv4helper;
    Quicipv4helper.SetBase("2.0.0.0", "255.255.255.252");
    Ipv4InterfaceContainer QuicinternetIpIfaces = Quicipv4helper.Assign(QuicinternetDevices);
//    Ipv4Address QuicremoteServerAddr = QuicinternetIpIfaces.GetAddress (1);
    Ipv4Address QuicpgwServerAddr = QuicinternetIpIfaces.GetAddress(0);

    Ipv4StaticRoutingHelper Quicipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> QuicremoteServerStaticRouting = Quicipv4RoutingHelper.GetStaticRouting (QuicSrvNodes.Get(0)->GetObject<Ipv4> ());
    QuicremoteServerStaticRouting->SetDefaultRoute(QuicpgwServerAddr, 1);

    // Install IP stack on UE

    stackQuic.InstallQuic(TcpUeNodes);
    stackQuic.InstallQuic(QuicUeNodes);
    Ipv4InterfaceContainer tcpAIpIfaces, tcpBIpIfaces, quicAIpIfaces, quicBIpIfaces;
    tcpAIpIfaces = epcHelper->AssignUeIpv4Address(NetDeviceContainer(TcpALteDevs));
    tcpBIpIfaces = epcHelper->AssignUeIpv4Address(NetDeviceContainer(TcpBLteDevs));
    quicAIpIfaces = epcHelper->AssignUeIpv4Address(NetDeviceContainer(QuicALteDevs));
    quicBIpIfaces = epcHelper->AssignUeIpv4Address(NetDeviceContainer(QuicBLteDevs));

    //Create Routing Information

    Ipv4StaticRoutingHelper ipv4RoutingHelper;

    for(uint32_t i = 0; i < TcpUeNodes.GetN(); ++i) {
        Ptr<Node> userNode = TcpUeNodes.Get(i);
        // Set the default gateway for the tcp UE
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(userNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    for(uint32_t i = 0; i < QuicUeNodes.GetN(); ++i) {
        Ptr<Node> userNode = QuicUeNodes.Get(i);
        // Set the default gateway for the quic UE
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(userNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    //Attach UE's to eNodeB
    lteHelper->Attach (TcpALteDevs, eNBLteDevs.Get (0));
    lteHelper->Attach (QuicALteDevs, eNBLteDevs.Get (0));
    lteHelper->Attach (TcpBLteDevs, eNBLteDevs.Get (1));
    lteHelper->Attach (QuicBLteDevs, eNBLteDevs.Get (1));



    for(uint16_t i = 0; i < ATcpFlowNum; i++) {
        //Create bulksend apps
        AddressValue remoteAddress (InetSocketAddress (tcpAIpIfaces.GetAddress (i, 0) /*receiver address (each UE)*/, dlPort_tcp));
        BulkSendHelper tcpHelper ("ns3::TcpSocketFactory", Address());
        tcpHelper.SetAttribute("Remote", remoteAddress);
        tcpHelper.SetAttribute("MaxBytes", UintegerValue(fileSize));
        tcpHelper.SetAttribute("SendSize", UintegerValue(packetSize));
        ApplicationContainer tcpApp = tcpHelper.Install(TcpSrvNodes);

        tcpApp.Get(0)->SetStartTime(Seconds(1.0 + (i*0.1)));
        tcpApp.Get(0)->SetStopTime(Seconds(duration-1.05));

        //Create Packet Sink on UE
        Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny() /*reveicer address (each UE)*/, dlPort_tcp));
        PacketSinkHelper dlPacketSinkHelper("ns3::TcpSocketFactory", sinkLocalAddress);
        dlPacketSinkHelper.SetAttribute ("Protocol", TypeIdValue (TcpSocketFactory::GetTypeId ()));
        ApplicationContainer sinkApp = dlPacketSinkHelper.Install(TcpUeNodes.Get(i));

        sinkApp.Get(0)->SetStartTime(Seconds(0.6));
        sinkApp.Get(0)->SetStopTime(Seconds(duration));
    }

    for(uint16_t i = 0; i < BTcpFlowNum; i++) {
        //Create bulksend apps
        AddressValue remoteAddress (InetSocketAddress (tcpBIpIfaces.GetAddress (i, 0) /*receiver address (each UE)*/, dlPort_tcp));
        BulkSendHelper tcpHelper ("ns3::TcpSocketFactory", Address());
        tcpHelper.SetAttribute("Remote", remoteAddress);
        tcpHelper.SetAttribute("MaxBytes", UintegerValue(fileSize));
        tcpHelper.SetAttribute("SendSize", UintegerValue(packetSize));
        ApplicationContainer tcpApp = tcpHelper.Install(TcpSrvNodes);

        tcpApp.Get(0)->SetStartTime(Seconds(1.0 + (i*0.1)));
        tcpApp.Get(0)->SetStopTime(Seconds(duration-1.05));

        //Create Packet Sink on UE
        Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny() /*reveicer address (each UE)*/, dlPort_tcp));
        PacketSinkHelper dlPacketSinkHelper("ns3::TcpSocketFactory", sinkLocalAddress);
        dlPacketSinkHelper.SetAttribute ("Protocol", TypeIdValue (TcpSocketFactory::GetTypeId ()));
        ApplicationContainer sinkApp = dlPacketSinkHelper.Install(TcpUeNodes.Get(2+i));

        sinkApp.Get(0)->SetStartTime(Seconds(0.6));
        sinkApp.Get(0)->SetStopTime(Seconds(duration));
    }

    for(uint16_t i = 0; i < AQuicFlowNum; i++) {

        AddressValue clientAddress (InetSocketAddress (quicAIpIfaces.GetAddress (i, 0) /*receiver address (each UE)*/, dlPort_quic));
        BulkSendHelper quicHelper ("ns3::QuicSocketFactory", Address ());
        quicHelper.SetAttribute ("Remote", clientAddress);
        quicHelper.SetAttribute("MaxBytes", UintegerValue(fileSize));
        quicHelper.SetAttribute("SendSize", UintegerValue(packetSize));
        ApplicationContainer quicApp = quicHelper.Install(QuicSrvNodes);

        quicApp.Get(0)->SetStartTime(Seconds(1.05 + (i*0.1)));
        quicApp.Get(0)->SetStopTime(Seconds(duration-1.0));

        //Create Packet Sink on UE
        Address sinkAddressAny (InetSocketAddress (Ipv4Address::GetAny () /*receiver address (each UE)*/, dlPort_quic));
        PacketSinkHelper dlPacketSinkHelper("ns3::QuicSocketFactory", sinkAddressAny);
        dlPacketSinkHelper.SetAttribute ("Protocol", TypeIdValue (QuicSocketFactory::GetTypeId ()));
        ApplicationContainer sinkApp = dlPacketSinkHelper.Install(QuicUeNodes.Get(i));

        sinkApp.Get(0)->SetStartTime(Seconds(0.6));
        sinkApp.Get(0)->SetStopTime(Seconds(duration));
    }

    for(uint16_t i = 0; i < BQuicFlowNum; i++) {

        AddressValue clientAddress (InetSocketAddress (quicBIpIfaces.GetAddress (i, 0) /*receiver address (each UE)*/, dlPort_quic));
        BulkSendHelper quicHelper ("ns3::QuicSocketFactory", Address ());
        quicHelper.SetAttribute ("Remote", clientAddress);
        quicHelper.SetAttribute("MaxBytes", UintegerValue(fileSize));
        quicHelper.SetAttribute("SendSize", UintegerValue(packetSize));
        ApplicationContainer quicApp = quicHelper.Install(QuicSrvNodes);

        quicApp.Get(0)->SetStartTime(Seconds(1.05 + (i*0.1)));
        quicApp.Get(0)->SetStopTime(Seconds(duration-1.0));

        //Create Packet Sink on UE
        Address sinkAddressAny (InetSocketAddress (Ipv4Address::GetAny () /*receiver address (each UE)*/, dlPort_quic));
        PacketSinkHelper dlPacketSinkHelper("ns3::QuicSocketFactory", sinkAddressAny);
        dlPacketSinkHelper.SetAttribute ("Protocol", TypeIdValue (QuicSocketFactory::GetTypeId ()));
        ApplicationContainer sinkApp = dlPacketSinkHelper.Install(QuicUeNodes.Get(2+i));

        sinkApp.Get(0)->SetStartTime(Seconds(0.6));
        sinkApp.Get(0)->SetStopTime(Seconds(duration));
    }

//    TcpremoteServerp2p.EnablePcapAll("test");

//    std::cout << "***Simulation is Starting***" << std::endl;

    Simulator::Stop(Seconds(duration+1));

    //NetAnim
//    AnimationInterface anim("automatic-handover.xml");

    NodeStatistics* nodeStat = new NodeStatistics(NodeContainer(TcpUeNodes, TcpSrvNodes.Get(0)),"test", false, 0);
    Simulator::Schedule(Seconds(1),
                        &NodeStatistics::AdvancePosition,
                        nodeStat,
                        1);





    Simulator::Run();
    Simulator::Destroy();

    return 0;
}

