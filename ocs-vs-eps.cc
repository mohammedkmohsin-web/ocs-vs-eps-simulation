// ============================================================================
//  ocs-vs-eps.cc
//  Performance Evaluation of Reconfigurable Optical Circuit Switching (OCS)
//  vs Electrical Packet Switching (EPS) for Collective AI/ML Traffic.
//
//  ns-3 (release 3.45).  Place this file in  scratch/  and build with:
//      ./ns3 build ocs-vs-eps
//  Run the executable DIRECTLY (not via ./ns3 run) so stdout is captured:
//      ./build/scratch/ns3.45-ocs-vs-eps-optimized --help
//
//  DESIGN NOTES
//  ------------
//  * One phase is measured per program run (--phaseId). A driver script loops
//    over all phases and sums phase times into the collective completion time.
//    This keeps every phase in a clean simulation instance and avoids stale
//    UDP sockets that corrupted earlier multi-phase-in-one-run designs.
//  * Each active (src,dst) pair sends a constant-rate UDP flow at the host link
//    rate for a short measurement window. We read the per-flow throughput the
//    fabric sustains under contention, then the driver computes
//        phase_time = chunk_bits / min_throughput
//    and CCT = sum(phase_time) + reconfiguration cost.
//  * EPS: two-tier leaf-spine, ECMP across spines. Oversubscription is set by
//    the spine link rate (--spineRateGbps).
//  * OCS: reconfigurable crossbar giving each pair a dedicated full-rate circuit.
//    --ocsRadix limits the switch radix (0 = ideal). (Radix results in the paper
//    are produced analytically on top of the ideal-crossbar throughput.)
//  * --seed permutes the host->leaf placement, the source of statistical
//    variation across the five repetitions reported in the paper.
// ============================================================================
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include <vector>
#include <string>
#include <cmath>
#include <limits>
#include <random>

using namespace ns3;
NS_LOG_COMPONENT_DEFINE ("OcsVsEps");

struct Cfg {
  std::string pattern = "all_to_all";   // all_to_all | ring_allreduce
  std::string paradigm = "eps";         // eps | ocs
  uint32_t N = 32, nodesPerLeaf = 8, oversub = 4;
  double linkRateGbps = 100.0, spineRateGbps = 100.0;
  double totalPerNodeGB = 1.0, TrecfgUs = 200.0, linkDelayUs = 1.0;
  double windowMs = 2.0;
  int phaseId = 0;                      // phase to measure; -1 => print phase count
  uint32_t seed = 1;                    // permutes host placement
  uint32_t ocsRadix = 0;                // 0 = ideal crossbar
  uint64_t Chunk () const { return (uint64_t)((totalPerNodeGB * 1e9) / N); }
};

using Pair = std::pair<uint32_t,uint32_t>;
using Phase = std::vector<Pair>;

// Build the list of communicating pairs for each phase of the collective.
static std::vector<Phase> BuildPhases (const Cfg& c, bool& reconfigEvery) {
  std::vector<Phase> phases;
  if (c.pattern == "ring_allreduce") {
    Phase ring;
    for (uint32_t i = 0; i < c.N; ++i) ring.push_back({i,(i+1)%c.N});
    for (uint32_t s = 0; s < 2*(c.N-1); ++s) phases.push_back(ring);
    reconfigEvery = false;              // ring keeps its shape: reconfigure once
  } else {
    // All-to-All via symmetric round-robin. Works for any N (even non powers of 2),
    // unlike an XOR schedule which breaks for non-power-of-2 sizes.
    uint32_t M = c.N + (c.N % 2);
    for (uint32_t r = 0; r < M - 1; ++r) {
      Phase ph;
      for (uint32_t i = 0; i < M/2; ++i) {
        uint32_t a = (i == 0) ? 0 : (i + r) % (M - 1) + 1;
        uint32_t b = (M - 1 - i + r) % (M - 1) + 1;
        if (i == 0) b = (r % (M - 1)) + 1;
        if (a < c.N && b < c.N && a != b) ph.push_back({a, b});
      }
      if (!ph.empty()) phases.push_back(ph);
    }
    reconfigEvery = true;               // permutation changes every phase
  }
  return phases;
}

