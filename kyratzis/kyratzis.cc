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

NS_LOG_COMPONENT_DEFINE ("Simulation_1");


static Ptr<OutputStreamWrapper> TcpcWndStream;
static Ptr<OutputStreamWrapper> TcprttStream;

static void
CwndTracer (uint32_t oldval, uint32_t newval)
{
    *TcpcWndStream->GetStream () << Simulator::Now ().GetSeconds () << "," << oldval << "," << newval << std::endl;
}


static void
RttTracer (Time oldval, Time newval)
{
    *TcprttStream->GetStream () << Simulator::Now ().GetSeconds () << "," << oldval.GetSeconds () << "," << newval.GetSeconds () << std::endl;
}

static void
TraceCwnd (std::string cwnd_tr_file_name)
{
    AsciiTraceHelper ascii;
    TcpcWndStream = ascii.CreateFileStream (cwnd_tr_file_name.c_str ());
    Config::ConnectWithoutContext ("/NodeList/6/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&CwndTracer));

}

static void
TraceRtt (std::string rtt_tr_file_name)
{
    AsciiTraceHelper ascii;
    TcprttStream = ascii.CreateFileStream (rtt_tr_file_name.c_str ());
    Config::ConnectWithoutContext ("/NodeList/6/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeCallback (&RttTracer));
}



// connect to a number of traces

static void
CwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
    *stream->GetStream () << Simulator::Now ().GetSeconds () << "," << oldCwnd << "," << newCwnd << std::endl;
}

static void
RttChange (Ptr<OutputStreamWrapper> stream, Time oldRtt, Time newRtt)
{
    *stream->GetStream () << Simulator::Now ().GetSeconds () << "," << oldRtt.GetSeconds () << "," << newRtt.GetSeconds () << std::endl;
}

static void
Rx (Ptr<OutputStreamWrapper> stream, Ptr<const Packet> p, const QuicHeader& q, Ptr<const QuicSocketBase> qsb)
{
    *stream->GetStream () << Simulator::Now ().GetSeconds () << "," << p->GetSize() << std::endl;
}

static void
QuicTraces(uint32_t serverId, std::string pathVersion, std::string finalPart)
{
    AsciiTraceHelper asciiTraceHelper;

    std::ostringstream pathCW;
    pathCW << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/CongestionWindow";
    NS_LOG_INFO("Matches cw " << Config::LookupMatches(pathCW.str().c_str()).GetN());

    std::ostringstream fileCW;
    fileCW << pathVersion << "QUIC-cwnd-change"  << serverId << "" << finalPart;

    std::ostringstream pathRTT;
    pathRTT << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/RTT";

    std::ostringstream fileRTT;
    fileRTT << pathVersion << "QUIC-rtt-change"  << serverId << "" << finalPart;

    std::ostringstream pathRCWnd;
    pathRCWnd << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/RWND";

    std::ostringstream fileRCWnd;
    fileRCWnd <<pathVersion << "QUIC-rwnd-change"  << serverId << "" << finalPart;

    std::ostringstream fileName;
    fileName << pathVersion << "QUIC-rx-data" << serverId << "" << finalPart;

    std::ostringstream pathRx;
    pathRx << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Rx";

    NS_LOG_INFO("Matches rx " << Config::LookupMatches(pathRx.str().c_str()).GetN());

    Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream (fileName.str ().c_str ());
    Config::ConnectWithoutContext (pathRx.str ().c_str (), MakeBoundCallback (&Rx, stream));

    Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream (fileCW.str ().c_str ());
    Config::ConnectWithoutContext (pathCW.str ().c_str (), MakeBoundCallback(&CwndChange, stream1));

    Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
    Config::ConnectWithoutContext (pathRTT.str ().c_str (), MakeBoundCallback(&RttChange, stream2));

    Ptr<OutputStreamWrapper> stream4 = asciiTraceHelper.CreateFileStream (fileRCWnd.str ().c_str ());
    Config::ConnectWithoutContext (pathRCWnd.str ().c_str (), MakeBoundCallback(&CwndChange, stream4));
}

bool isClient(Ipv4Address ipAddress, Ipv4InterfaceContainer containerAddress) {
    for(uint32_t i = 0; i < containerAddress.GetN(); ++i) {
        Ipv4Address clientAddress = containerAddress.Get(i).first->GetAddress(1, 0).GetLocal();
        if(clientAddress == ipAddress) {
            return true;
        }
    }

    return false;
}

//Define function for Throughput Calculation every second

void ThroughputMonitor (FlowMonitorHelper *fmhelper, Ptr<FlowMonitor> flowMon, Ipv4InterfaceContainer ipIpIfaces, Ptr<OutputStreamWrapper> temp) {


    *temp->GetStream () << Simulator::Now ().GetSeconds ();

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (fmhelper->GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMon->GetFlowStats();

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin (); iter != stats.end (); ++iter) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (iter->first);

        if (isClient(t.destinationAddress, ipIpIfaces)) {

            *temp->GetStream () << "," << ((iter->second.rxBytes * 8.0) / (iter->second.timeLastRxPacket.GetSeconds() - iter->second.timeFirstTxPacket.GetSeconds()) / 1000000);
        }
    }

    *temp->GetStream() << std::endl;

    Simulator::Schedule(Seconds(1.0), &ThroughputMonitor, fmhelper, flowMon, ipIpIfaces, temp);
}


