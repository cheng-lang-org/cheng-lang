const DEV_ENDPOINT = "/__cheng_dev_poll";
const OVERLAY_ID = "__cheng_dev_overlay";
const POLL_INTERVAL = 800;

let overlay = null;
let overlayText = null;
let pendingMessage = "";

function ensureOverlay() {
  if (overlay && overlayText) {
    return;
  }
  const create = () => {
    if (overlay && overlayText) {
      return;
    }
    overlay = document.createElement("div");
    overlay.id = OVERLAY_ID;
    overlay.style.position = "fixed";
    overlay.style.inset = "0";
    overlay.style.zIndex = "2147483647";
    overlay.style.background = "rgba(18, 18, 18, 0.92)";
    overlay.style.color = "#f5f5f5";
    overlay.style.fontFamily = "ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, \"Liberation Mono\", \"Courier New\", monospace";
    overlay.style.padding = "24px";
    overlay.style.display = "none";
    overlay.style.overflow = "auto";

    const title = document.createElement("div");
    title.textContent = "Cheng Web Build Error";
    title.style.fontSize = "18px";
    title.style.fontWeight = "600";
    title.style.marginBottom = "12px";

    overlayText = document.createElement("pre");
    overlayText.style.whiteSpace = "pre-wrap";
    overlayText.style.margin = "0";
    overlayText.style.lineHeight = "1.4";

    overlay.appendChild(title);
    overlay.appendChild(overlayText);

    document.body.appendChild(overlay);

    if (pendingMessage) {
      overlayText.textContent = pendingMessage;
      overlay.style.display = "block";
    }
  };

  if (document.body) {
    create();
  } else {
    document.addEventListener("DOMContentLoaded", create, { once: true });
  }
}

function normalizeMessage(data) {
  if (!data) {
    return "Build failed.";
  }
  try {
    const parsed = JSON.parse(data);
    if (typeof parsed === "string") {
      return parsed;
    }
    if (parsed && typeof parsed === "object") {
      if (typeof parsed.message === "string") return parsed.message;
      if (typeof parsed.error === "string") return parsed.error;
      return JSON.stringify(parsed, null, 2);
    }
  } catch {
    return data;
  }
  return String(data);
}

function showError(message) {
  const text = normalizeMessage(message);
  ensureOverlay();
  pendingMessage = text;
  if (overlay && overlayText) {
    overlayText.textContent = text;
    overlay.style.display = "block";
  }
}

function hideError() {
  pendingMessage = "";
  if (overlay) {
    overlay.style.display = "none";
  }
}

let lastVersion = 0;
let pollTimer = null;

function refreshStyles(version) {
  const links = document.querySelectorAll('link[rel="stylesheet"]');
  if (!links || links.length === 0) {
    return false;
  }
  const cacheKey = "__cwc_css";
  links.forEach((link) => {
    const href = link.getAttribute("href");
    if (!href) return;
    try {
      const url = new URL(href, window.location.origin);
      url.searchParams.set(cacheKey, String(version));
      link.setAttribute("href", url.toString());
    } catch {
      link.setAttribute("href", `${href}?${cacheKey}=${version}`);
    }
  });
  return true;
}

async function poll() {
  try {
    const res = await fetch(DEV_ENDPOINT, { cache: "no-store" });
    if (!res.ok) {
      throw new Error("dev poll failed");
    }
    const data = await res.json();
    if (data && typeof data.error === "string" && data.error.length > 0) {
      showError(data.error);
    } else {
      hideError();
      if (data && typeof data.version === "number") {
        const kind = data.kind === "css" ? "css" : "full";
        if (data.version !== lastVersion) {
          const next = data.version;
          if (lastVersion !== 0) {
            lastVersion = next;
            if (kind === "css") {
              if (!refreshStyles(next)) {
                window.location.reload();
              }
            } else {
              window.location.reload();
            }
            return;
          }
          lastVersion = next;
        }
      }
    }
  } catch {
    // ignore transient poll errors
  }
  pollTimer = setTimeout(poll, POLL_INTERVAL);
}

if (typeof window !== "undefined") {
  if (pollTimer) {
    clearTimeout(pollTimer);
  }
  poll();
}