namespace {
  Cfg g_c;
  NodeContainer g_hosts;
  std::vector<Ipv4Address> g_addr;
  std::vector<Ptr<PacketSink>> g_sinks;
  std::vector<uint32_t> g_perm;         // random host -> slot permutation
}

// Random placement permutation, seeded, so runs differ in which pairs cross the spine.
static void BuildPerm (const Cfg& c) {
  g_perm.resize(c.N);
  for (uint32_t i=0;i<c.N;i++) g_perm[i]=i;
  std::mt19937 rng(c.seed*2654435761u + 12345u);
  for (uint32_t i=c.N;i-->1;) {
    std::uniform_int_distribution<uint32_t> d(0,i);
    std::swap(g_perm[i],g_perm[d(rng)]);
  }
}

static void BuildTopology (const Cfg& c) {
  BuildPerm(c);
  uint32_t nLeaf = (c.N + c.nodesPerLeaf - 1) / c.nodesPerLeaf;
  uint32_t nSpine = std::max<uint32_t>(1,(uint32_t)std::round((double)c.nodesPerLeaf/(double)c.oversub));
  g_hosts.Create(c.N);
  NodeContainer leaves; leaves.Create(nLeaf);
  InternetStackHelper stack; stack.Install(g_hosts); stack.Install(leaves);

  PointToPointHelper p2pHost;
  p2pHost.SetDeviceAttribute("DataRate", StringValue(std::to_string((uint64_t)c.linkRateGbps)+"Gbps"));
  p2pHost.SetChannelAttribute("Delay", TimeValue(MicroSeconds(c.linkDelayUs)));
  p2pHost.SetQueue("ns3::DropTailQueue<Packet>","MaxSize",StringValue("2000p"));

  Ipv4AddressHelper ip; ip.SetBase("10.0.0.0","255.255.255.0");
  g_addr.resize(c.N);
  for (uint32_t h = 0; h < c.N; ++h) {
    uint32_t lf = g_perm[h] / c.nodesPerLeaf;         // placement permutation
    NetDeviceContainer dev = p2pHost.Install(g_hosts.Get(h), leaves.Get(lf));
    Ipv4InterfaceContainer ifc = ip.Assign(dev);
    g_addr[h] = ifc.GetAddress(0);
    ip.NewNetwork();
  }

  if (c.paradigm == "eps") {
    NodeContainer spines; spines.Create(nSpine); stack.Install(spines);
    PointToPointHelper p2pSpine;
    p2pSpine.SetDeviceAttribute("DataRate", StringValue(std::to_string((uint64_t)c.spineRateGbps)+"Gbps"));
    p2pSpine.SetChannelAttribute("Delay", TimeValue(MicroSeconds(c.linkDelayUs)));
    p2pSpine.SetQueue("ns3::DropTailQueue<Packet>","MaxSize",StringValue("2000p"));
    for (uint32_t lf = 0; lf < nLeaf; ++lf)
      for (uint32_t sp = 0; sp < nSpine; ++sp) {
        NetDeviceContainer dev = p2pSpine.Install(leaves.Get(lf), spines.Get(sp));
        ip.Assign(dev); ip.NewNetwork();
      }
  } else {
    PointToPointHelper p2pOcs;
    p2pOcs.SetDeviceAttribute("DataRate", StringValue(std::to_string((uint64_t)c.linkRateGbps)+"Gbps"));
    p2pOcs.SetChannelAttribute("Delay", TimeValue(MicroSeconds(c.linkDelayUs)));
    p2pOcs.SetQueue("ns3::DropTailQueue<Packet>","MaxSize",StringValue("2000p"));
    uint32_t R = c.ocsRadix;
    if (R == 0 || R >= nLeaf) {                        // ideal crossbar
      NodeContainer xbar; xbar.Create(1); stack.Install(xbar);
      for (uint32_t lf = 0; lf < nLeaf; ++lf) {
        NetDeviceContainer dev = p2pOcs.Install(leaves.Get(lf), xbar.Get(0));
        ip.Assign(dev); ip.NewNetwork();
      }
    } else {                                           // limited optical interconnect
      NodeContainer ospines; ospines.Create(R); stack.Install(ospines);
      for (uint32_t lf = 0; lf < nLeaf; ++lf)
        for (uint32_t sp = 0; sp < R; ++sp) {
          NetDeviceContainer dev = p2pOcs.Install(leaves.Get(lf), ospines.Get(sp));
          ip.Assign(dev); ip.NewNetwork();
        }
    }
  }
  Config::SetDefault("ns3::Ipv4GlobalRouting::RandomEcmpRouting", BooleanValue(true));
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();
}

