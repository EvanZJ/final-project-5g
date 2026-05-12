# 5G Manufacturing Simulation — Docker-in-Docker Guide
## Running All Containers Inside a Single Outer Container

---

## Concept

All simulation services run as inner Docker containers **inside one privileged
outer container** (`5g-sim-host`). The outer container runs a Docker daemon
(Docker-in-Docker / DinD), so from the host you only ever run one `docker run`.

```
HOST MACHINE
└── docker run → 5g-sim-host  (DinD, 16g RAM, 10 CPU, privileged)
                 ├── inner dockerd
                 └── inner 5g-sim-net (10.100.0.0/16)
                     ├── 5g-sim-mongodb       10.100.0.10
                     ├── 5g-sim-nrf           10.100.0.20
                     ├── 5g-sim-udr           10.100.0.21
                     ├── 5g-sim-udm           10.100.0.22
                     ├── 5g-sim-ausf          10.100.0.23
                     ├── 5g-sim-nssf          10.100.0.24
                     ├── 5g-sim-pcf           10.100.0.25
                     ├── 5g-sim-amf           10.100.0.30
                     ├── 5g-sim-smf-embb      10.100.0.31
                     ├── 5g-sim-smf-mtc       10.100.0.32
                     ├── 5g-sim-upf-embb      10.100.0.40
                     ├── 5g-sim-upf-mtc       10.100.0.41
                     ├── 5g-sim-webui         10.100.0.50
                     ├── 5g-sim-gnb           10.100.0.60
                     ├── 5g-sim-ue-video-1    10.100.0.71
                     ├── 5g-sim-ue-video-2    10.100.0.72
                     ├── 5g-sim-ue-iot-1..10  10.100.0.81-90
                     ├── 5g-sim-iperf-video   10.100.0.90
                     ├── 5g-sim-iperf-iot     10.100.0.91
                     └── 5g-sim-ns3           10.100.0.100
```

| Layer | Inner containers | RAM | CPU |
|-------|-----------------|-----|-----|
| Core NFs | mongodb + 6 NFs + 2 SMF + 2 UPF + webui | ~8 g | 5.0 |
| UERANSIM | gnb + 2 video UE + 10 IoT UE | ~2 g | 1.75 |
| Traffic | iperf-video + iperf-iot | 256 m | 0.5 |
| NS-3 | build + simulation | 4 g | 4.0 |
| **Outer total** | | **16 g** | **10** |

---

## Prerequisites

- Linux host with Docker installed, user in `docker` group
- Project directory at `/home/rifqi/thesis-rifqi/5gproj`

---

## Step 0 — Create Directory Structure

Run this on the host once to create all directories and install the gtp5g kernel module:

```bash
mkdir -p /home/rifqi/thesis-rifqi/5gproj/config/free5gc/{amf,smf-embb,smf-mtc,upf-embb,upf-mtc,nssf,nrf,ausf,udr,udm,pcf,webui}
mkdir -p /home/rifqi/thesis-rifqi/5gproj/config/ueransim/{gnb,ue-video,ue-iot}
mkdir -p /home/rifqi/thesis-rifqi/5gproj/mongo-init
mkdir -p /home/rifqi/thesis-rifqi/5gproj/ns3-sim/scratch
```

Install GTP5G module (free5GC v3.4.1 requires gtp5g **v0.8.6**):

```bash
docker run --rm --privileged \
    -v /lib/modules:/lib/modules \
    -v /usr/src:/usr/src \
    ubuntu:22.04 \
    sh -c "apt-get update -qq && \
           apt-get install -y git make gcc-12 kmod linux-headers-\$(uname -r) 2>/dev/null && \
           ln -sf /usr/bin/gcc-12 /usr/bin/gcc && \
           git clone --depth 1 --branch v0.8.6 https://github.com/free5gc/gtp5g.git && \
           cd gtp5g && make && \
           rmmod gtp5g 2>/dev/null || true && \
           insmod gtp5g.ko && \
           echo 'gtp5g v0.8.6 loaded OK'"
```

---

## Step 1 — Create Named Volumes

```bash
docker volume create 5g-sim-docker    # inner Docker daemon image/layer storage
docker volume create 5g-sim-mongo     # MongoDB subscriber data
docker volume create 5g-sim-ns3       # NS-3 build cache (survives restarts)
docker volume create 5g-sim-results   # NS-3 simulation output traces
```

---

## Step 2 — Start the Outer Container

```bash
docker run -d \
  --name        5g-sim-host \
  --privileged \
  --cgroupns=host \
  --memory=16g \
  --cpus=10 \
  --memory-swap=16g \
  --shm-size=1g \
  --sysctl      net.ipv4.ip_forward=1 \
  -v /home/rifqi/thesis-rifqi/5gproj:/sim \
  -v 5g-sim-docker:/var/lib/docker \
  -v 5g-sim-mongo:/mongo-data \
  -v 5g-sim-ns3:/ns3-build \
  -v 5g-sim-results:/sim-results \
  -p 5000:5000 \
  docker:26-dind \
  sh -c "modprobe udp_tunnel; modprobe gtp; \
         dockerd --host=unix:///var/run/docker.sock \
                 --storage-driver=overlay2 \
                 --iptables=true \
                 --ip-forward=true"
```

