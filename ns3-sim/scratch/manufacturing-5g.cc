#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Manufacturing5g");

int
main(int argc, char* argv[])
{
    double simTime = 15.0;
    uint16_t numerology = 1;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Simulation duration", simTime);
    cmd.AddValue("numerology", "Numerology", numerology);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue(999999999));

    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    Ptr<IdealBeamformingHelper> idealBeamformingHelper = CreateObject<IdealBeamformingHelper>();
    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
    nrHelper->SetBeamformingHelper(idealBeamformingHelper);
    nrHelper->SetEpcHelper(epcHelper);
    idealBeamformingHelper->SetAttribute("BeamformingMethod",
                                         TypeIdValue(DirectPathBeamforming::GetTypeId()));
    nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacSchedulerTdmaRR"));
    nrHelper->SetGnbPhyAttribute("Numerology", UintegerValue(numerology));
    nrHelper->SetGnbPhyAttribute("TxPower", DoubleValue(44.0));
    nrHelper->SetUePhyAttribute("TxPower", DoubleValue(20.0));

    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(3.5e9, 20e6, 1, BandwidthPartInfo::UMa);
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    nrHelper->InitializeOperationBand(&band);
    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

    NodeContainer gnbNodes, videoUeNodes, iotUeNodes;
    gnbNodes.Create(1);
    videoUeNodes.Create(2);
    iotUeNodes.Create(3);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
    pos->Add(Vector(0.0, 0.0, 10.0));
    pos->Add(Vector(10.0, 0.0, 1.5));
    pos->Add(Vector(-10.0, 0.0, 1.5));
    pos->Add(Vector(25.0, 20.0, 1.0));
    pos->Add(Vector(-25.0, 15.0, 1.0));
    pos->Add(Vector(5.0, -30.0, 1.0));
    mobility.SetPositionAllocator(pos);
    mobility.Install(gnbNodes);
    mobility.Install(videoUeNodes);
    mobility.Install(iotUeNodes);

    NetDeviceContainer gnbDev = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
    NetDeviceContainer videoUeDev = nrHelper->InstallUeDevice(videoUeNodes, allBwps);
    NetDeviceContainer iotUeDev = nrHelper->InstallUeDevice(iotUeNodes, allBwps);

    for (auto it = gnbDev.Begin(); it != gnbDev.End(); ++it)
        DynamicCast<NrGnbNetDevice>(*it)->UpdateConfig();
    for (auto it = videoUeDev.Begin(); it != videoUeDev.End(); ++it)
        DynamicCast<NrUeNetDevice>(*it)->UpdateConfig();
    for (auto it = iotUeDev.Begin(); it != iotUeDev.End(); ++it)
        DynamicCast<NrUeNetDevice>(*it)->UpdateConfig();

    InternetStackHelper inet;
    inet.Install(videoUeNodes);
    inet.Install(iotUeNodes);

    Ipv4InterfaceContainer videoIp = epcHelper->AssignUeIpv4Address(videoUeDev);
    Ipv4InterfaceContainer iotIp = epcHelper->AssignUeIpv4Address(iotUeDev);

    NodeContainer allUeNodes;
    allUeNodes.Add(videoUeNodes);
    allUeNodes.Add(iotUeNodes);
    for (uint32_t i = 0; i < allUeNodes.GetN(); i++)
    {
        Ipv4StaticRoutingHelper routing;
        routing.GetStaticRouting(allUeNodes.Get(i)->GetObject<Ipv4>())
            ->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    nrHelper->AttachToClosestEnb(videoUeDev, gnbDev);
    nrHelper->AttachToClosestEnb(iotUeDev, gnbDev);

    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    inet.Install(remoteHostContainer);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);
    Ptr<Ipv4> remoteIpv4 = remoteHost->GetObject<Ipv4>();

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
    p2p.SetChannelAttribute("Delay", TimeValue(Seconds(0.001)));
    NetDeviceContainer internetDevices = p2p.Install(epcHelper->GetPgwNode(), remoteHost);
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);
    Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress(1);
    Ipv4StaticRoutingHelper routingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        routingHelper.GetStaticRouting(remoteIpv4);
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"),
                                               Ipv4Mask("255.0.0.0"), 1);

    Time appStart = Seconds(1.0);
    Time udpAppStartTime = Seconds(1.5);
    uint16_t dlPort = 1234;
    uint16_t ulPort = 5678;

    UdpServerHelper dlPacketSink(dlPort);
    ApplicationContainer serverApps = dlPacketSink.Install(videoUeNodes);
    serverApps.Start(udpAppStartTime);
    serverApps.Stop(Seconds(simTime));

    UdpServerHelper ulPacketSink(ulPort);
    serverApps = ulPacketSink.Install(remoteHost);
    serverApps.Start(udpAppStartTime);
    serverApps.Stop(Seconds(simTime));

    Time interPacketInterval = Seconds(0.001333);
    uint32_t maxPackets = 10000;
    UdpClientHelper dlClient(videoIp.GetAddress(0), dlPort);
    dlClient.SetAttribute("MaxPackets", UintegerValue(maxPackets));
    dlClient.SetAttribute("Interval", TimeValue(interPacketInterval));
    dlClient.SetAttribute("PacketSize", UintegerValue(1000));
    ApplicationContainer clientApps = dlClient.Install(remoteHost);
    clientApps.Start(udpAppStartTime);
    clientApps.Stop(Seconds(simTime));

    UdpClientHelper ulClient(remoteHostAddr, ulPort);
    ulClient.SetAttribute("MaxPackets", UintegerValue(maxPackets));
    ulClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    ulClient.SetAttribute("PacketSize", UintegerValue(100));
    clientApps = ulClient.Install(iotUeNodes);
    clientApps.Start(udpAppStartTime);
    clientApps.Stop(Seconds(simTime));

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    auto classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    auto stats = monitor->GetFlowStats();

    double flowDuration = simTime - 1.5;
    double embbRx = 0, mmtcRx = 0, embbDelay = 0, mmtcDelay = 0;
    uint32_t embbN = 0, mmtcN = 0;

    for (auto& kv : stats)
    {
        auto t = classifier->FindFlow(kv.first);
        double rx = kv.second.rxBytes * 8.0 / flowDuration / 1e6;
        double delay = kv.second.rxPackets > 0
                           ? kv.second.delaySum.GetMilliSeconds() / kv.second.rxPackets
                           : 0.0;
        bool isEmbb = (t.destinationPort == dlPort);
        bool isMmtc = (t.destinationPort == ulPort);
        if (isEmbb) { embbRx += rx; embbDelay += delay; embbN++; }
        else if (isMmtc) { mmtcRx += rx; mmtcDelay += delay; mmtcN++; }

        std::cout << (isEmbb ? "[eMBB]" : isMmtc ? "[mMTC]" : "[----]")
                  << " flow=" << kv.first
                  << " src=" << t.sourceAddress << ":" << t.sourcePort
                  << " dst=" << t.destinationAddress << ":" << t.destinationPort
                  << " rx=" << rx << "Mbps"
                  << " delay=" << delay << "ms"
                  << " lost=" << kv.second.lostPackets << "\n";
    }

    std::cout << "\n===== Slice Summary =====\n"
              << "eMBB: rx=" << embbRx << " Mbps  delay="
              << (embbN ? embbDelay / embbN : 0) << " ms\n"
              << "mMTC: rx=" << mmtcRx << " Mbps  delay="
              << (mmtcN ? mmtcDelay / mmtcN : 0) << " ms\n";

    monitor->SerializeToXmlFile("/sim-results/flow-monitor.xml", true, true);
    Simulator::Destroy();
    return 0;
}
