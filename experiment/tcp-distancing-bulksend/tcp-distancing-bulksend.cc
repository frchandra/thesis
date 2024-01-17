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

#include "ns3/applications-module.h"
#include "ns3/boolean.h"
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/double.h"
#include "ns3/gnuplot.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/netanim-module.h"
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/ssid.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpDistancingBulksend");

/** Node statistics */
class NodeStatistics
{
  public:
    int stepItr = 0;
    /**
    * Constructor
    * \param aps AP devices
    * \param stas STA devices
     */
    NodeStatistics(NetDeviceContainer aps, NetDeviceContainer stas);

    /**
    * RX callback
    * \param path path
    * \param packet received packet
    * \param from sender
     */
    void RxCallback(std::string path, Ptr<const Packet> packet, const Address& from);
    /**
    * Set node position
    * \param node the node
    * \param position the position
     */
    void SetPosition(Ptr<Node> node, Vector position);
    /**
    * Advance node position
    * \param node the node
    * \param stepsSize the size of a step
    * \param stepsTime the time interval between steps
     */
    void AdvancePosition(Ptr<Node> node, int stepsSize, int stepsTime);
    /**
    * Get node position
    * \param node the node
    * \return the position
     */
    Vector GetPosition(Ptr<Node> node);
    /**
    * \return the gnuplot 2d dataset
     */
    Gnuplot2dDataset GetDatafile();

  private:
    uint32_t m_bytesTotal;     //!< total bytes
    Gnuplot2dDataset m_output; //!< gnuplot 2d dataset
};

NodeStatistics::NodeStatistics(NetDeviceContainer aps, NetDeviceContainer stas)
{
    m_bytesTotal = 0;
}

void
NodeStatistics::RxCallback(std::string path, Ptr<const Packet> packet, const Address& from)
{
    m_bytesTotal += packet->GetSize();
}

void
NodeStatistics::SetPosition(Ptr<Node> node, Vector position)
{
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    mobility->SetPosition(position);
}

Vector
NodeStatistics::GetPosition(Ptr<Node> node)
{
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    return mobility->GetPosition();
}

void
NodeStatistics::AdvancePosition(Ptr<Node> node, int stepsSize, int stepsTime)
{
    NS_LOG_INFO("STEP #" << stepItr);
    stepItr++;
    Vector pos = GetPosition(node);
    double mbs = ((m_bytesTotal * 8.0) / (1000000 * stepsTime));
    NS_LOG_INFO(" mbs : " << mbs);
    m_bytesTotal = 0;
    m_output.Add(pos.x, mbs);
    pos.x += stepsSize;
    SetPosition(node, pos);
    Simulator::Schedule(Seconds(stepsTime),
                        &NodeStatistics::AdvancePosition,
                        this,
                        node,
                        stepsSize,
                        stepsTime);
}

Gnuplot2dDataset
NodeStatistics::GetDatafile()
{
    return m_output;
}

