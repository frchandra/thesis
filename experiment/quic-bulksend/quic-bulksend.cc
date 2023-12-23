/*
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
 */


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

NS_LOG_COMPONENT_DEFINE("QuicBulkSendExample");

int main(){
//    bool tracing = true;
    uint32_t maxBytes = 0;
//    uint32_t QUICFlows = 1;
//    uint32_t maxPackets = 0;

    NodeContainer nodes;
    nodes.Create (2);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("20Mbps"));
    pointToPoint.SetChannelAttribute ("Delay", StringValue ("10ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install (nodes);

    QuicHelper stack;
    stack.InstallQuic (nodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase ("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i = ipv4.Assign (devices);

    ApplicationContainer sourceApps;
    ApplicationContainer sinkApps;

    uint16_t port = 10000;

    BulkSendHelper source  ("ns3::QuicSocketFactory",InetSocketAddress (i.GetAddress (1), port));
    source.SetAttribute ("MaxBytes", UintegerValue (maxBytes)); // Set the amount of data to send in bytes.  Zero is unlimited.
    sourceApps.Add (source.Install (nodes.Get (0)));

    PacketSinkHelper sink ("ns3::QuicSocketFactory",InetSocketAddress (Ipv4Address::GetAny (), port));
    sinkApps.Add (sink.Install (nodes.Get (1)));






    sinkApps.Start (Seconds (0.0));
    sinkApps.Stop (Seconds (9));
    sourceApps.Start (Seconds (1));
    sourceApps.Stop (Seconds (9));

    pointToPoint.EnablePcapAll ("quic-bulksend", false);


    Simulator::Stop (Seconds (10));
    Simulator::Run ();
    Simulator::Destroy ();
}