void printCurrentTime() {

    std::cout << Simulator::Now().GetSeconds() << "\n";
}


int main(int argc, char *argv[]) {


    uint16_t tcp_flows = 1, quic_flows = 1;
    uint16_t Cells = 1;
    double distance = -3000;
    double error_p = 0.0;
    double duration = 12.0;
    double fileSize = 0;
    double packetSize = 1024;
//    double numStreams = 4;
    double UE_radius = 50; //meters
    uint64_t buffSize = 524288;
    bool use_0RTT = false;
    bool use_2RTT = true;
    bool KPI_tracing = false;
    bool Throughput_Trace = true;
    std::string tcp_congestion_op = "TcpNewReno";

    Vector eNBposition = Vector(distance, 0, 15);
    Vector PGWposition = Vector(0, 0, 0);
    Vector TcpserverPosition = Vector(5000, -1000, 0);
    Vector QuicserverPosition = Vector(5000, 1000, 0);

    // Parse command line attribute
    CommandLine cmd;

    cmd.AddValue ("duration", "The duration of the simulation", duration);
    cmd.AddValue ("tcp_flows", "The number of TCP clients", tcp_flows);
    cmd.AddValue ("quic_flows", "The number of QUIC clients", quic_flows);
    cmd.AddValue ("fileSize", "The number of bytes to send", fileSize);
    cmd.AddValue ("packetSize", "The size of packets in bytes", packetSize);
    cmd.AddValue ("buffSize", "The buffer size of TCP/QUIC sockets", buffSize);
    cmd.AddValue ("tcp_congestion_op", "The TCP congestion algorithm", tcp_congestion_op);
    cmd.AddValue ("error_p", "The packet error probability on PGW-server link", error_p);
    cmd.AddValue ("UE_radius", "The distance between UE and eNodeB", UE_radius);
    cmd.AddValue ("use_0RTT", "Enable 0-RTT Handshake", use_0RTT);
    cmd.AddValue ("use_2RTT", "Enable 2-RTT Handshake", use_2RTT);
    cmd.AddValue ("KPI_tracing", "Enable the CWND and RTT tracing", KPI_tracing);
    cmd.AddValue ("Throughput_Trace", "Enable the throughput tracing", Throughput_Trace);

    cmd.Parse(argc, argv);

    LogComponentEnable("Simulation_1", LOG_LEVEL_ALL);

    tcp_congestion_op = std::string ("ns3::") + tcp_congestion_op;

    LogComponentEnableAll (LOG_PREFIX_TIME);
    LogComponentEnableAll (LOG_PREFIX_FUNC);
    LogComponentEnableAll (LOG_PREFIX_NODE);

    LogComponentEnable ("QuicSocketBase", LOG_LEVEL_INFO);
    LogComponentEnable ("TcpSocketBase", LOG_LEVEL_INFO);

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

    // Select congestion control variant
    if (tcp_congestion_op.compare ("ns3::TcpWestwoodPlus") == 0) {

        // TcpWestwoodPlus is not an actual TypeId name; we need TcpWestwood here
        // the default protocol type in ns3::TcpWestwood is WESTWOOD
//        Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOODPLUS));
//        Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpWestwood::GetTypeId ()));
        Config::SetDefault ("ns3::QuicL4Protocol::SocketType", TypeIdValue(QuicCongestionOps::GetTypeId()));
    }
    else {
        Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName (tcp_congestion_op)));
        Config::SetDefault ("ns3::QuicL4Protocol::SocketType", TypeIdValue(QuicCongestionOps::GetTypeId()));
    }


    //Create UE and eNB nodes
    NodeContainer tcpNodes, quicNodes, enBNodes;
    tcpNodes.Create(tcp_flows);
    quicNodes.Create(quic_flows);
    enBNodes.Create(Cells);

    //Create LTE and EPC
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();

    Ptr<PointToPointEpcHelper>  epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);
    Ptr<Node> pgwNode = epcHelper->GetPgwNode(); //P-GW node

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
//    lteHelper->SetFadingModelAttribute("TraceFilename", StringValue("/home/ns3/Desktop/quic-ns-3/scratch/fading_trace_modified_40s_EPA_3kmph.fad"));
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
    eNBposition_allocator->Add(eNBposition);
    eNBmobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    eNBmobility.SetPositionAllocator(eNBposition_allocator);
    eNBmobility.Install(enBNodes.Get(0));

    Ptr<ConstantRandomVariable> rho = CreateObject<ConstantRandomVariable>();
    rho->SetAttribute ("Constant", DoubleValue (UE_radius));

    Ptr<UniformRandomVariable> theta = CreateObject<UniformRandomVariable>();
    theta->SetAttribute ("Min", DoubleValue (0));
    theta->SetAttribute ("Max", DoubleValue (6.2830));

    MobilityHelper UEmobility;
    Ptr<RandomDiscPositionAllocator> UEposition_allocator = CreateObject<RandomDiscPositionAllocator> ();
    UEposition_allocator->SetRho(rho);
    UEposition_allocator->SetTheta(theta);
    UEposition_allocator->SetX(eNBposition.x);
    UEposition_allocator->SetY(eNBposition.y);
    UEmobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    UEmobility.SetPositionAllocator(UEposition_allocator);
    UEmobility.Install(tcpNodes);
    UEmobility.Install(quicNodes);

    MobilityHelper PGWmobility;
    Ptr<ListPositionAllocator> PGWposition_allocator = CreateObject<ListPositionAllocator> ();
    PGWposition_allocator->Add(PGWposition);
    PGWmobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    PGWmobility.SetPositionAllocator(PGWposition_allocator);
    PGWmobility.Install(pgwNode);

    //Install LTE stack to UE and eNB nodes
    NetDeviceContainer eNBLteDevs = lteHelper->InstallEnbDevice(enBNodes);
    NetDeviceContainer tcpLteDevs, quicLteDevs;
    tcpLteDevs = lteHelper->InstallUeDevice(tcpNodes);
    quicLteDevs = lteHelper->InstallUeDevice(quicNodes);

    //Create remote server and add IP stack
    NodeContainer remoteServerContainer;
    remoteServerContainer.Create(2);
    Ptr<Node> TcpremoteServer = remoteServerContainer.Get(0);
    Ptr<Node> QuicremoteServer = remoteServerContainer.Get(1);
    QuicHelper stackQuic;
    stackQuic.InstallQuic(remoteServerContainer);

    //Set mobility for TCP server node
    MobilityHelper Tcpservermobility;
    Ptr<ListPositionAllocator> TcpserverPosition_allocator = CreateObject<ListPositionAllocator> ();
    TcpserverPosition_allocator->Add(TcpserverPosition);
    Tcpservermobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Tcpservermobility.SetPositionAllocator(TcpserverPosition_allocator);
    Tcpservermobility.Install(TcpremoteServer);

    //Set mobility for QUIC server node
    MobilityHelper Quicservermobility;
    Ptr<ListPositionAllocator> QuicserverPosition_allocator = CreateObject<ListPositionAllocator> ();
    QuicserverPosition_allocator->Add(QuicserverPosition);
    Quicservermobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Quicservermobility.SetPositionAllocator(QuicserverPosition_allocator);
    Quicservermobility.Install(QuicremoteServer);

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
    TcpinternetDevices = TcpremoteServerp2p.Install(pgwNode, TcpremoteServer);
    Ipv4AddressHelper Tcpipv4helper;
    Tcpipv4helper.SetBase("1.0.0.0", "255.255.255.252");
    Ipv4InterfaceContainer TcpinternetIpIfaces = Tcpipv4helper.Assign(TcpinternetDevices);
