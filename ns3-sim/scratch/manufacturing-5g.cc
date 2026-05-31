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
    double simTime = 10.0;
    uint16_t numerology = 1;
    uint16_t numVideo = 2;
    uint16_t numIot = 3;
    double videoRateMbps = 6.0;
    double bandwidthMHz = 20.0;
    std::string scheduler = "TdmaRR";
    std::string scenario = "baseline";

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Simulation duration", simTime);
    cmd.AddValue("numerology", "Numerology", numerology);
    cmd.AddValue("numVideo", "Number of video UEs", numVideo);
    cmd.AddValue("numIot", "Number of IoT UEs", numIot);
    cmd.AddValue("videoRate", "Video rate per UE (Mbps)", videoRateMbps);
    cmd.AddValue("bandwidth", "Channel bandwidth (MHz)", bandwidthMHz);
    cmd.AddValue("scheduler", "Scheduler: TdmaRR|OfdmaPF|OfdmaRR", scheduler);
    cmd.AddValue("scenario", "Scenario label for output", scenario);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue(999999999));

    uint32_t videoPort = 1234;
    uint32_t iotPort = 5678;
    double rateBps = videoRateMbps * 1e6;
    uint32_t pktSize = 1000;
    double pktInterval = (pktSize * 8.0) / rateBps;

    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    Ptr<IdealBeamformingHelper> idealBeamformingHelper = CreateObject<IdealBeamformingHelper>();
    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
    nrHelper->SetBeamformingHelper(idealBeamformingHelper);
    nrHelper->SetEpcHelper(epcHelper);
    idealBeamformingHelper->SetAttribute("BeamformingMethod",
                                         TypeIdValue(DirectPathBeamforming::GetTypeId()));
    if (scheduler == "TdmaRR")
        nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacSchedulerTdmaRR"));
    else if (scheduler == "OfdmaPF")
        nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacSchedulerOfdmaPF"));
    else if (scheduler == "OfdmaRR")
        nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacSchedulerOfdmaRR"));
    else
        nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacSchedulerTdmaRR"));
    nrHelper->SetGnbPhyAttribute("Numerology", UintegerValue(numerology));
    nrHelper->SetGnbPhyAttribute("TxPower", DoubleValue(44.0));
    nrHelper->SetUePhyAttribute("TxPower", DoubleValue(20.0));

    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(3.5e9, bandwidthMHz * 1e6, 1, BandwidthPartInfo::UMa);
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    nrHelper->InitializeOperationBand(&band);
    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

    NodeContainer gnbNodes, videoUeNodes, iotUeNodes;
    gnbNodes.Create(1);
    videoUeNodes.Create(numVideo);
    iotUeNodes.Create(numIot);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
    pos->Add(Vector(0.0, 0.0, 10.0));
    for (uint16_t i = 0; i < numVideo; i++)
        pos->Add(Vector(10.0 + i * 5.0, 0.0, 1.5));
    for (uint16_t i = 0; i < numIot; i++)
        pos->Add(Vector(20.0 + i * 3.0, 15.0 + i * 2.0, 1.0));
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
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting = routingHelper.GetStaticRouting(remoteIpv4);
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"),
                                               Ipv4Mask("255.0.0.0"), 1);

    Time udpAppStartTime = Seconds(1.5);

    UdpServerHelper dlPacketSink(videoPort);
    ApplicationContainer serverApps = dlPacketSink.Install(videoUeNodes);
    serverApps.Start(udpAppStartTime);
    serverApps.Stop(Seconds(simTime));

    UdpServerHelper ulPacketSink(iotPort);
    serverApps = ulPacketSink.Install(remoteHost);
    serverApps.Start(udpAppStartTime);
    serverApps.Stop(Seconds(simTime));

    uint32_t maxPackets = 100000;
    for (uint32_t i = 0; i < numVideo; i++)
    {
        UdpClientHelper dlClient(videoIp.GetAddress(i), videoPort);
        dlClient.SetAttribute("MaxPackets", UintegerValue(maxPackets));
        dlClient.SetAttribute("Interval", TimeValue(Seconds(pktInterval)));
        dlClient.SetAttribute("PacketSize", UintegerValue(pktSize));
        ApplicationContainer clientApps = dlClient.Install(remoteHost);
        clientApps.Start(udpAppStartTime);
        clientApps.Stop(Seconds(simTime));
    }

    for (uint32_t i = 0; i < numIot; i++)
    {
        UdpClientHelper ulClient(remoteHostAddr, iotPort);
        ulClient.SetAttribute("MaxPackets", UintegerValue(maxPackets));
        ulClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
        ulClient.SetAttribute("PacketSize", UintegerValue(100));
        ApplicationContainer clientApps = ulClient.Install(iotUeNodes.Get(i));
        clientApps.Start(udpAppStartTime);
        clientApps.Stop(Seconds(simTime));
    }

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    auto classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    auto stats = monitor->GetFlowStats();

    double flowDuration = simTime - 1.5;
    double embbRx = 0, mmtcRx = 0, embbDelay = 0, mmtcDelay = 0;
    uint32_t embbN = 0, mmtcN = 0, embbLost = 0, mmtcLost = 0;

    for (auto& kv : stats)
    {
        auto t = classifier->FindFlow(kv.first);
        double rx = kv.second.rxBytes * 8.0 / flowDuration / 1e6;
        double delay = kv.second.rxPackets > 0
                           ? kv.second.delaySum.GetMilliSeconds() / kv.second.rxPackets
                           : 0.0;
        bool isEmbb = (t.destinationPort == videoPort);
        bool isMmtc = (t.destinationPort == iotPort);
        if (isEmbb) { embbRx += rx; embbDelay += delay; embbN++; embbLost += kv.second.lostPackets; }
        else if (isMmtc) { mmtcRx += rx; mmtcDelay += delay; mmtcN++; mmtcLost += kv.second.lostPackets; }
    }

    std::cout << "\n===== " << scenario << " =====\n"
              << "Scenario: " << scenario
              << " | Scheduler: " << scheduler
              << " | Video UEs: " << numVideo
              << " | IoT UEs: " << numIot
              << " | Video rate: " << videoRateMbps << " Mbps/UE\n";
    std::cout << "eMBB: rx=" << embbRx << " Mbps"
              << " delay=" << (embbN ? embbDelay / embbN : 0) << " ms"
              << " lost=" << embbLost << " pkts\n";
    std::cout << "mMTC: rx=" << mmtcRx << " Mbps"
              << " delay=" << (mmtcN ? mmtcDelay / mmtcN : 0) << " ms"
              << " lost=" << mmtcLost << " pkts\n";
    std::cout << "===== END =====\n";

    Simulator::Destroy();
    return 0;
}