Wait for the inner Docker daemon to be ready:

```bash
until docker exec 5g-sim-host docker info >/dev/null 2>&1; do
  sleep 2 && printf "."
done && echo " inner daemon ready"
```

Verify GTP modules loaded:

```bash
docker exec 5g-sim-host lsmod | grep gtp
```

---

## Step 3 — Create the Inner Network

```bash
docker exec 5g-sim-host \
  docker network create \
    --driver bridge \
    --subnet 10.100.0.0/16 \
    --opt com.docker.network.bridge.name=5gbr0 \
    5g-sim-net
```

---

## Step 4 — Pull Images Inside the Outer Container

```bash
for img in \
  mongo:4.4 \
  free5gc/nrf:v3.4.1 \
  free5gc/udr:v3.4.1 \
  free5gc/udm:v3.4.1 \
  free5gc/ausf:v3.4.1 \
  free5gc/nssf:v3.4.1 \
  free5gc/pcf:v3.4.1 \
  free5gc/amf:v3.4.1 \
  free5gc/smf:v3.4.1 \
  free5gc/upf:v3.4.1 \
  free5gc/webui:v3.4.1 \
  towards5gs/ueransim-gnb \
  towards5gs/ueransim-ue \
  networkstatic/iperf3 \
  ubuntu:22.04; do
  echo "Pulling $img ..."
  docker exec 5g-sim-host docker pull "$img"
done
```

---

## Step 5 — MongoDB

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/mongo-init/01-subscribers.js << 'EOF'
db = db.getSiblingDB('free5gc');

function subscriber(imsi, sst, sd, dnn) {
  return {
    ueId: "imsi-" + imsi,
    tenantId: "admin",
    authenticationSubscription: {
      authenticationMethod: "5G_AKA",
      authenticationManagementField: "8000",
      opc: { opcValue: "E8ED289DEBA952E4283B54E88E6183CA", encryptionAlgorithm: 0, encryptionKey: 0 },
      permanentKey: { permanentKeyValue: "465B5CE8B199B49FAA5F0A2EE238A6BC", encryptionAlgorithm: 0, encryptionKey: 0 },
      sequenceNumber: "16f3b3f70fc2"
    },
    accessAndMobilitySubscriptionData: {
      nssai: {
        defaultSingleNssais: [{ sst: sst, sd: sd }],
        singleNssais:        [{ sst: sst, sd: sd }]
      },
      subscribedUeAmbr: { downlink: "2 Gbps", uplink: "1 Gbps" }
    },
    sessionManagementSubscriptionData: [{
      singleNssai: { sst: sst, sd: sd },
      dnnConfigurations: {
        [dnn]: {
          pduSessionTypes: { defaultSessionType: "IPV4", allowedSessionTypes: ["IPV4"] },
          sscModes: { defaultSscMode: "SSC_MODE_1", allowedSscModes: ["SSC_MODE_2","SSC_MODE_3"] },
          sessionAmbr: { downlink: "1 Gbps", uplink: "500 Mbps" },
          "5gQosProfile": { var5qi: 9, arp: { priorityLevel: 8, preemptCap: "NOT_PREEMPT", preemptVuln: "NOT_PREEMPTABLE" } }
        }
      }
    }],
    smfSelectionSubscriptionData: {
      subscribedSnssaiInfos: { [sst + sd]: { dnnInfos: [{ dnn: dnn }] } }
    }
  };
}

["999700000000001","999700000000002"].forEach(function(imsi) {
  db.subscribers.updateOne({ ueId: "imsi-"+imsi },
    { $set: subscriber(imsi, 1, "000001", "video") }, { upsert: true });
  print("Registered video UE: " + imsi);
});

for (var i = 1; i <= 10; i++) {
  var imsi = "9997000000001" + String(i).padStart(2,'0');
  db.subscribers.updateOne({ ueId: "imsi-"+imsi },
    { $set: subscriber(imsi, 3, "000002", "iot") }, { upsert: true });
  print("Registered IoT UE: " + imsi);
}
print("Done.");
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-mongodb \
    --network     5g-sim-net \
    --ip          10.100.0.10 \
    --memory=1g   --cpus=1.0 \
    --restart     unless-stopped \
    -v 5g-sim-mongo:/data/db \
    -v /sim/mongo-init:/docker-entrypoint-initdb.d:ro \
    mongo:4.4 \
    --wiredTigerCacheSizeGB 0.5
```

Wait until ready:

```bash
until docker exec 5g-sim-host \
    docker exec 5g-sim-mongodb \
    mongo --quiet --eval "db.adminCommand('ping')" >/dev/null 2>&1; do
  sleep 2 && printf "."
done && echo " MongoDB ready"
```

---

## Step 6 — NRF

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/nrf/nrfcfg.yaml << 'EOF'
info:
  version: 1.0.2
  description: NRF configuration

configuration:
  MongoDBName: free5gc
  MongoDBUrl: mongodb://10.100.0.10:27017
  sbi:
    scheme: http
    registerIPv4: 10.100.0.20
    bindingIPv4: 0.0.0.0
    port: 8000
  DefaultPlmnId:
    mcc: "999"
    mnc: "70"
  serviceNameList:
    - nnrf-nfm
    - nnrf-disc
  oauth2: false

logger:
  enable: true
  level: info
  reportCaller: false
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-nrf \
    --network     5g-sim-net \
    --ip          10.100.0.20 \
    --memory=256m --cpus=0.25 \
    --restart     unless-stopped \
    -v /sim/config/free5gc/nrf:/free5gc/config:ro \
    free5gc/nrf:v3.4.1 \
    /free5gc/nrf -c /free5gc/config/nrfcfg.yaml

sleep 4
```