int
main(){

    LogComponentEnable("TcpDistancingBulksend", LOG_LEVEL_INFO);

    std::string outputFileName = "tcp-distancing-bulksend";
    std::string transport_prot = "TcpNewReno";
    int ap1_x = 0;
    int ap1_y = 0;
    int sta1_x = 0;
    int sta1_y = 0;
    int steps = 5;
    int stepsSize = 1;
    int stepsTime = 1;
    int simuTime = steps * stepsTime;
    int maxBytes = 0;

    NodeContainer wifiApNodes;
    wifiApNodes.Create(1);
    NodeContainer wifiStaNodes;
    wifiStaNodes.Create(1);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(ap1_x, ap1_y, 0.0));
    positionAlloc->Add(Vector(sta1_x, sta1_y, 0.0));
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNodes.Get(0));
    mobility.Install(wifiStaNodes.Get(0));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    YansWifiPhyHelper wifiPhy;
    YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
    wifiPhy.SetChannel(wifiChannel.Create());
    //    wifiPhy.Set("ChannelSettings", StringValue("{0, 0, BAND_5GHZ, 0}"));
    wifiPhy.Set("ChannelSettings", StringValue("{0, 0, BAND_2_4GHZ, 0}"));

    NetDeviceContainer wifiApDevices;
    NetDeviceContainer wifiStaDevices;
    NetDeviceContainer wifiDevices;

    WifiMacHelper wifiMac;
    Ssid ssid = Ssid("AP");
    wifiMac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
    wifiStaDevices.Add(wifi.Install(wifiPhy, wifiMac, wifiStaNodes.Get(0)));
    ssid = Ssid("AP");
    wifiMac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    wifiApDevices.Add(wifi.Install(wifiPhy, wifiMac, wifiApNodes.Get(0)));
    wifiDevices.Add(wifiStaDevices); //0
    wifiDevices.Add(wifiApDevices); //1

    InternetStackHelper stack;
    stack.Install(wifiApNodes);
    stack.Install(wifiStaNodes);
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer itf = address.Assign(wifiDevices);
    Ipv4Address sinkAddress = itf.GetAddress(0); //sta
    uint16_t port = 9;

    transport_prot = std::string ("ns3::") + transport_prot;
    TypeId tcpTid;
    NS_ABORT_MSG_UNLESS (TypeId::LookupByNameFailSafe (transport_prot, &tcpTid), "TypeId " << transport_prot << " not found");
    Config::SetDefault ("ns3::QuicL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName (transport_prot)));

    PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(sinkAddress, port)); //at the sta
    ApplicationContainer apps_sink = sink.Install(wifiStaNodes.Get(0));

    BulkSendHelper bulksend("ns3::TcpSocketFactory", InetSocketAddress(sinkAddress /*dest address*/, port));
    bulksend.SetAttribute("MaxBytes", UintegerValue (maxBytes));
    bulksend.SetAttribute("StartTime", TimeValue(Seconds(0.5)));
    bulksend.SetAttribute("StopTime", TimeValue(Seconds(simuTime)));
    ApplicationContainer apps_source = bulksend.Install(wifiApNodes.Get(0)); //server

    apps_sink.Start(Seconds(0.5));
    apps_sink.Stop(Seconds(simuTime));

    NodeStatistics atpCounter = NodeStatistics(wifiApDevices, wifiStaDevices);
    Simulator::Schedule(Seconds(0.5 + stepsTime),
                        &NodeStatistics::AdvancePosition,
                        &atpCounter,
                        wifiStaNodes.Get(0),
                        stepsSize,
                        stepsTime);
    Config::Connect("/NodeList/1/ApplicationList/*/$ns3::PacketSink/Rx",
                    MakeCallback(&NodeStatistics::RxCallback, &atpCounter));

    AnimationInterface anim("wireless.xml");
    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
    {
        anim.UpdateNodeDescription(wifiStaNodes.Get(i), "STA"); // Optional
        anim.UpdateNodeColor(wifiStaNodes.Get(i), 255, 0, 0);   // Optional
    }
    for (uint32_t i = 0; i < wifiApNodes.GetN(); ++i)
    {
        anim.UpdateNodeDescription(wifiApNodes.Get(i), "AP"); // Optional
        anim.UpdateNodeColor(wifiApNodes.Get(i), 0, 255, 0);  // Optional
    }
    anim.EnablePacketMetadata(); // Optional
    anim.EnableIpv4RouteTracking("routingtable-wireless.xml",
                                 Seconds(0),
                                 Seconds(5),
                                 Seconds(0.25));         // Optional
    anim.EnableWifiMacCounters(Seconds(0), Seconds(10)); // Optional
    anim.EnableWifiPhyCounters(Seconds(0), Seconds(10)); // Optional

    Simulator::Stop(Seconds(simuTime));
    Simulator::Run();

    std::ofstream outfile("throughput-" + outputFileName + ".plt");
    Gnuplot gnuplot = Gnuplot("throughput-" + outputFileName + ".eps", "Throughput");
    gnuplot.SetTerminal("post eps color enhanced");
    gnuplot.SetLegend("Time (seconds)", "Throughput (Mb/s)");
    gnuplot.SetTitle("Throughput (AP to STA) vs time");
    gnuplot.AddDataset(atpCounter.GetDatafile());
    gnuplot.GenerateOutput(outfile);

    wifiPhy.EnablePcap("pcapfile", wifiStaDevices.Get(0), true, true);

    Simulator::Destroy();

    return 0;
}