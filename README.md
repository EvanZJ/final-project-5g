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

## Experimental Results

All experiments run with NS-3.40 + 5G-LENA v2.6.y at 3.5 GHz, 15 kHz SCS, UMa scenario,
106 RBs (20 MHz) or 216 RBs (40 MHz), TDMA RR scheduler unless noted.
Simulation duration: 10 seconds per run. **Zero packet loss across ALL experiments.**

### Experiment 1: Baseline Balanced Load
- 2 video UEs (6 Mbps DL each) + 3 IoT UEs (1 Kbps UL each)
- 20 MHz BW, TdmaRR

| Slice | Offered | Throughput | Avg Delay | Loss |
|-------|---------|-----------|-----------|------|
| eMBB  | 12 Mbps | 12.33 Mbps | 2 ms | 0% |
| mMTC  | 3 Kbps  | 3.25 Kbps | 5 ms | 0% |

### Experiment 2: High Video-Demand Stress
- 4 video UEs (10 Mbps DL each) + 3 IoT UEs (1 Kbps UL each)
- 20 MHz BW, TdmaRR

| Slice | Offered | Throughput | Avg Delay | Loss |
|-------|---------|-----------|-----------|------|
| eMBB  | 40 Mbps | 41.11 Mbps | 2 ms | 0% |
| mMTC  | 3 Kbps  | 3.25 Kbps | 5 ms | 0% |

### Experiment 3: High IoT-Density Stress
- 2 video UEs (6 Mbps DL each) + **10 IoT UEs** (1 Kbps UL each)
- 20 MHz BW, TdmaRR

| Slice | Offered | Throughput | Avg Delay | Loss |
|-------|---------|-----------|-----------|------|
| eMBB  | 12 Mbps | 12.33 Mbps | 2 ms | 0% |
| mMTC  | 10 Kbps | 10.84 Kbps | 5.2 ms | 0% |

### Experiment 4: Scheduler Comparison (OfdmaPF)
- 2 video UEs (6 Mbps DL each) + 3 IoT UEs
- 20 MHz BW, **OfdmaPF** scheduler

| Slice | Offered | Throughput | Avg Delay | Loss |
|-------|---------|-----------|-----------|------|
| eMBB  | 12 Mbps | 12.33 Mbps | 2 ms | 0% |
| mMTC  | 3 Kbps  | 3.25 Kbps | **8 ms** | 0% |

### Experiment 5: Scheduler Comparison (OfdmaRR)
- 2 video UEs (6 Mbps DL each) + 3 IoT UEs
- 20 MHz BW, **OfdmaRR** scheduler

| Slice | Offered | Throughput | Avg Delay | Loss |
|-------|---------|-----------|-----------|------|
| eMBB  | 12 Mbps | 12.33 Mbps | 2 ms | 0% |
| mMTC  | 3 Kbps  | 3.25 Kbps | **8 ms** | 0% |

### Experiment 6: 40 MHz Bandwidth (Baseline × 2)
- 2 video UEs (20 Mbps DL each) + 3 IoT UEs
- **40 MHz BW** (216 RBs), TdmaRR

| Slice | Offered | Throughput | Avg Delay | Loss |
|-------|---------|-----------|-----------|------|
| eMBB  | 40 Mbps | 41.11 Mbps | 2 ms | 0% |
| mMTC  | 3 Kbps  | 3.25 Kbps | 5 ms | 0% |

### Experiment 7: Extreme Video (40 MHz, 4 UEs × 20 Mbps)
- 4 video UEs (20 Mbps DL each) + 3 IoT UEs
- **40 MHz BW** (216 RBs), TdmaRR

| Slice | Offered | Throughput | Avg Delay | Loss |
|-------|---------|-----------|-----------|------|
| eMBB  | 80 Mbps | 82.22 Mbps | 2 ms | 0% |
| mMTC  | 3 Kbps  | 3.25 Kbps | 5 ms | 0% |

### Key Findings
1. **Zero packet loss across all scenarios** — 5G-LENA scheduler handles traffic well
2. **eMBB scales linearly** — doubling bandwidth or UEs doubles throughput
3. **mMTC delay increases with OFDMA schedulers** — TDMA is better for low-rate IoT
4. **No performance degradation under stress** — 10 IoT UEs + 2 video UEs cause no issues
5. **40 MHz doubles capacity** — 80+ Mbps achievable for video

### Future Experiments
- Multi-slice PRB partitioning (e.g., 70 RBs eMBB + 36 RBs mMTC)
- Different traffic models (VBR video, bursty IoT)
- Mobility scenarios (moving UEs on factory floor)
- Multi-cell deployment

---

## Debugging Report

This section documents every significant problem encountered during the project,
its root cause, and the resolution applied. Problems are grouped by subsystem.

### 1. Network Stack — gtp5g Kernel Module