---

## Step 7 — UDR

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/udr/udrcfg.yaml << 'EOF'
info:
  version: 1.0.2
  description: UDR configuration

configuration:
  sbi:
    scheme: http
    registerIPv4: 10.100.0.21
    bindingIPv4: 0.0.0.0
    port: 8000
  serviceNameList:
    - nudr-dr
  mongodb:
    name: free5gc
    url: mongodb://10.100.0.10:27017
  nrfUri: http://10.100.0.20:8000

logger:
  enable: true
  level: info
  reportCaller: false
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-udr \
    --network     5g-sim-net \
    --ip          10.100.0.21 \
    --memory=256m --cpus=0.25 \
    --restart     unless-stopped \
    -v /sim/config/free5gc/udr:/free5gc/config:ro \
    free5gc/udr:v3.4.1 \
    /free5gc/udr -c /free5gc/config/udrcfg.yaml
```

---

## Step 8 — UDM

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/udm/udmcfg.yaml << 'EOF'
info:
  version: 1.0.3
  description: UDM configuration

configuration:
  serviceNameList:
    - nudm-sdm
    - nudm-uecm
    - nudm-ueau
    - nudm-ee
    - nudm-pp
  sbi:
    scheme: http
    registerIPv4: 10.100.0.22
    bindingIPv4: 0.0.0.0
    port: 8000
  nrfUri: http://10.100.0.20:8000

logger:
  enable: true
  level: info
  reportCaller: false
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-udm \
    --network     5g-sim-net \
    --ip          10.100.0.22 \
    --memory=256m --cpus=0.25 \
    --restart     unless-stopped \
    -v /sim/config/free5gc/udm:/free5gc/config:ro \
    free5gc/udm:v3.4.1 \
    /free5gc/udm -c /free5gc/config/udmcfg.yaml
```

---

## Step 9 — AUSF

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/ausf/ausfcfg.yaml << 'EOF'
info:
  version: 1.0.3
  description: AUSF configuration

configuration:
  sbi:
    scheme: http
    registerIPv4: 10.100.0.23
    bindingIPv4: 0.0.0.0
    port: 8000
  serviceNameList:
    - nausf-auth
  nrfUri: http://10.100.0.20:8000
  plmnSupportList:
    - mcc: "999"
      mnc: "70"

logger:
  enable: true
  level: info
  reportCaller: false
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ausf \
    --network     5g-sim-net \
    --ip          10.100.0.23 \
    --memory=256m --cpus=0.25 \
    --restart     unless-stopped \
    -v /sim/config/free5gc/ausf:/free5gc/config:ro \
    free5gc/ausf:v3.4.1 \
    /free5gc/ausf -c /free5gc/config/ausfcfg.yaml
```

---

## Step 10 — NSSF

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/nssf/nssfcfg.yaml << 'EOF'
info:
  version: 1.0.2
  description: NSSF configuration

configuration:
  nssfName: NSSF
  sbi:
    scheme: http
    registerIPv4: 10.100.0.24
    bindingIPv4: 0.0.0.0
    port: 8000
  serviceNameList:
    - nnssf-nsselection
    - nnssf-nssaiavailability
  nrfUri: http://10.100.0.20:8000
  supportedPlmnList:
    - mcc: "999"
      mnc: "70"
  supportedNssaiInPlmnList:
    - plmnId:
        mcc: "999"
        mnc: "70"
      supportedSnssaiList:
        - sst: 1
          sd: "000001"
        - sst: 3
          sd: "000002"
  nsiList:
    - snssai:
        sst: 1
        sd: "000001"
      nsiInformationList:
        - nrfId: http://10.100.0.20:8000/nnrf-nfm/v1/nf-instances
          nsiId: "111"
    - snssai:
        sst: 3
        sd: "000002"
      nsiInformationList:
        - nrfId: http://10.100.0.20:8000/nnrf-nfm/v1/nf-instances
          nsiId: "222"

logger:
  enable: true
  level: info
  reportCaller: false
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-nssf \
    --network     5g-sim-net \
    --ip          10.100.0.24 \
    --memory=256m --cpus=0.25 \
    --restart     unless-stopped \
    -v /sim/config/free5gc/nssf:/free5gc/config:ro \
    free5gc/nssf:v3.4.1 \
    /free5gc/nssf -c /free5gc/config/nssfcfg.yaml
```

---

## Step 11 — PCF

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/pcf/pcfcfg.yaml << 'EOF'
info:
  version: 1.0.2
  description: PCF configuration

