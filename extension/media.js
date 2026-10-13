// Media URLs live only in this worker's bounded memory, never in persistent storage.
export function publicUrl(value) {
  try { const url = new URL(value); return /^https?:$/.test(url.protocol) && !url.username && !url.password && value.length <= 32768 ? url.href : null; }
  catch { return null; }
}
export function youtubePage(value) {
  const address = publicUrl(value); if (!address) return null;
  const url = new URL(address);
  if (url.hostname === "youtu.be") return address;
  if (!/(^|\.)youtube\.com$/.test(url.hostname)) return null;
  const match = url.pathname.match(/^\/(?:embed|shorts|live)\/([\w-]{6,})/);
  const id = url.searchParams.get("v") || match?.[1];
  return id && /^[\w-]{6,}$/.test(id) ? `https://www.youtube.com/watch?v=${id}` : null;
}
export class MediaSources {
  constructor(now = () => Date.now()) { this.tabs = new Map(); this.now = now; }
  observe(details) {
    if (details.tabId < 0 || details.statusCode < 200 || details.statusCode >= 400) return;
    const url = publicUrl(details.url); if (!url || /\.(?:m4s|ts)(?:[?#]|$)/i.test(url)) return;
    const mime = details.responseHeaders?.find(h => h.name.toLowerCase() === "content-type")?.value || "";
    if (!/^(?:video\/|application\/(?:vnd\.apple\.mpegurl|x-mpegurl|dash\+xml))/i.test(mime) && !/\.(?:mp4|webm|mov|m3u8|mpd)(?:[?#]|$)/i.test(url)) return;
    // YouTube's playback requests may be partial audio/video fragments. Resolve its page instead.
    if (/(^|\.)googlevideo\.com$/.test(new URL(url).hostname)) return;
    const list = (this.tabs.get(details.tabId) || []).filter(x => x.url !== url && this.now() - x.at < 600000);
    list.push({url, frame: details.frameId, at: this.now()}); this.tabs.delete(details.tabId); this.tabs.set(details.tabId, list.slice(-32));
    if (this.tabs.size > 128) this.tabs.delete(this.tabs.keys().next().value);
  }
  resolve(message, sender) {
    const original = publicUrl(sender.url) || publicUrl(sender.origin) || publicUrl(sender.tab?.url);
    if (!original || sender.tab?.incognito) return null;
    // Chrome's sender.url can retain the document's initial URL after pushState.
    // Accept the content script's current location only within that same origin.
    const current = publicUrl(message.page);
    if (current && new URL(current).origin !== new URL(original).origin) return null;
    const page = current || original;
    if (/(^|\.)youtube\.com$/.test(new URL(page).hostname) || new URL(page).hostname === "youtu.be") {
      const player = youtubePage(message.videoPage);
      return player || youtubePage(page);
    }
    const direct = publicUrl(message.source); if (direct) return direct;
    const candidates = (this.tabs.get(sender.tab?.id) || []).filter(x => x.frame === sender.frameId && this.now() - x.at < 600000);
    // A single manifest can be resolved safely; multiple players need the site's extractor.
    return candidates.length === 1 ? candidates[0].url : page;
  }
}
