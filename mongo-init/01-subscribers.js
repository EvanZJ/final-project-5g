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