configuration:
  pcfName: PCF
  sbi:
    scheme: http
    registerIPv4: 10.100.0.25
    bindingIPv4: 0.0.0.0
    port: 8000
  timeFormat: 2019-01-02 15:04:05
  defaultBdtRefId: BdtPolicyId-
  nrfUri: http://10.100.0.20:8000
  serviceList:
    - serviceName: npcf-am-policy-control
    - serviceName: npcf-smpolicycontrol
      suppFeat: 3fff
    - serviceName: npcf-bdtpolicycontrol
    - serviceName: npcf-policyauthorization
      suppFeat: 3
    - serviceName: npcf-eventexposure
    - serviceName: npcf-ue-policy-control
  mongodb:
    name: free5gc
    url: mongodb://10.100.0.10:27017

logger:
  enable: true
  level: info
  reportCaller: false
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-pcf \
    --network     5g-sim-net \
    --ip          10.100.0.25 \
    --memory=256m --cpus=0.25 \
    --restart     unless-stopped \
    -v /sim/config/free5gc/pcf:/free5gc/config:ro \
    free5gc/pcf:v3.4.1 \
    /free5gc/pcf -c /free5gc/config/pcfcfg.yaml

sleep 8
```

---

## Step 12 — UPF eMBB

UPFs need `NET_ADMIN` and `/dev/net/tun` for GTP-U tunnel creation.

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/upf-embb/upfcfg.yaml << 'EOF'
version: 1.0.3
description: UPF eMBB configuration

pfcp:
  addr: 10.100.0.40
  nodeID: 10.100.0.40
  retransTimeout: 1s
  maxRetrans: 3

gtpu:
  forwarder: gtp5g
  ifList:
    - addr: 10.100.0.40
      type: N3

dnnList:
  - dnn: video
    cidr: 10.61.0.0/16
    natifname: eth0

logger:
  enable: true
  level: info
  reportCaller: false
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-upf-embb \
    --network     5g-sim-net \
    --ip          10.100.0.40 \
    --memory=1g   --cpus=1.0 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    --device      /dev/net/tun \
    --sysctl      net.ipv4.ip_forward=1 \
    -v /sim/config/free5gc/upf-embb:/free5gc/config:ro \
    free5gc/upf:v3.4.1 \
    /free5gc/upf -c /free5gc/config/upfcfg.yaml
```

---

## Step 13 — UPF mMTC

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/upf-mtc/upfcfg.yaml << 'EOF'
version: 1.0.3
description: UPF mMTC configuration

pfcp:
  addr: 10.100.0.41
  nodeID: 10.100.0.41
  retransTimeout: 1s
  maxRetrans: 3

gtpu:
  forwarder: gtp5g
  ifList:
    - addr: 10.100.0.41
      type: N3

dnnList:
  - dnn: iot
    cidr: 10.62.0.0/16
    natifname: eth0

logger:
  enable: true
  level: info
  reportCaller: false
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-upf-mtc \
    --network     5g-sim-net \
    --ip          10.100.0.41 \
    --memory=1g   --cpus=1.0 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    --device      /dev/net/tun \
    --sysctl      net.ipv4.ip_forward=1 \
    -v /sim/config/free5gc/upf-mtc:/free5gc/config:ro \
    free5gc/upf:v3.4.1 \
    /free5gc/upf -c /free5gc/config/upfcfg.yaml

sleep 5
```

---

## Step 14 — AMF

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/amf/amfcfg.yaml << 'EOF'
info:
  version: 1.0.9
  description: AMF configuration

configuration:
  amfName: AMF
  ngapIpList:
    - 10.100.0.30
  ngapPort: 38412
  sbi:
    scheme: http
    registerIPv4: 10.100.0.30
    bindingIPv4: 0.0.0.0
    port: 8000
  serviceNameList:
    - namf-comm
    - namf-evts
    - namf-mt
    - namf-loc
    - namf-oam
  servedGuamiList:
    - plmnId:
        mcc: "999"
        mnc: "70"
      amfId: cafe00
  supportTaiList:
    - plmnId:
        mcc: "999"
        mnc: "70"
      tac: "000001"
  plmnSupportList:
    - plmnId:
        mcc: "999"
        mnc: "70"
      snssaiList:
        - sst: 1
          sd: "000001"
        - sst: 3
          sd: "000002"
  supportDnnList:
    - video
    - iot
  nrfUri: http://10.100.0.20:8000
  security:
    integrityOrder: [NIA2, NIA0]
    cipheringOrder: [NEA0, NEA2]
  networkName:
    full: free5GC
    short: free
  locality: area1
  sctp:
    numOstreams: 3
    maxInstreams: 5
    maxAttempts: 2
    maxInitTimeout: 2
  defaultUECtxReq: false
  t3502Value: 720
  t3512Value: 3240
  non3gppDeregistrationTimerValue: 3240
  t3513:
    enable: true
    expireTime: 6s
    maxRetryTimes: 4
  t3522:
    enable: true
    expireTime: 6s
    maxRetryTimes: 4
  t3550:
    enable: true
    expireTime: 6s
    maxRetryTimes: 4
  t3555:
    enable: true
    expireTime: 6s
    maxRetryTimes: 4
  t3560:
    enable: true
    expireTime: 6s
    maxRetryTimes: 4
  t3565:
    enable: true
    expireTime: 6s
    maxRetryTimes: 4
  t3570:
    enable: true
    expireTime: 6s
    maxRetryTimes: 4

logger:
  enable: true
  level: info
  reportCaller: false
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-amf \
    --network     5g-sim-net \
    --ip          10.100.0.30 \
    --memory=512m --cpus=0.5 \
    --restart     unless-stopped \
    -v /sim/config/free5gc/amf:/free5gc/config:ro \
    free5gc/amf:v3.4.1 \
    /free5gc/amf -c /free5gc/config/amfcfg.yaml

sleep 5
```

