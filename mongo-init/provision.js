db = db.getSiblingDB('free5gc');

function provision(imsi, sst, sd, dnn, sstSdKey) {
  var ueId = "imsi-" + imsi;
  var plmn = "99970";
  var f    = { ueId: ueId, servingPlmnId: plmn };

  db["subscriptionData.authenticationData.authenticationSubscription"].updateOne(f, { $set: {
    ueId: ueId, servingPlmnId: plmn,
    authenticationMethod: "5G_AKA",
    authenticationManagementField: "8000",
    permanentKey: { permanentKeyValue: "465B5CE8B199B49FAA5F0A2EE238A6BC", encryptionKey: 0, encryptionAlgorithm: 0 },
    opc:          { opcValue: "E8ED289DEBA952E4283B54E88E6183CA",  encryptionKey: 0, encryptionAlgorithm: 0 },
    milenage:     { op: { opValue: "", encryptionKey: 0, encryptionAlgorithm: 0 } },
    sequenceNumber: "16f3b3f70fc2"
  }}, { upsert: true });

  db["subscriptionData.provisionedData.amData"].updateOne(f, { $set: {
    ueId: ueId, servingPlmnId: plmn,
    gpsis: ["msisdn-09" + imsi.slice(-8)],
    nssai: { defaultSingleNssais: [{ sst: sst, sd: sd }], singleNssais: [{ sst: sst, sd: sd }] },
    subscribedUeAmbr: { uplink: "1 Gbps", downlink: "2 Gbps" }
  }}, { upsert: true });

  var dnnCfg = {};
  dnnCfg[dnn] = {
    pduSessionTypes: { defaultSessionType: "IPV4", allowedSessionTypes: ["IPV4"] },
    sscModes:        { defaultSscMode: "SSC_MODE_1", allowedSscModes: ["SSC_MODE_2","SSC_MODE_3"] },
    sessionAmbr:     { uplink: "200 Mbps", downlink: "100 Mbps" },
    "5gQosProfile":  { "5qi": 9, arp: { priorityLevel: 8, preemptCap: "NOT_PREEMPT", preemptVuln: "NOT_PREEMPTABLE" }, priorityLevel: 8 }
  };
  db["subscriptionData.provisionedData.smData"].updateOne(
    { ueId: ueId, servingPlmnId: plmn, singleNssai: { sst: sst, sd: sd } },
    { $set: { ueId: ueId, servingPlmnId: plmn, singleNssai: { sst: sst, sd: sd }, dnnConfigurations: dnnCfg } },
    { upsert: true }
  );

  var snssaiInfos = {};
  snssaiInfos[sstSdKey] = { dnnInfos: [{ dnn: dnn }] };
  db["subscriptionData.provisionedData.smfSelectionSubscriptionData"].updateOne(f, { $set: {
    ueId: ueId, servingPlmnId: plmn, subscribedSnssaiInfos: snssaiInfos
  }}, { upsert: true });

  db["policyData.ues.amData"].updateOne({ ueId: ueId }, { $set: {
    ueId: ueId, subscCats: ["free5gc"]
  }}, { upsert: true });

  var smPolicyData = {};
  smPolicyData[sstSdKey] = { snssai: { sst: sst, sd: sd }, smPolicyDnnData: {} };
  smPolicyData[sstSdKey].smPolicyDnnData[dnn] = { dnn: dnn };
  db["policyData.ues.smData"].updateOne({ ueId: ueId }, { $set: {
    ueId: ueId, smPolicySnssaiData: smPolicyData
  }}, { upsert: true });

  print("Provisioned: " + ueId);
}

provision("999700000000001", 1, "000001", "video", "01000001");
provision("999700000000002", 1, "000001", "video", "01000001");
provision("999700000000101", 3, "000002", "iot",   "03000002");
provision("999700000000102", 3, "000002", "iot",   "03000002");
provision("999700000000103", 3, "000002", "iot",   "03000002");
print("Done.");