| Problem | Root Cause | Resolution |
|---------|-----------|------------|
| **gtp5g version mismatch** — UPF crashes: `gtp5g version(0.10.2) should be 0.8.6 <= verion < 0.9.0` | free5gc v3.4.1 UPF checks gtp5g version range (0.8.6–0.9.0). Host had gtp5g v0.10.2 (from existing free5gc-compose setup). Cannot load two versions of the same kernel module. | Upgraded all free5gc images from v3.4.1 to v4.2.2, which supports gtp5g v0.10.2. |
| **UPF `forwarder: gtpu` rejected** — `gtpu does not validate as in(gtp5g)` | free5gc v3.4.1 UPF only accepts `forwarder: gtp5g`. The user-space `gtpu` forwarder is not implemented. | Reverted to `forwarder: gtp5g` and used v4.2.2 UPF image. |

### 2. Core Network — Configuration Format

| Problem | Root Cause | Resolution |
|---------|-----------|------------|
| **UDR crash on startup** — `invalid DbConnectorType: non zero value required` | free5gc v4.2.2 UDR config requires explicit `dbConnectorType: mongodb` field. The v3.4.1-style config omitted this. | Added `dbConnectorType: mongodb` to UDR config. |
| **UDM returns 500 on authentication** — `json: cannot unmarshal string into Go struct field AuthenticationSubscription.sequenceNumber of type models.SequenceNumber` | free5gc v4.2.2 changed `sequenceNumber` format from a hex string to a struct `{sqn: "hex"}`. The old `permanentKey`/`opc` struct fields were replaced with `encPermanentKey`/`encOpcKey` (encrypted strings). | Ran MongoDB migration: converted `sequenceNumber` → `{sqn: ...}`, renamed `permanentKey.permanentKeyValue` → `encPermanentKey`, `opc.opcValue` → `encOpcKey`, added `protectionParameterId: "0"`. |
| **SMF nil-pointer crash on startup** — `panic: runtime error: invalid memory address or nil pointer dereference` in `nrf_service.go:157` | SMF v4.2.2 expects `plmnList` in config. The v3.4.1 config had no `plmnList`, causing nil pointer in NfProfile construction. | Added `plmnList: [{mcc: "999", mnc: "70"}]` to SMF configs. |
| **SMF crash: `invalid links: non zero value required`** | SMF v4.2.2 validates that the `links` section has at least one entry. Removing the AN-to-UPF link to bypass PFCP issues caused validation failure. | Restored `links: [{A: gNB1, B: UPF}]` (the AN/UPF topology is required). |
| **SMF log: `Can not get interface`** | SMF PFCP module searches for a local network interface matching `pfcp.externalAddr`. Using bare IPs (`10.100.0.31`) caused interface lookup to fail inside Docker. | Switched `pfcp.externalAddr` and `nodeID` to Docker hostnames (`5g-sim-smf-embb`) which resolve correctly. |
| **SMF never sends PFCP Session Establishment** — always sends Session Modification to AN UPF with SEID=0 | SMF tries to establish PFCP with the AN (gNB) first. UERANSIM gNB has no PFCP listener, so the establishment fails silently and the SMF never proceeds to establish sessions with the actual UPF. | Partial: Added `nodeID`/`addr` to gNB1 in SMF userplane config pointing to the actual UPF's PFCP address. This lets PFCP Association succeed, but per-UE session establishment still requires further investigation. |
| **UPF data plane broken: 0 RX on `upfgtp` interface** | Consequence of no PFCP sessions — the UPF has no PDR/FAR rules for any UE traffic, so GTP-U packets from the gNB are dropped. | Workaround: NS-3 simulation runs standalone with its own internal EPC (`NrPointToPointEpcHelper`), bypassing the external UPF. |
| **CHF nil pointer in SMF** — `SelectedCHFProfile stays nil` | CHF v3.4.1 registeres in NRF without a `locality` field. SMF's NRF discovery with `preferred-locality=area1` returns zero matches, causing nil pointer when SMF tries to send charging requests. | Patched CHF NfProfile in MongoDB: `db.NfProfile.updateOne({nfType:"CHF"},{$set:{locality:"area1"}})`. Must be re-applied after every CHF restart. |

### 3. UERANSIM — UE Registration

| Problem | Root Cause | Resolution |
|---------|-----------|------------|
| **UE registration rejected with `CONGESTION`** | AUSF returned HTTP 500 to AMF because UDM could not authenticate the UE. Root chain: AMF → AUSF → UDM → UDR → MongoDB. UDR's missing `dbConnectorType` caused UDR to crash, making UDM unreachable. | Fixed UDR config (see above). The 500 error cascaded through the entire auth chain. |
| **UE registration rejected: `invalid IMEI checksum`** | AMF decodes the UE's IMEI and validates it using the Luhn algorithm. The UERANSIM UE config used IMEIs with incorrect check digits. | Recalculated check digits for every IMEI using the standard GSM Luhn algorithm. Example: `356938035643803` → `356938035643809`. |
| **gNB-UE RLS incompatibility** — UE reports `no cell is in coverage` | Switching gNB from `towards5gs/ueransim-gnb` to `free5gc/ueransim:latest` broke RLS (Radio Link Simulation) compatibility with the `towards5gs/ueransim-ue` UE containers. | Reverted gNB to `towards5gs/ueransim-gnb`. Both images are v3.2.8 but may have different RLS implementations. |

### 4. NS-3 / 5G-LENA Simulation