---

## Step 15 — SMF eMBB

SMFs run PFCP association with UPFs at startup — UPFs must be running first.

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/smf-embb/smfcfg.yaml << 'EOF'
info:
  version: 1.0.7
  description: SMF eMBB configuration

configuration:
  smfName: SMF-eMBB
  sbi:
    scheme: http
    registerIPv4: 10.100.0.31
    bindingIPv4: 0.0.0.0
    port: 8000
  serviceNameList:
    - nsmf-pdusession
    - nsmf-event-exposure
    - nsmf-oam
  snssaiInfos:
    - sNssai:
        sst: 1
        sd: "000001"
      dnnInfos:
        - dnn: video
          dns:
            ipv4: 8.8.8.8
  pfcp:
    listenAddr: 10.100.0.31
    externalAddr: 10.100.0.31
    nodeID: 10.100.0.31
  locality: area1
  nrfUri: http://10.100.0.20:8000
  userplaneInformation:
    upNodes:
      gNB1:
        type: AN
      UPF-eMBB:
        type: UPF
        nodeID: 10.100.0.40
        addr: 10.100.0.40
        sNssaiUpfInfos:
          - sNssai:
              sst: 1
              sd: "000001"
            dnnUpfInfoList:
              - dnn: video
                pools:
                  - cidr: 10.61.0.0/16
        interfaces:
          - interfaceType: N3
            endpoints:
              - 10.100.0.40
            networkInstances:
              - internet
    links:
      - A: gNB1
        B: UPF-eMBB
  t3591:
    enable: true
    expireTime: 16s
    maxRetryTimes: 3
  t3592:
    enable: true
    expireTime: 16s
    maxRetryTimes: 3

logger:
  enable: true
  level: info
  reportCaller: false
EOF

cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/smf-embb/uerouting.yaml << 'EOF'
info:
  version: 1.0.7
  description: Routing information for UE

ueRoutingInfo: {}
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-smf-embb \
    --network     5g-sim-net \
    --ip          10.100.0.31 \
    --memory=512m --cpus=0.5 \
    --restart     unless-stopped \
    -v /sim/config/free5gc/smf-embb:/free5gc/config:ro \
    free5gc/smf:v3.4.1 \
    /free5gc/smf -c /free5gc/config/smfcfg.yaml
```

---

## Step 16 — SMF mMTC

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/smf-mtc/smfcfg.yaml << 'EOF'
info:
  version: 1.0.7
  description: SMF mMTC configuration

configuration:
  smfName: SMF-mMTC
  sbi:
    scheme: http
    registerIPv4: 10.100.0.32
    bindingIPv4: 0.0.0.0
    port: 8000
  serviceNameList:
    - nsmf-pdusession
    - nsmf-event-exposure
    - nsmf-oam
  snssaiInfos:
    - sNssai:
        sst: 3
        sd: "000002"
      dnnInfos:
        - dnn: iot
          dns:
            ipv4: 8.8.8.8
  pfcp:
    listenAddr: 10.100.0.32
    externalAddr: 10.100.0.32
    nodeID: 10.100.0.32
  locality: area1
  nrfUri: http://10.100.0.20:8000
  userplaneInformation:
    upNodes:
      gNB1:
        type: AN
      UPF-mMTC:
        type: UPF
        nodeID: 10.100.0.41
        addr: 10.100.0.41
        sNssaiUpfInfos:
          - sNssai:
              sst: 3
              sd: "000002"
            dnnUpfInfoList:
              - dnn: iot
                pools:
                  - cidr: 10.62.0.0/16
        interfaces:
          - interfaceType: N3
            endpoints:
              - 10.100.0.41
            networkInstances:
              - internet
    links:
      - A: gNB1
        B: UPF-mMTC
  t3591:
    enable: true
    expireTime: 16s
    maxRetryTimes: 3
  t3592:
    enable: true
    expireTime: 16s
    maxRetryTimes: 3

logger:
  enable: true
  level: info
  reportCaller: false
EOF

cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/smf-mtc/uerouting.yaml << 'EOF'
info:
  version: 1.0.7
  description: Routing information for UE

ueRoutingInfo: {}
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-smf-mtc \
    --network     5g-sim-net \
    --ip          10.100.0.32 \
    --memory=512m --cpus=0.5 \
    --restart     unless-stopped \
    -v /sim/config/free5gc/smf-mtc:/free5gc/config:ro \
    free5gc/smf:v3.4.1 \
    /free5gc/smf -c /free5gc/config/smfcfg.yaml

sleep 5
```

Verify PFCP associations:

```bash
docker exec 5g-sim-host docker logs 5g-sim-smf-embb 2>&1 | grep -i "pfcp.*success"
docker exec 5g-sim-host docker logs 5g-sim-smf-mtc  2>&1 | grep -i "pfcp.*success"
```

---