//    Ipv4Address TcpremoteServerAddr = TcpinternetIpIfaces.GetAddress (1);
    Ipv4Address TcppgwServerAddr = TcpinternetIpIfaces.GetAddress(0);

    Ipv4StaticRoutingHelper Tcpipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> TcpremoteServerStaticRouting = Tcpipv4RoutingHelper.GetStaticRouting (TcpremoteServer->GetObject<Ipv4> ());
    TcpremoteServerStaticRouting->SetDefaultRoute(TcppgwServerAddr, 1);

    // Create the QUIC Internet link
    PointToPointHelper QuicremoteServerp2p;//point to point line between PGW and remote server
    QuicremoteServerp2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    QuicremoteServerp2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(12)));
    QuicremoteServerp2p.SetDeviceAttribute("Mtu", UintegerValue(2000));
    QuicremoteServerp2p.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (&error_model));

    NetDeviceContainer QuicinternetDevices;
    QuicinternetDevices = QuicremoteServerp2p.Install(pgwNode, QuicremoteServer);
    Ipv4AddressHelper Quicipv4helper;
    Quicipv4helper.SetBase("2.0.0.0", "255.255.255.252");
    Ipv4InterfaceContainer QuicinternetIpIfaces = Quicipv4helper.Assign(QuicinternetDevices);