| Problem | Root Cause | Resolution |
|---------|-----------|------------|
| **Build failure: `/usr/bin/ccache: not found`** | NS-3 build system uses `ccache` for caching object files. The build container's minimal `ubuntu:22.04` image lacks this package. | Added `ccache` to apt install list. |
| **Build failure: CMake regeneration fails with ninja** | Incremental builds (`python3 ns3 build scratch/...`) trigger CMake rerun, which fails if boost headers are missing. | Installed `libboost-all-dev` in the build container. |
| **Runtime crash: `m_channelBandwidth == 0`** | 5G-LENA's PHY layer checks that channel bandwidth is set before computing spectrum models. The BWP bandwidth configuration was correct, but the per-device PHY objects were not synchronized with the BWP config. | Added `DynamicCast<NrGnbNetDevice>(*it)->UpdateConfig()` and `DynamicCast<NrUeNetDevice>(*it)->UpdateConfig()` calls after device installation. |
| **Runtime crash: `Failed to bind socket`** | `PacketSinkHelper` tried to bind a UDP socket at 0.1s simulation time, but the UE's network interface (assigned by `AssignUeIpv4Address`) was not ready. | Switched to `UdpServerHelper` (which handles timing better) and delayed app start to 1.5s. |
| **Incorrect bandwidth calculation** | Original code computed total bandwidth from PRB counts (`(prbEmbb + prbMmtc) * 12 * 15kHz`). 5G-LENA's `SimpleOperationBandConf` expects the channel bandwidth in Hz directly and computes PRBs internally. | Use fixed bandwidth values (`20e6` or `40e6`) and let the model compute PRBs. Made bandwidth a command-line parameter. |
| **mMTC flow classification: showed 0 Mbps** | Flow classifier checked `t.sourcePort == iotPort`, but UDP clients use ephemeral ports (49153+). | Changed to `t.destinationPort == iotPort` for UL flows. |

### 5. Docker-in-Docker Infrastructure

| Problem | Root Cause | Resolution |
|---------|-----------|------------|
| **Inner volume vs. outer volume confusion** | The outer container mounts host volumes (`-v 5g-sim-ns3:/ns3-build`). Inner Docker containers also mount `5g-sim-ns3` — but this is the *inner* volume stored at `/var/lib/docker/volumes/5g-sim-ns3/_data/` inside the outer container, NOT the outer host volume. | Access inner build artifacts via `docker exec 5g-sim-host ls /var/lib/docker/volumes/5g-sim-ns3/_data/`. Use the `5g-sim-results` volume for persistent output. |
| **NS-3 build lost on every container restart** | Ephemeral build containers (`docker run --rm`) write build artifacts to the volume correctly, but the build environment (apt packages) is lost. | Create a reusable build script that installs all deps and rebuilds. Cache package downloads via Docker layer caching. |
| **Slow build cycle (~10 min per rebuild)** | Each build requires apt-get install of 15+ packages (~200 MB). | Use a custom Docker image with pre-installed deps for production use. For development, accept the overhead. |
| **gtp5g kernel module scope** | gtp5g is a kernel module loaded on the **host**. The outer (DinD) container inherits it. All inner containers share the same kernel module version. Cannot run two different gtp5g versions simultaneously. | Ensured all inner free5gc images (v4.2.2) are compatible with the host's gtp5g v0.10.2. |

### 6. Network Configuration

| Problem | Root Cause | Resolution |
|---------|-----------|------------|
| **iperf3 server unreachable from UE via 5G path** | UEs have two interfaces: `eth0` (Docker bridge) and `uesimtun0` (5G tunnel). The default route points to `eth0`, so traffic to the iperf server (on the same Docker bridge) bypasses the 5G data path entirely. | Added static routes: `ip route add 10.100.0.91/32 dev uesimtun0 metric 100`. However, data still doesn't flow because the underlying UPF data plane is broken (see PFCP issue above). |
| **iperf-video and UE IoT 10 IP conflict** | Both assigned `10.100.0.90` in the original guide. | Shifted iperf servers: iperf-video at `.91`, iperf-iot at `.92`. |
| **UPF iptables NAT missing** | The UPF config has `natifname: eth0`, but v4.2.2 UPF does not automatically add iptables MASQUERADE rules for UE subnet. | Added `iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE && iptables -I FORWARD 1 -j ACCEPT` to UPF startup command. |

### 7. Summary of Root Causes

The debugging effort revealed a clear pattern: most issues stemmed from **version mismatches** between the original project guide (written for free5gc v3.4.1) and the actual system (which had free5gc v4.2.2 + gtp5g v0.10.2 pre-installed). Key changes between v3.4.1 and v4.2.2:

| Change | Impact |
|--------|--------|
| gtp5g compatibility | v3.4.1 → gtp5g 0.8.x; v4.2.2 → gtp5g 0.10.x |
| Config schema additions | `dbConnectorType`, `plmnList`, `links` validation |
| MongoDB schema changes | `sequenceNumber` format, credential field names |
| PFCP behaviour | v4.2.2 SMF more strict about AN PFCP establishment |
| UPF NAT | v4.2.2 requires manual iptables configuration |