## Step 17 — WebUI

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/free5gc/webui/webuicfg.yaml << 'EOF'
info:
  version: 1.0.2
  description: WebUI configuration

configuration:
  mongodb:
    name: free5gc
    url: mongodb://10.100.0.10:27017
  nrfUri: http://10.100.0.20:8000
  webServer:
    scheme: http
    ipv4Address: 0.0.0.0
    port: 5000
  billingServer:
    enable: true
    hostIPv4: 127.0.0.1
    listenPort: 2122
    port: 2121
    tls:
      pem: cert/chf.pem
      key: cert/chf.key

logger:
  enable: true
  level: info
  reportCaller: false
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-webui \
    --network     5g-sim-net \
    --ip          10.100.0.50 \
    --memory=256m --cpus=0.25 \
    --restart     unless-stopped \
    -p 5000:5000 \
    -v /sim/config/free5gc/webui:/free5gc/config:ro \
    free5gc/webui:v3.4.1 \
    /free5gc/webui -c /free5gc/config/webuicfg.yaml
```

---

## Step 18 — iperf3 Video Server

```bash
docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-iperf-video \
    --network     5g-sim-net \
    --ip          10.100.0.90 \
    --memory=128m --cpus=0.25 \
    --restart     unless-stopped \
    networkstatic/iperf3 -s
```

---

## Step 19 — iperf3 IoT Server

```bash
docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-iperf-iot \
    --network     5g-sim-net \
    --ip          10.100.0.91 \
    --memory=128m --cpus=0.25 \
    --restart     unless-stopped \
    networkstatic/iperf3 -s
```

---

## Step 20 — gNB

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/gnb/gnb.yaml << 'EOF'
mcc: '999'
mnc: '70'
nci: '0x000000010'
idLength: 32
tac: 1
linkIp: 10.100.0.60
ngapIp: 10.100.0.60
gtpIp:  10.100.0.60
amfConfigs:
  - address: 10.100.0.30
    port: 38412
slices:
  - sst: 1
    sd: "000001"
  - sst: 3
    sd: "000002"
ignoreStreamIds: true
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-gnb \
    --network     5g-sim-net \
    --ip          10.100.0.60 \
    --memory=512m --cpus=0.5 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/gnb:/etc/ueransim:ro \
    towards5gs/ueransim-gnb \
    /ueransim/build/nr-gnb -c /etc/ueransim/gnb.yaml

sleep 6

docker exec 5g-sim-host docker logs 5g-sim-gnb 2>&1 | grep -i "ng setup\|connected"
```

---

## Step 21 — UE Video 1

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-video/ue-video-1.yaml << 'EOF'
supi: 'imsi-999700000000001'
mcc: '999'
mnc: '70'
key: '465B5CE8B199B49FAA5F0A2EE238A6BC'
op:  'E8ED289DEBA952E4283B54E88E6183CA'
opType: 'OPC'
amf: '8000'
imei: '356938035643803'
gnbSearchList:
  - 10.100.0.60
initialSlices:
  - sst: 1
    sd: "000001"
sessions:
  - type: 'IPv4'
    apn: 'video'
    slice:
      sst: 1
      sd: "000001"
configured-nssai:
  - sst: 1
    sd: "000001"
default-nssai:
  - sst: 1
    sd: "000001"
integrity:
  IA1: true
  IA2: true
  IA3: false
ciphering:
  EA1: true
  EA2: true
  EA3: false
integrityMaxRate:
  uplink: 'full'
  downlink: 'full'
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ue-video-1 \
    --network     5g-sim-net \
    --ip          10.100.0.71 \
    --memory=256m --cpus=0.25 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/ue-video:/etc/ueransim:ro \
    towards5gs/ueransim-ue \
    /ueransim/build/nr-ue -c /etc/ueransim/ue-video-1.yaml

sleep 3
```

---

## Step 22 — UE Video 2

```bash
sed 's/000000001/000000002/; s/643803/643804/' \
  /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-video/ue-video-1.yaml \
  > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-video/ue-video-2.yaml

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ue-video-2 \
    --network     5g-sim-net \
    --ip          10.100.0.72 \
    --memory=256m --cpus=0.25 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/ue-video:/etc/ueransim:ro \
    towards5gs/ueransim-ue \
    /ueransim/build/nr-ue -c /etc/ueransim/ue-video-2.yaml

sleep 3
```

---

## Step 23 — UE IoT 1

```bash
cat > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-1.yaml << 'EOF'
supi: 'imsi-999700000000101'
mcc: '999'
mnc: '70'
key: '465B5CE8B199B49FAA5F0A2EE238A6BC'
op:  'E8ED289DEBA952E4283B54E88E6183CA'
opType: 'OPC'
amf: '8000'
imei: '356938035643901'
gnbSearchList:
  - 10.100.0.60
initialSlices:
  - sst: 3
    sd: "000002"
sessions:
  - type: 'IPv4'
    apn: 'iot'
    slice:
      sst: 3
      sd: "000002"
configured-nssai:
  - sst: 3
    sd: "000002"
default-nssai:
  - sst: 3
    sd: "000002"
integrity:
  IA1: true
  IA2: true
  IA3: false
ciphering:
  EA1: true
  EA2: true
  EA3: false
