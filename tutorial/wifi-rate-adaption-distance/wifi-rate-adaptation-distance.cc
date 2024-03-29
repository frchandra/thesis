/*
 * Copyright (c) 2014 Universidad de la República - Uruguay
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Matías Richart <mrichart@fing.edu.uy>
 */

/**
 * This example program is ns3 designed to illustrate the behavior of
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
 * The output consists of:
 * - A plot of average throughput vs. distance.
 * - (if logging is enabled) the changes of rate to standard output.
 *
 * Example usage:
 * ./ns3 run "wifi-rate-adaptation-distance --standard=802.11a --staManager=ns3::MinstrelWifiManager
 * --apManager=ns3::MinstrelWifiManager --outputFileName=minstrel"
 *
 * Another example (moving towards the AP):
 * ./ns3 run "wifi-rate-adaptation-distance --standard=802.11a --staManager=ns3::MinstrelWifiManager
 * --apManager=ns3::MinstrelWifiManager --outputFileName=minstrel --stepsSize=1 --STA1_x=-200"
 *
 * Example for HT rates with SGI and channel width of 40MHz:
 * ./ns3 run "wifi-rate-adaptation-distance --staManager=ns3::MinstrelHtWifiManager
 * --apManager=ns3::MinstrelHtWifiManager --outputFileName=minstrelHt --shortGuardInterval=true
 * --channelWidth=40"
 *
 * To enable the log of rate changes:
 * export NS_LOG=RateAdaptationDistance=level_info
 */

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
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/ssid.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/quic-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("RateAdaptationDistance");

/** Node statistics */
class NodeStatistics{
  public:
    int stepItr = 0;
    NodeStatistics(NetDeviceContainer aps, NetDeviceContainer stas);
    void RxCallback(std::string path, Ptr<const Packet> packet, const Address& from);
    void SetPosition(Ptr<Node> node, Vector position);
    void AdvancePosition(Ptr<Node> node, int stepsSize, int stepsTime);
    Vector GetPosition(Ptr<Node> node);
    Gnuplot2dDataset GetDatafile();
    void MonitorSnifferRxCallback(std::string context, Ptr< const Packet > packet, uint16_t channelFreqMhz, WifiTxVector txVector, MpduInfo aMpdu, SignalNoiseDbm signalNoise, uint16_t staId);
  private:
    uint32_t m_bytesTotal;     //!< total bytes
    Gnuplot2dDataset m_output; //!< gnuplot 2d dataset
};

NodeStatistics::NodeStatistics(NetDeviceContainer aps, NetDeviceContainer stas){
    m_bytesTotal = 0;
}

void NodeStatistics::RxCallback(std::string path, Ptr<const Packet> packet, const Address& from){
    m_bytesTotal += packet->GetSize();
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

void NodeStatistics::MonitorSnifferRxCallback(std::string context, Ptr<const ns3::Packet> packet, uint16_t channelFreqMhz, ns3::WifiTxVector txVector, ns3::MpduInfo aMpdu, ns3::SignalNoiseDbm signalNoise, uint16_t staId){
    NS_LOG_UNCOND("signalNoise.noise: " << signalNoise.noise << "signalNoise.signal" << signalNoise.signal);
}

Gnuplot2dDataset NodeStatistics::GetDatafile(){
    return m_output;
}

/**
 * Callback for 'Rate' trace source
 *
 * \param oldRate old MCS rate (bits/sec)
 * \param newRate new MCS rate (bits/sec)
 */
//void
//RateCallback(uint64_t oldRate, uint64_t newRate)
//{
//    NS_LOG_INFO("Rate " << newRate / 1000000.0 << " Mbps");
//}

int
main(int argc, char* argv[])
{
    LogComponentEnable("RateAdaptationDistance", LOG_LEVEL_INFO);

    std::string outputFileName = "minstrelHTSA";
    int ap1_x = 0;
    int ap1_y = 0;
    int sta1_x = 5;
    int sta1_y = 0;
    int steps = 10;
    int stepsSize = 10;
    int stepsTime = 1;
    int simuTime = steps * stepsTime;

    // Define the APs
    NodeContainer wifiApNodes;
    wifiApNodes.Create(1);

    // Define the STAs
    NodeContainer wifiStaNodes;
    wifiStaNodes.Create(1);

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

    wifiDevices.Add(wifiStaDevices);
    wifiDevices.Add(wifiApDevices);

    // Configure the mobility.
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    // Initial position of AP and STA
    positionAlloc->Add(Vector(ap1_x, ap1_y, 0.0));
    positionAlloc->Add(Vector(sta1_x, sta1_y, 0.0));
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNodes.Get(0));
    mobility.Install(wifiStaNodes.Get(0));

    // Statistics counter
    NodeStatistics atpCounter = NodeStatistics(wifiApDevices, wifiStaDevices);

    // Move the STA by stepsSize meters every stepsTime seconds
    Simulator::Schedule(Seconds(0.5 + stepsTime),
                        &NodeStatistics::AdvancePosition,
                        &atpCounter,
                        wifiStaNodes.Get(0),
                        stepsSize,
                        stepsTime);

    // Configure the IP stack
    InternetStackHelper stack;
    stack.Install(wifiApNodes);
    stack.Install(wifiStaNodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i = address.Assign(wifiDevices);
    Ipv4Address sinkAddress = i.GetAddress(0);
    uint16_t port = 9;

    // Configure the CBR generator
    PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(sinkAddress, port));
    ApplicationContainer apps_sink = sink.Install(wifiStaNodes.Get(0));

    OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(sinkAddress, port));
    onoff.SetConstantRate(DataRate("400Mb/s"), 1420);
    onoff.SetAttribute("StartTime", TimeValue(Seconds(0.5)));
    onoff.SetAttribute("StopTime", TimeValue(Seconds(simuTime)));
    ApplicationContainer apps_source = onoff.Install(wifiApNodes.Get(0));

    apps_sink.Start(Seconds(0.5));
    apps_sink.Stop(Seconds(simuTime));

    //------------------------------------------------------------
    //-- Setup stats and data collection
    //--------------------------------------------

    // Register packet receptions to calculate throughput
    Config::Connect("/NodeList/1/ApplicationList/*/$ns3::PacketSink/Rx",
                    MakeCallback(&NodeStatistics::RxCallback, &atpCounter));

    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferRx",
                    MakeCallback(&NodeStatistics::MonitorSnifferRxCallback, &atpCounter));

    Simulator::Stop(Seconds(simuTime));
    Simulator::Run();

    std::ofstream outfile("throughput-" + outputFileName + ".plt");
    Gnuplot gnuplot = Gnuplot("throughput-" + outputFileName + ".eps", "Throughput");
    gnuplot.SetTerminal("post eps color enhanced");
    gnuplot.SetLegend("Time (seconds)", "Throughput (Mb/s)");
    gnuplot.SetTitle("Throughput (AP to STA) vs time");
    gnuplot.AddDataset(atpCounter.GetDatafile());
    gnuplot.GenerateOutput(outfile);

    Simulator::Destroy();

    return 0;
}
