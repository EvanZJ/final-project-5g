# Hybrid 5G Manufacturing Simulation — Network Slicing with free5GC + UERANSIM + NS-3/5G-LENA

## Project Overview

A Docker-in-Docker (DinD) simulation environment for 5G smart manufacturing,
integrating:
- **free5GC v4.2.2** — 5G core network (AMF, SMF, UPF, NRF, NSSF, AUSF, UDM, UDR, PCF, CHF)
- **UERANSIM v3.2.8** — gNB and UE simulation (control + user plane)
- **NS-3.40 + 5G-LENA v2.6.y** — NR radio simulation (PHY/MAC, scheduler, channel)
- **Two network slices:**
  - **eMBB** (SST=1, SD=000001) — video traffic, 6 Mbps DL
  - **mMTC** (SST=3, SD=000002) — IoT telemetry, 1 Kbps UL per device

---

## Architecture (Docker-in-Docker)

```
HOST MACHINE
└── docker run → 5g-sim-host  (DinD, 16g RAM, 10 CPU, privileged)
    └── inner dockerd
        └── 5g-sim-net (10.100.0.0/16)
            ├── mongodb     10.100.0.10  — Subscriber data
            ├── nrf         10.100.0.20  — NRF
            ├── udr         10.100.0.21  — UDR
            ├── udm         10.100.0.22  — UDM
            ├── ausf        10.100.0.23  — AUSF
            ├── nssf        10.100.0.24  — NSSF
            ├── pcf         10.100.0.25  — PCF
            ├── chf         10.100.0.26  — CHF
            ├── amf         10.100.0.30  — AMF
            ├── smf-embb    10.100.0.31  — SMF (eMBB slice)
            ├── smf-mtc     10.100.0.32  — SMF (mMTC slice)
            ├── upf-embb    10.100.0.40  — UPF (eMBB, 10.61.0.0/16)
            ├── upf-mtc     10.100.0.41  — UPF (mMTC, 10.62.0.0/16)
            ├── webui       10.100.0.50  — WebUI (port 5000)
            ├── gnb         10.100.0.60  — gNB (UERANSIM)
            ├── ue-video-1  10.100.0.71  — eMBB UE 1
            ├── ue-video-2  10.100.0.72  — eMBB UE 2
            ├── ue-iot-1..3 10.100.0.81-83  — mMTC UEs
            ├── iperf-video 10.100.0.91  — iperf3 server (eMBB)
            ├── iperf-iot   10.100.0.92  — iperf3 server (mMTC)
            └── ns3         10.100.0.100 — NS-3 simulation (build + run)
```

---

## Current State

### DinD Core Network
- All 5G core NFs running (NRF, UDR, UDM, AUSF, NSSF, PCF, CHF, AMF, SMF, UPF)
- gNB connected to AMF; NG Setup successful
- 5 UEs registered with PDU sessions (2 video, 3 IoT)
- IP pools: 10.61.0.0/16 (eMBB) and 10.62.0.0/16 (mMTC)
- WebUI accessible at http://localhost:5001

### Data Plane Status
- Control plane: All UEs register and establish PDU sessions ✓
- User plane: SMF-UPF PFCP session establishment has issues in DinD environment.
  The SMF cannot send PFCP Session Establishment to the AN UPF (gNB) because
  UERANSIM gNB does not implement a PFCP endpoint. This prevents the free5GC
  UPF from forwarding user-plane traffic for the UEs.
- **Workaround**: NS-3 simulation uses its own internal EPC (NrPointToPointEpcHelper)
  for standalone operation. The full DinD integration (TAP bridge) is WIP.

### NS-3 Simulation
- Built and running successfully with 5G-LENA v2.6.y
- Two traffic classes: eMBB DL (6 Mbps) and mMTC UL (3 × 1 Kbps)

**Simulation Results (15s, 106 RBs, 20 MHz, 3.5 GHz):**

| Slice | Throughput | Avg Delay | Loss | Traffic Type |
|-------|-----------|-----------|------|-------------|
| eMBB  | 6.09 Mbps | 2 ms      | 0%   | Video DL (2 UEs, 6 Mbps total) |
| mMTC  | 3.2 Kbps  | 5 ms      | 0%   | IoT UL (3 UEs, 1 Kbps each) |

---

## Setup Instructions

### Prerequisites
- Linux host with Docker, user in `docker` group
- 16 GB RAM, 10+ CPUs recommended
- gtp5g kernel module loaded (v0.10.2, compatible with free5gc v4.2.2)

### Start from Scratch

```bash
# 1. Clone this repo
cd /path/to/final-project-5g

# 2. Create volumes
docker volume create 5g-sim-docker
docker volume create 5g-sim-mongo
docker volume create 5g-sim-ns3
docker volume create 5g-sim-results

# 3. Start outer container
docker run -d --name 5g-sim-host --privileged --cgroupns=host \
  --memory=16g --cpus=10 --memory-swap=16g --shm-size=1g \
  --sysctl net.ipv4.ip_forward=1 \
  -v $(pwd):/sim \
  -v 5g-sim-docker:/var/lib/docker \
  -v 5g-sim-mongo:/mongo-data \
  -v 5g-sim-ns3:/ns3-build \
  -v 5g-sim-results:/sim-results \
  -p 5001:5000 \
  docker:26-dind \
  sh -c "rm -f /var/run/docker.pid; modprobe udp_tunnel; modprobe gtp; \
         dockerd --host=unix:///var/run/docker.sock \
                 --storage-driver=overlay2 --iptables=true --ip-forward=true"

# 4. Wait for inner Docker daemon
until docker exec 5g-sim-host docker info >/dev/null 2>&1; do sleep 2; done

# 5. Create inner network
docker exec 5g-sim-host docker network create --driver bridge \
  --subnet 10.100.0.0/16 5g-sim-net
```