integrityMaxRate:
  uplink: 'full'
  downlink: 'full'
EOF

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ue-iot-1 \
    --network     5g-sim-net \
    --ip          10.100.0.81 \
    --memory=128m --cpus=0.1 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/ue-iot:/etc/ueransim:ro \
    towards5gs/ueransim-ue \
    /ueransim/build/nr-ue -c /etc/ueransim/ue-iot-1.yaml

sleep 1
```

---

## Step 24 — UE IoT 2

```bash
sed 's/000000101/000000102/; s/356938035643901/356938035643902/' \
  /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-1.yaml \
  > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-2.yaml

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ue-iot-2 \
    --network     5g-sim-net \
    --ip          10.100.0.82 \
    --memory=128m --cpus=0.1 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/ue-iot:/etc/ueransim:ro \
    towards5gs/ueransim-ue \
    /ueransim/build/nr-ue -c /etc/ueransim/ue-iot-2.yaml

sleep 1
```

---

## Step 25 — UE IoT 3

```bash
sed 's/000000101/000000103/; s/356938035643901/356938035643903/' \
  /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-1.yaml \
  > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-3.yaml

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ue-iot-3 \
    --network     5g-sim-net \
    --ip          10.100.0.83 \
    --memory=128m --cpus=0.1 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/ue-iot:/etc/ueransim:ro \
    towards5gs/ueransim-ue \
    /ueransim/build/nr-ue -c /etc/ueransim/ue-iot-3.yaml

sleep 1
```

---

## Step 26 — UE IoT 4

```bash
sed 's/000000101/000000104/; s/356938035643901/356938035643904/' \
  /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-1.yaml \
  > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-4.yaml

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ue-iot-4 \
    --network     5g-sim-net \
    --ip          10.100.0.84 \
    --memory=128m --cpus=0.1 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/ue-iot:/etc/ueransim:ro \
    towards5gs/ueransim-ue \
    /ueransim/build/nr-ue -c /etc/ueransim/ue-iot-4.yaml

sleep 1
```

---

## Step 27 — UE IoT 5

```bash
sed 's/000000101/000000105/; s/356938035643901/356938035643905/' \
  /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-1.yaml \
  > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-5.yaml

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ue-iot-5 \
    --network     5g-sim-net \
    --ip          10.100.0.85 \
    --memory=128m --cpus=0.1 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/ue-iot:/etc/ueransim:ro \
    towards5gs/ueransim-ue \
    /ueransim/build/nr-ue -c /etc/ueransim/ue-iot-5.yaml

sleep 1
```

---

## Step 28 — UE IoT 6

```bash
sed 's/000000101/000000106/; s/356938035643901/356938035643906/' \
  /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-1.yaml \
  > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-6.yaml

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ue-iot-6 \
    --network     5g-sim-net \
    --ip          10.100.0.86 \
    --memory=128m --cpus=0.1 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/ue-iot:/etc/ueransim:ro \
    towards5gs/ueransim-ue \
    /ueransim/build/nr-ue -c /etc/ueransim/ue-iot-6.yaml

sleep 1
```

---

## Step 29 — UE IoT 7

```bash
sed 's/000000101/000000107/; s/356938035643901/356938035643907/' \
  /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-1.yaml \
  > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-7.yaml

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ue-iot-7 \
    --network     5g-sim-net \
    --ip          10.100.0.87 \
    --memory=128m --cpus=0.1 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/ue-iot:/etc/ueransim:ro \
    towards5gs/ueransim-ue \
    /ueransim/build/nr-ue -c /etc/ueransim/ue-iot-7.yaml

sleep 1
```

---

## Step 30 — UE IoT 8

```bash
sed 's/000000101/000000108/; s/356938035643901/356938035643908/' \
  /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-1.yaml \
  > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-8.yaml

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ue-iot-8 \
    --network     5g-sim-net \
    --ip          10.100.0.88 \
    --memory=128m --cpus=0.1 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/ue-iot:/etc/ueransim:ro \
    towards5gs/ueransim-ue \
    /ueransim/build/nr-ue -c /etc/ueransim/ue-iot-8.yaml

sleep 1
```

---

## Step 31 — UE IoT 9

```bash
sed 's/000000101/000000109/; s/356938035643901/356938035643909/' \
  /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-1.yaml \
  > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-9.yaml

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ue-iot-9 \
    --network     5g-sim-net \
    --ip          10.100.0.89 \
    --memory=128m --cpus=0.1 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/ue-iot:/etc/ueransim:ro \
    towards5gs/ueransim-ue \
    /ueransim/build/nr-ue -c /etc/ueransim/ue-iot-9.yaml

sleep 1
```

---

## Step 32 — UE IoT 10

```bash
sed 's/000000101/000000110/; s/356938035643901/356938035643910/' \
  /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-1.yaml \
  > /home/rifqi/thesis-rifqi/5gproj/config/ueransim/ue-iot/ue-iot-10.yaml

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ue-iot-10 \
    --network     5g-sim-net \
    --ip          10.100.0.90 \
    --memory=128m --cpus=0.1 \
    --restart     unless-stopped \
    --cap-add     NET_ADMIN \
    -v /sim/config/ueransim/ue-iot:/etc/ueransim:ro \
    towards5gs/ueransim-ue \
    /ueransim/build/nr-ue -c /etc/ueransim/ue-iot-10.yaml

