const BASE = "http://127.0.0.1:52673";
let seq = 0;
async function rpc(method, payload) {
  const rpcId = `probe-${++seq}`;
  const res = await fetch(BASE + "/api/" + method, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ type: "client-request", rpcId, method, payload: payload ?? {} }),
  });
  if (!res.ok) return { httpError: res.status, body: await res.text() };
  return await res.json();
}

async function main() {
  const sid = process.argv[2] || "session-78254cce-87d1-4423-9f52-ba3de1dafd20";
  const target = process.argv[3] || "standard";

  console.log(`Trying to switch ${sid} -> ${target}`);
  const r = await rpc("agentPreset.select", { sessionId: sid, agentPreset: target });
  console.log(JSON.stringify(r, null, 2));

  const s = await rpc("session.list", {});
  const item = (s?.result?.value?.items ?? []).find(i => i.sessionId === sid);
  console.log("after switch, session view:", JSON.stringify(item, null, 2));
}
main().catch(e => console.error("FATAL", e));