### Restart After Host Reboot

See `sim5gcontainerinsidecontainer.md` (lines 57-133) for the full restart
procedure, which includes reloading gtp5g, recreating the outer container,
re-patching CHF locality, and restarting NFs.

### Verify

```bash
# Check all containers
docker exec 5g-sim-host docker ps --format "table {{.Names}}\t{{.Status}}"

# Check UE PDU sessions
for ue in video-1 video-2 iot-1 iot-2 iot-3; do
  echo "=== ue-$ue ==="
  docker exec 5g-sim-host docker exec 5g-sim-ue-$ue \
    ip -brief addr show uesimtun0 2>&1 | head -1
done

# Check NRF registrations
docker exec 5g-sim-host docker exec 5g-sim-nrf \
  wget -qO- http://10.100.0.20:8000/nnrf-nfm/v1/nf-instances \
  | python3 -m json.tool | grep nfType
```

---

## NS-3 Simulation

### Run the simulation standalone

```bash
docker exec 5g-sim-host docker run --rm -v 5g-sim-ns3:/ns3-build ubuntu:22.04 \
  sh -c "apt-get update -qq && apt-get install -y -qq libgsl-dev libsqlite3-dev libxml2 \
  && LD_LIBRARY_PATH=/ns3-build/ns-3-dev/build/lib \
  /ns3-build/ns-3-dev/build/scratch/ns3.40-manufacturing-5g-optimized \
  --simTime=15"
```

### Parameters
- `--simTime`: Simulation duration in seconds (default: 15)
- `--numerology`: NR numerology (default: 1 = 30 kHz SCS)

### Modify the simulation
Edit `ns3-sim/scratch/manufacturing-5g.cc`, then rebuild:
```bash
cp ns3-sim/scratch/manufacturing-5g.cc /var/lib/docker/volumes/5g-sim-ns3/_data/ns-3-dev/scratch/
docker exec 5g-sim-host docker run --rm -v 5g-sim-ns3:/ns3-build ubuntu:22.04 \
  sh -c "apt-get update -qq && apt-get install -y -qq cmake ninja-build g++ python3 libboost-all-dev libgsl-dev libsqlite3-dev libeigen3-dev libxml2-dev ccache \
  && cd /ns3-build/ns-3-dev && python3 ns3 build scratch/manufacturing-5g"
```

---

## Debugging Notes

### PFCP Session Establishment Issue
The free5gc SMF v4.2.2 requires PFCP sessions with the "AN UPF" (gNB node).
UERANSIM gNB does not implement a PFCP listener, causing SMF to fail with:
```
Sending PFCP Session Modification Request to AN UPF error: received unexpected SEID
```

Resolution approaches:
1. Configure SMF userplane with gNB PFCP `nodeID`/`addr` pointing to the actual UPF
2. Switch to `free5gc/ueransim` which may have better compatibility
3. The NS-3 simulation is standalone and not affected by this issue

### UE IMEI Checksum
UERANSIM validates IMEI using the Luhn algorithm. All IMEIs in config files
have been corrected.

### MongoDB Subscriber Format
free5gc v4.2.2 uses a different subscriber data format than v3.4.1:
- `sequenceNumber` → `{sqn: hex_string}`
- `permanentKey` → `encPermanentKey` (encrypted hex)
- `opc` → `encOpcKey` (encrypted hex)

### UPF NAT
UPF requires iptables MASQUERADE for UE traffic:
```bash
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
iptables -I FORWARD 1 -j ACCEPT
```

---

## Teardown

```bash
# Stop inner containers
docker exec 5g-sim-host sh -c "docker rm -f \$(docker ps -aq)" 2>/dev/null || true

# Stop and remove outer container
docker stop 5g-sim-host && docker rm 5g-sim-host

# Remove volumes (deletes MongoDB data and NS-3 build cache)
docker volume rm 5g-sim-docker 5g-sim-mongo 5g-sim-ns3 5g-sim-results
```

---

## Files

| File | Purpose |
|------|---------|
| `sim5gcontainerinsidecontainer.md` | Complete setup guide (DinD) |
| `config/free5gc/*/` | free5gc NF configurations |
| `config/ueransim/*/` | UERANSIM gNB and UE configurations |
| `mongo-init/*.js` | MongoDB subscriber provisioning scripts |
| `ns3-sim/scratch/manufacturing-5g.cc` | NS-3 simulation source |
| `README.md` | This file |

---

## KPI Results (Planned vs Achieved)

| KPI | Target | eMBB Result | mMTC Result |
|-----|--------|-------------|-------------|
| Throughput | Measurable | 6.09 Mbps | 3.2 Kbps |
| E2E Latency | Measurable | 2 ms | 5 ms |
| Packet Delivery | 100% | 100% | 100% |
| PRB Utilization | Trackable | Within 106 PRBs/20 MHz | |

### Planned Experiments
- [ ] Baseline balanced load
- [ ] High video-demand stress (more UEs, higher bitrate)
- [ ] High IoT-density stress (10+ IoT UEs)
- [ ] Scheduler parameter sensitivity (OFDMA vs TDMA, PF vs RR)
- [ ] Multi-slice PRB partitioning
