import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import readline from "node:readline";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

async function readFileBytes(filePath) {
  try {
    return await fs.readFile(filePath);
  } catch {
    return Buffer.alloc(0);
  }
}

async function writeError(tmpDir, message) {
  await fs.writeFile(path.join(tmpDir, "err.txt"), message, "utf8");
}

function bytesFromHex(hex) {
  if (!hex || hex.length === 0) {
    return Buffer.alloc(0);
  }
  return Buffer.from(hex, "hex");
}

function bytesToHex(bytes) {
  return Buffer.from(bytes).toString("hex");
}

async function launchRuntimePage() {
  const [{ chromium }] = await Promise.all([
    import("playwright")
  ]);
  const htmlPath = path.resolve(__dirname, "../../runtime/browser/webrtc_datachannel_runtime.html");
  const browser = await chromium.launch({
    headless: true,
    args: ["--disable-features=WebRtcHideLocalIpsWithMdns"]
  });
  const page = await browser.newPage();
  await page.goto(pathToFileURL(htmlPath).href);
  await page.waitForFunction(() => !!window.v3BrowserWebrtcDatachannelRuntime);
  return { browser, page };
}

async function runSessionBridge() {
  const { browser, page } = await launchRuntimePage();
  const rl = readline.createInterface({
    input: process.stdin,
    crlfDelay: Infinity
  });
  async function send(reply) {
    process.stdout.write(`${JSON.stringify(reply)}\n`);
  }
  try {
    for await (const line of rl) {
      if (!line || line.length === 0) {
        continue;
      }
      let req;
      try {
        req = JSON.parse(line);
      } catch (err) {
        const text = err instanceof Error ? err.message : String(err);
        await send({ ok: false, errHex: bytesToHex(Buffer.from(text, "utf8")) });
        continue;
      }
      try {
        if (req.command === "open") {
          const result = await page.evaluate(async (input) => {
            return window.v3BrowserWebrtcDatachannelRuntime.openSession(input);
          }, {
            policyBytes: Array.from(bytesFromHex(req.policyHex || "")),
            label: Buffer.from(req.labelHex || "", "hex").toString("utf8")
          });
          await send({
            ok: true,
            signalHex: bytesToHex(result.signalBytes || [])
          });
          continue;
        }
        if (req.command === "exchange") {
          const result = await page.evaluate(async (input) => {
            return window.v3BrowserWebrtcDatachannelRuntime.exchangeSession(input);
          }, {
            protocolBytes: Array.from(bytesFromHex(req.protocolHex || "")),
            requestBytes: Array.from(bytesFromHex(req.requestHex || "")),
            responseBytes: Array.from(bytesFromHex(req.responseHex || ""))
          });
          await send({
            ok: true,
            responseHex: bytesToHex(result.responseBytes || [])
          });
          continue;
        }
        if (req.command === "close") {
          await page.evaluate(async () => {
            return window.v3BrowserWebrtcDatachannelRuntime.closeSession();
          });
          await send({ ok: true });
          break;
        }
        await send({
          ok: false,
          errHex: bytesToHex(Buffer.from("v3 browser webrtc: unknown session command", "utf8"))
        });
      } catch (err) {
        const text = err instanceof Error ? `${err.message}\n${err.stack || ""}` : String(err);
        await send({ ok: false, errHex: bytesToHex(Buffer.from(text, "utf8")) });
      }
    }
  } finally {
    rl.close();
    await browser.close();
  }
}

async function main() {
  if (process.argv[2] === "--session") {
    await runSessionBridge();
    return;
  }
  const tmpDir = process.argv[2];
  if (!tmpDir) {
    process.exitCode = 1;
    return;
  }
  try {
    const protocolBytes = await readFileBytes(path.join(tmpDir, "protocol.bin"));
    const policyBytes = await readFileBytes(path.join(tmpDir, "policy.bin"));
    const labelText = await fs.readFile(path.join(tmpDir, "label.txt"), "utf8");
    const requestBytes = await readFileBytes(path.join(tmpDir, "request.bin"));
    const responseBytes = await readFileBytes(path.join(tmpDir, "response.bin"));
    const { browser, page } = await launchRuntimePage();
    try {
      const result = await page.evaluate(async (input) => {
        return window.v3BrowserWebrtcDatachannelRuntime.runBridge(input);
      }, {
        protocolBytes: Array.from(protocolBytes),
        policyBytes: Array.from(policyBytes),
        label: labelText,
        requestBytes: Array.from(requestBytes),
        responseBytes: Array.from(responseBytes)
      });
      await fs.writeFile(path.join(tmpDir, "signal.bin"), Buffer.from(result.signalBytes));
      await fs.writeFile(path.join(tmpDir, "client_response.bin"), Buffer.from(result.responseBytes));
      await fs.writeFile(path.join(tmpDir, "err.txt"), "", "utf8");
    } finally {
      await browser.close();
    }
  } catch (err) {
    const text = err instanceof Error ? `${err.message}\n${err.stack || ""}` : String(err);
    await writeError(tmpDir, text);
    process.exitCode = 1;
  }
}

await main();
