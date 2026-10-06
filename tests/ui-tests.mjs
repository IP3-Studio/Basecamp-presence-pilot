import { resolve } from "node:path";
import { writeFileSync } from "node:fs";

// CI sets LOGOS_QT_MCP automatically; for interactive use: nix build .#test-framework -o result-mcp
const root = process.env.LOGOS_QT_MCP || new URL("../result-mcp", import.meta.url).pathname;
const { test, run } = await import(resolve(root, "test-framework/framework.mjs"));

test("presence_ui: loads UI", async (app) => {
  await app.waitFor(
    async () => { await app.expectTexts(["Presence"]); },
    { timeout: 15000, interval: 500, description: "UI to load" }
  );
});

test("presence_ui: connects to backend", async (app) => {
  await app.waitFor(
    async () => { await app.expectTexts(["Connected"]); },
    { timeout: 15000, interval: 500, description: "backend to connect" }
  );
});

test("presence_ui: simulated room produces observations and a verdict", async (app) => {
  await app.waitFor(
    async () => { await app.expectTexts(["In this room", "Observer view", "Linkability"]); },
    { timeout: 15000, interval: 500, description: "panels to render" }
  );
  // The simulated room starts logging adverts within a couple of seconds.
  await app.waitFor(
    async () => { await app.expectTexts(["observations-present"]); },
    { timeout: 20000, interval: 1000, description: "observer log to fill" }
  );
  // Default settings: 15-minute epochs at 60x, so the second epoch starts
  // within 15 s of real time. Under the aligned address policy (the
  // default) the observer check must then report unlinkable.
  await app.waitFor(
    async () => { await app.expectTexts(["epochs-two-plus", "verdict-unlinkable"]); },
    { timeout: 45000, interval: 1000, description: "two epochs observed and the unlinkable verdict" }
  );
  const shot = await app.screenshot();
  if (shot && shot.image) {
    const out = process.env.PRESENCE_SCREENSHOT || "/tmp/presence_ui.png";
    try { writeFileSync(out, Buffer.from(shot.image, "base64")); console.log("screenshot:", out); } catch (e) { console.log("screenshot not saved:", String(e)); }
  }
});

test("presence_ui: drifting address policy makes the observer link epochs", async (app) => {
  await app.click("DRIFTING");
  // The policy change clears the log; the room re-fills and, once two epochs
  // are in, the drifting address bridges them and the verdict flips.
  await app.waitFor(
    async () => { await app.expectTexts(["epochs-two-plus", "verdict-linkable"]); },
    { timeout: 60000, interval: 1000, description: "the linkable verdict under a drifting address" }
  );
  const shot = await app.screenshot();
  if (shot && shot.image) {
    const out = (process.env.PRESENCE_SCREENSHOT || "/tmp/presence_ui.png").replace(/\.png$/, "-drifting.png");
    try { writeFileSync(out, Buffer.from(shot.image, "base64")); console.log("screenshot:", out); } catch (e) { console.log("screenshot not saved:", String(e)); }
  }
  // Leave the default behind for the next run.
  await app.click("ALIGNED");
});

test("presence_ui: a simulated friend is recognised through its pairwise tag", async (app) => {
  await app.click("Add sim friend");
  await app.waitFor(
    async () => { await app.expectTexts(["Ada (simulated)", "friend-here"]); },
    { timeout: 30000, interval: 1000, description: "the simulated friend's tag to be matched" }
  );
  const shot = await app.screenshot();
  if (shot && shot.image) {
    const out = (process.env.PRESENCE_SCREENSHOT || "/tmp/presence_ui.png").replace(/\.png$/, "-friend.png");
    try { writeFileSync(out, Buffer.from(shot.image, "base64")); console.log("screenshot:", out); } catch (e) { console.log("screenshot not saved:", String(e)); }
  }
  await app.click("×");
});

run();
