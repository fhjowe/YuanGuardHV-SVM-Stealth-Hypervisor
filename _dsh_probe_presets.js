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
  const list = await rpc("agentPreset.list", {});
  console.log("=== agentPreset.list ===");
  console.log(JSON.stringify(list.result ?? list, null, 2));

  const sess = await rpc("session.list", {});
  const items = sess?.result?.value?.items ?? [];
  console.log("\n=== session.list (current view) ===");
  for (const it of items) {
    console.log(`${it.sessionId}  blank=${it.blank} preset=${it.agentPreset ?? "(none)"} running=${it.running}`);
  }
}
main().catch(e => console.error("FATAL", e));