//    Ipv4Address QuicremoteServerAddr = QuicinternetIpIfaces.GetAddress (1);
    Ipv4Address QuicpgwServerAddr = QuicinternetIpIfaces.GetAddress(0);

    Ipv4StaticRoutingHelper Quicipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> QuicremoteServerStaticRouting = Quicipv4RoutingHelper.GetStaticRouting (QuicremoteServer->GetObject<Ipv4> ());
    QuicremoteServerStaticRouting->SetDefaultRoute(QuicpgwServerAddr, 1);

    // Install IP stack on UE

    stackQuic.InstallQuic(tcpNodes);
    stackQuic.InstallQuic(quicNodes);
    Ipv4InterfaceContainer tcpIpIfaces, quicIpIfaces;
    tcpIpIfaces = epcHelper->AssignUeIpv4Address(NetDeviceContainer(tcpLteDevs));
    quicIpIfaces = epcHelper->AssignUeIpv4Address(NetDeviceContainer(quicLteDevs));

    //Create Routing Information

    Ipv4StaticRoutingHelper ipv4RoutingHelper;

    for(uint32_t i = 0; i < tcpNodes.GetN(); ++i) {
        Ptr<Node> userNode = tcpNodes.Get(i);
        // Set the default gateway for the tcp UE
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(userNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    for(uint32_t i = 0; i < quicNodes.GetN(); ++i) {
        Ptr<Node> userNode = quicNodes.Get(i);
        // Set the default gateway for the quic UE
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(userNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    //Attach UE's to eNodeB
    lteHelper->Attach (tcpLteDevs, eNBLteDevs.Get (0));
    lteHelper->Attach (quicLteDevs, eNBLteDevs.Get (0));

    //Create Random Variables for Application Start Time
    double min = 1.0;
    double max = 3.0;
    Ptr<UniformRandomVariable> startTimes = CreateObject<UniformRandomVariable> ();
    startTimes->SetAttribute ("Min", DoubleValue (min));
    startTimes->SetAttribute ("Max", DoubleValue (max));

    //Create BulkSend application for both TCP and QUIC clients

    uint16_t dlPort_tcp = 1000;
    uint16_t dlPort_quic = 2000;

    for(uint16_t i = 0; i < tcpNodes.GetN(); i++) {

        //Create bulksend apps

        AddressValue remoteAddress (InetSocketAddress (tcpIpIfaces.GetAddress (i, 0), dlPort_tcp));
        BulkSendHelper tcpHelper ("ns3::TcpSocketFactory", Address());
        tcpHelper.SetAttribute("Remote", remoteAddress);
        tcpHelper.SetAttribute("MaxBytes", UintegerValue(fileSize));
        tcpHelper.SetAttribute("SendSize", UintegerValue(packetSize));
        ApplicationContainer tcpApp = tcpHelper.Install(TcpremoteServer);

        tcpApp.Get(0)->SetStartTime(Seconds(1.0 + (i*0.1)));
        tcpApp.Get(0)->SetStopTime(Seconds(duration-1.05));

        //Create Packet Sink on UE
        Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny(), dlPort_tcp));
        PacketSinkHelper dlPacketSinkHelper("ns3::TcpSocketFactory", sinkLocalAddress);
        dlPacketSinkHelper.SetAttribute ("Protocol", TypeIdValue (TcpSocketFactory::GetTypeId ()));
        ApplicationContainer sinkApp = dlPacketSinkHelper.Install(tcpNodes.Get(i));

        sinkApp.Get(0)->SetStartTime(Seconds(0.6));
        sinkApp.Get(0)->SetStopTime(Seconds(duration));
    }



    for(uint16_t i = 0; i < quicNodes.GetN(); i++) {

        AddressValue clientAddress (InetSocketAddress (quicIpIfaces.GetAddress (i, 0), dlPort_quic));
        BulkSendHelper quicHelper ("ns3::QuicSocketFactory", Address ());
        quicHelper.SetAttribute ("Remote", clientAddress);
        quicHelper.SetAttribute("MaxBytes", UintegerValue(fileSize));
        quicHelper.SetAttribute("SendSize", UintegerValue(packetSize));
        ApplicationContainer quicApp = quicHelper.Install(QuicremoteServer);

        quicApp.Get(0)->SetStartTime(Seconds(1.05 + (i*0.1)));
        quicApp.Get(0)->SetStopTime(Seconds(duration-1.0));

        //Create Packet Sink on UE
        Address sinkAddressAny (InetSocketAddress (Ipv4Address::GetAny (), dlPort_quic));
        PacketSinkHelper dlPacketSinkHelper("ns3::QuicSocketFactory", sinkAddressAny);
        dlPacketSinkHelper.SetAttribute ("Protocol", TypeIdValue (QuicSocketFactory::GetTypeId ()));
        ApplicationContainer sinkApp = dlPacketSinkHelper.Install(quicNodes.Get(i));

        sinkApp.Get(0)->SetStartTime(Seconds(0.6));
        sinkApp.Get(0)->SetStopTime(Seconds(duration));
    }

    //Schedule Time Print

    for(int i = 1; i < duration; ++i) {
        Simulator::Schedule(Seconds(i), &printCurrentTime);
    }

    //Schedule Traces

    if (KPI_tracing)
    {
        for (uint16_t i = 0; i < tcp_flows; i++)
        {
            Ptr<Node> tcpClient = tcpNodes.Get (i);
            Ptr<Node> server = remoteServerContainer.Get (0); //tcpServer
            Time t = Seconds(1.0 + (i*0.1) + 0.00001);

            Simulator::Schedule (t, &TraceCwnd, "senderTcp-cwnd-change.csv");
            Simulator::Schedule (t, &TraceRtt, "senderTcp-rtt-change.csv");
        }


        for (uint16_t i = 0; i < quic_flows; i++)
        {
            Ptr<Node> quicClient = quicNodes.Get (i);
            Ptr<Node> server = remoteServerContainer.Get (1); //quicServer
            Time t = Seconds(1.05 + (i*0.1) + 0.00001);
            Simulator::Schedule (t, &QuicTraces, server->GetId(), "sender", ".csv");
        }
    }




    //Configure Flow Monitor
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();
    flowMonitor->Start(Seconds(0.0));

    if (Throughput_Trace)
    {
        //Create files for Throughput Calculations
        AsciiTraceHelper asciiHelper;

        std::ostringstream tcpThroughput;
        std::ostringstream quicThroughput;

        tcpThroughput << "Tcp_Throughput_Calculations.csv";
        quicThroughput << "Quic_Throughput_Calculations.csv";

        Ptr<OutputStreamWrapper> streamThroughput_1 = asciiHelper.CreateFileStream (tcpThroughput.str ().c_str ());
        Ptr<OutputStreamWrapper> streamThroughput_2 = asciiHelper.CreateFileStream (quicThroughput.str ().c_str ());

        //Schedule Flow Monitor every second
        Simulator::Schedule(Seconds(0.50), &ThroughputMonitor, &flowHelper, flowMonitor, tcpIpIfaces, streamThroughput_1);
        Simulator::Schedule(Seconds(0.51), &ThroughputMonitor, &flowHelper, flowMonitor, quicIpIfaces, streamThroughput_2);

    }


    //Enable Pcap tracing for server
    TcpremoteServerp2p.EnablePcap("trace", remoteServerContainer, BooleanValue(false));

    lteHelper->EnablePhyTraces ();
    lteHelper->EnableMacTraces ();
    lteHelper->EnableRlcTraces ();
    lteHelper->EnablePdcpTraces ();
    Ptr<RadioBearerStatsCalculator> rlcStats = lteHelper->GetRlcStats ();
    rlcStats->SetAttribute ("EpochDuration", TimeValue (Seconds (1.0)));
    Ptr<RadioBearerStatsCalculator> pdcpStats = lteHelper->GetPdcpStats ();
    pdcpStats->SetAttribute ("EpochDuration", TimeValue (Seconds (1.0)));

    std::cout << "***Simulation is Starting***" << std::endl;

    Simulator::Stop(Seconds(duration));
    Simulator::Run();

    flowMonitor->SerializeToXmlFile("simulation-flow.xml", true, true);

    //Calculate Final Results
    double dlThroughput_tcp = 0, dlThroughput_quic = 0;
    double downloadTime_tcp = 0, downloadTime_quic = 0;

    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();

    for(std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin(); iter != stats.end(); ++iter) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter->first);

        if(t.destinationPort == dlPort_tcp) {
            //dlThroughput_tcp += (fileSize*8.0)/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds()) / 1000000;
            dlThroughput_tcp += iter->second.rxBytes * 8.0 /(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds()) / 1000000;
            downloadTime_tcp += iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds();
        }


        if(t.destinationPort == dlPort_quic) {
            //dlThroughput_quic += (fileSize*8.0)/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds()) / 1000000;
            dlThroughput_quic += iter->second.rxBytes * 8.0 /(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds()) / 1000000;
            downloadTime_quic += iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds();
        }
    }

    dlThroughput_tcp /= tcp_flows;
    dlThroughput_quic /= quic_flows;

    downloadTime_tcp /= tcp_flows;
    downloadTime_quic /= quic_flows;

    std::cout << "***Results***\n";
    std::cout << "Average DL throughput for TCP clients: " << dlThroughput_tcp << " Mbps\n";
    std::cout << "Average DL throughput for QUIC clients: " << dlThroughput_quic << " Mbps\n";
    std::cout << "Average Download Time for TCP clients : " << downloadTime_tcp << " s\n";
    std::cout << "Average Download Time for QUIC clients : " << downloadTime_quic << " s\n";
    Simulator::Destroy();


    return 0;



}