sleep 3
```

Verify PDU sessions for both slices:

```bash
docker exec 5g-sim-host docker exec 5g-sim-ue-video-1 ip addr show uesimtun0
# Expected: inet 10.61.x.x  (eMBB UE pool)

docker exec 5g-sim-host docker exec 5g-sim-ue-iot-1 ip addr show uesimtun0
# Expected: inet 10.62.x.x  (mMTC UE pool)
```

---

## Step 33 — NS-3 Radio Simulation

```bash
docker exec 5g-sim-host sh -c 'cat > /tmp/ns3-start.sh << "SCRIPT"
#!/bin/sh
set -e
NS3_DIR="/ns3-build/ns-3-dev"
echo "[ns3] Installing build deps ..."
apt-get update -qq && apt-get install -y --no-install-recommends \
  g++ cmake ninja-build python3 git \
  libboost-all-dev libgsl-dev libsqlite3-dev libeigen3-dev ccache \
  >/dev/null 2>&1
if [ ! -d "${NS3_DIR}" ]; then
  echo "[ns3] Cloning NS-3 3.40 ..."
  git clone --depth 1 --branch ns-3.40 \
    https://gitlab.com/nsnam/ns-3-dev.git "${NS3_DIR}"
  echo "[ns3] Cloning 5G-LENA nr module ..."
  git clone --depth 1 --branch 5g-lena-v3.1 \
    https://gitlab.com/cttc-lena/nr.git "${NS3_DIR}/contrib/nr"
else
  echo "[ns3] Using cached build at ${NS3_DIR}"
fi
cp -f /sim/ns3-sim/scratch/manufacturing-5g.cc "${NS3_DIR}/scratch/"
cd "${NS3_DIR}"
python3 ns3 configure \
  --enable-modules=nr,internet,point-to-point,tap-bridge,fd-net-device,flow-monitor \
  --build-profile=optimized --disable-examples --disable-tests >/dev/null 2>&1
echo "[ns3] Building (first run ~30 min) ..."
python3 ns3 build -j$(nproc) 2>&1 | tail -3
mkdir -p /sim-results && cd /sim-results
echo "[ns3] Running simulation ..."
python3 "${NS3_DIR}/ns3" run \
  "scratch/manufacturing-5g --simTime=30 --useTap=false --prbEmbb=70 --prbMmtc=36" \
  2>&1 | tee /sim-results/sim-run.log
echo "[ns3] Done."
SCRIPT
chmod +x /tmp/ns3-start.sh'

docker exec 5g-sim-host \
  docker run -d \
    --name        5g-sim-ns3 \
    --network     5g-sim-net \
    --ip          10.100.0.100 \
    --memory=4g   --cpus=4.0 \
    --cap-add     NET_ADMIN \
    -v 5g-sim-ns3:/ns3-build \
    -v 5g-sim-results:/sim-results \
    -v /sim:/sim:ro \
    -v /tmp/ns3-start.sh:/entrypoint.sh:ro \
    ubuntu:22.04 \
    sh /entrypoint.sh
```

Follow NS-3 build progress:

```bash
docker exec 5g-sim-host docker logs -f 5g-sim-ns3
```

---

## Verification

```bash
# All inner containers running
docker exec 5g-sim-host docker ps \
  --format "table {{.Names}}\t{{.Status}}\t{{.Image}}"

# NRF registrations
docker exec 5g-sim-host \
  docker exec 5g-sim-nrf \
  wget -qO- http://10.100.0.20:8000/nnrf-nfm/v1/nf-instances \
  | python3 -m json.tool | grep nfType

# eMBB throughput test
docker exec 5g-sim-host \
  docker exec 5g-sim-ue-video-1 \
  iperf3 -c 10.100.0.90 -u -b 50M -t 10 --bind-dev uesimtun0

# WebUI
# http://localhost:5000  (admin / free5gc)
```

---

## Useful Commands

```bash
# Logs of any inner container
docker exec 5g-sim-host docker logs -f 5g-sim-amf

# Shell inside any inner container
docker exec 5g-sim-host docker exec -it 5g-sim-amf sh

# Shell inside the outer container
docker exec -it 5g-sim-host sh

# Resource usage of all inner containers
docker exec 5g-sim-host docker stats --no-stream

# Outer container usage from host
docker stats --no-stream 5g-sim-host
```

---

## Restart a Single NF

If one container is stuck in a restart loop, remove it and re-run its step:

```bash
docker exec 5g-sim-host docker rm -f 5g-sim-nrf
# then re-run Step 6
```

---

## Teardown

```bash
# Stop and remove all inner containers
docker exec 5g-sim-host \
  sh -c "docker rm -f \$(docker ps -aq)" 2>/dev/null || true

# Stop and remove the outer container
docker stop 5g-sim-host && docker rm 5g-sim-host

# Remove volumes (WARNING: deletes MongoDB data and NS-3 build cache)
docker volume rm 5g-sim-docker 5g-sim-mongo 5g-sim-ns3 5g-sim-results

# Keep NS-3 build cache, remove only data
docker volume rm 5g-sim-mongo 5g-sim-results
```