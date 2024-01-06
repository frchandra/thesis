#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("tls");

enum ConnectionState {
    INIT,
    HANDSHAKE_SENT,
    HANDSHAKE_RECEIVED,
    CONNECTED,
    DATA_EXCHANGED,
    DISCONNECTED
};

ConnectionState clientState = INIT;
ConnectionState serverState = INIT;

void HandshakeCallback(Ptr<Socket> socket, const Address& address) {
    if (socket->GetNode()->GetId() == 0) { // Client side
        if (clientState == HANDSHAKE_SENT) {
            NS_LOG_INFO("Client: TLS Handshake completed successfully.");
            clientState = CONNECTED;
            socket->Send(Create<Packet>("Hello from Client!"));
        }
    } else if (socket->GetNode()->GetId() == 1) { // Server side
        if (serverState == HANDSHAKE_RECEIVED) {
            NS_LOG_INFO("Server: TLS Handshake completed successfully.");
            serverState = CONNECTED;
        }
    }
}

void ReceiveDataCallback(Ptr<Socket> socket) {
    Ptr<Packet> packet;
    while ((packet = socket->Recv())) {
        if (socket->GetNode()->GetId() == 0) { // Client side
            NS_LOG_INFO("Client: Received data: " << packet->ToString());
            clientState = DATA_EXCHANGED;
        } else if (socket->GetNode()->GetId() == 1) { // Server side
            NS_LOG_INFO("Server: Received data: " << packet->ToString());
        }
    }
}

void ConnectionSucceededCallback(Ptr<Socket> socket) {
    if (socket->GetNode()->GetId() == 0) { // Client side
        if (clientState == CONNECTED) {
            NS_LOG_INFO("Client: Connection succeeded.");
        }
    } else if (socket->GetNode()->GetId() == 1) { // Server side
        if (serverState == CONNECTED) {
            NS_LOG_INFO("Server: Connection succeeded.");
        }
    }
}

void ConnectionFailedCallback(Ptr<Socket> socket) {
    if (socket->GetNode()->GetId() == 0) { // Client side
        NS_LOG_INFO("Client: Connection failed.");
    } else if (socket->GetNode()->GetId() == 1) { // Server side
        NS_LOG_INFO("Server: Connection failed.");
    }
}

int main() {
    // Initialize NS-3
    LogComponentEnable("TcpTlsHandshakeExample", LOG_LEVEL_INFO);

    // Create nodes
    NodeContainer nodes;
    nodes.Create(2);

    // Set up point-to-point link
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer devices = pointToPoint.Install(nodes);

    // Set up TCP/IP stack
    InternetStackHelper internet;
    internet.Install(nodes);

    // Set up TCP connections
    Ptr<Socket> serverSocket = Socket::CreateSocket(nodes.Get(1), TcpSocketFactory::GetTypeId());
    Ptr<Socket> clientSocket = Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());

    // Bind the server socket to an address
    InetSocketAddress serverAddress = InetSocketAddress(Ipv4Address::GetAny(), 9);
    serverSocket->Bind(serverAddress);
    serverSocket->Listen();

    // Register callbacks for TLS-like handshake completion, data reception, and connection status
    serverSocket->SetAcceptCallback(MakeNullCallback<void, Ptr<Socket>, const Address&>(),
                                    MakeCallback(&HandshakeCallback));
    clientSocket->SetRecvCallback(MakeCallback(&ReceiveDataCallback));
    clientSocket->SetConnectCallback(MakeCallback(&ConnectionSucceededCallback),
                                     MakeCallback(&ConnectionFailedCallback));

    // Initiate connection from the client
    InetSocketAddress clientAddress = InetSocketAddress(nodes.Get(1)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal(), 9);
    clientSocket->Connect(clientAddress);
    clientState = HANDSHAKE_SENT;

    // Simulate TLS handshake initiated by the server
    serverSocket->SendTo(Create<Packet>("ServerHello"), 0, clientAddress);
    serverState = HANDSHAKE_RECEIVED;


    // Run the simulation
    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}