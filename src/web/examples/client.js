import { ChengWasmHost } from "../runtime/host_glue.js";

const host = new ChengWasmHost({
  defaultRootSelector: "#app"
});

async function boot() {
  await host.load("./client.wasm");
  host.init();
  const handle = host.mount();
  window.__chengUpdate = () => host.update(handle);
  window.__chengUnmount = () => host.unmount(handle);
}

boot().catch((err) => {
  console.error("Cheng WASM bootstrap failed", err);
});