int main (int argc, char* argv[]) {
  CommandLine cmd(__FILE__);
  cmd.AddValue("pattern","all_to_all | ring_allreduce",g_c.pattern);
  cmd.AddValue("paradigm","eps | ocs",g_c.paradigm);
  cmd.AddValue("N","number of accelerators",g_c.N);
  cmd.AddValue("nodesPerLeaf","hosts per leaf",g_c.nodesPerLeaf);
  cmd.AddValue("oversub","EPS oversubscription factor",g_c.oversub);
  cmd.AddValue("linkRateGbps","host link rate (Gbps)",g_c.linkRateGbps);
  cmd.AddValue("spineRateGbps","spine link rate (Gbps)",g_c.spineRateGbps);
  cmd.AddValue("TrecfgUs","reconfiguration latency (us)",g_c.TrecfgUs);
  cmd.AddValue("totalPerNodeGB","data per node (GB)",g_c.totalPerNodeGB);
  cmd.AddValue("windowMs","measurement window (ms)",g_c.windowMs);
  cmd.AddValue("phaseId","phase to measure (-1 => print NPHASES)",g_c.phaseId);
  cmd.AddValue("seed","placement seed",g_c.seed);
  cmd.AddValue("ocsRadix","optical switch radix (0 = ideal)",g_c.ocsRadix);
  cmd.Parse(argc, argv);

  bool reconfigEvery = true;
  std::vector<Phase> phases = BuildPhases(g_c, reconfigEvery);

  if (g_c.phaseId < 0) {                               // report structure to driver
    std::cout << "NPHASES=" << phases.size()
              << " RECONFIG_EVERY=" << (reconfigEvery?1:0) << std::endl;
    return 0;
  }
  if ((uint32_t)g_c.phaseId >= phases.size()) {
    std::cout << "PHASE_THR_BPS=0" << std::endl;
    return 0;
  }

  BuildTopology(g_c);
  const Phase& ph = phases[g_c.phaseId];
  double W = g_c.windowMs * 1e-3;
  double now = 0.001;
  for (uint32_t idx = 0; idx < ph.size(); ++idx) {
    uint32_t s = ph[idx].first, d = ph[idx].second;
    uint16_t port = (uint16_t)(2000 + idx);
    PacketSinkHelper sinkH ("ns3::UdpSocketFactory",
                            InetSocketAddress (Ipv4Address::GetAny (), port));
    ApplicationContainer sa = sinkH.Install (g_hosts.Get (d));
    sa.Start (Seconds (0.0));
    g_sinks.push_back (DynamicCast<PacketSink> (sa.Get (0)));
    OnOffHelper onoff ("ns3::UdpSocketFactory",
                       InetSocketAddress (g_addr[d], port));
    onoff.SetConstantRate (DataRate ((uint64_t)(g_c.linkRateGbps * 1e9)), 1400);
    onoff.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1000]"));
    onoff.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer ca = onoff.Install (g_hosts.Get (s));
    ca.Start (Seconds (now));
    ca.Stop (Seconds (now + W));
  }

  Simulator::Stop (Seconds (now + W + 0.01));
  Simulator::Run ();

  double minThr = std::numeric_limits<double>::max();
  for (auto& sk : g_sinks) {
    double thr = (sk->GetTotalRx() * 8.0) / W;         // bits/s delivered
    if (thr < minThr) minThr = thr;
  }
  Simulator::Destroy ();
  std::cout << "PHASE_THR_BPS=" << (uint64_t)minThr << std::endl;
  return 0;
}
