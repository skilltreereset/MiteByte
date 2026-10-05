#pragma once
#include <Arduino.h>

static const char PAGE_INDEX[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Fleabyte</title>
<style>
:root{
  --bg:#FAFAF7; --card:#FFFFFF; --line:#EAEAE4;
  --ink:#1A1D1B; --muted:#6E7671; --faint:#9AA29C;
  --accent:#5E8C61; --accent-hover:#4E7751; --accent-soft:#EDF3ED;
  --warn:#B4761F; --danger:#B3453C;
  --shadow:0 1px 2px rgba(26,29,27,.04), 0 2px 8px rgba(26,29,27,.05);
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,"Liberation Mono",monospace;
  --sans:system-ui,-apple-system,"Segoe UI",Roboto,Inter,sans-serif;
  --r:14px;
}
*{box-sizing:border-box}
html,body{margin:0}
body{
  background:var(--bg); color:var(--ink);
  font:15px/1.55 var(--sans);
  padding-bottom:4rem;
  -webkit-text-size-adjust:100%;
}
h1{font-size:28px;font-weight:600;letter-spacing:-.02em;margin:0}
h2{font-size:16px;font-weight:600;letter-spacing:-.01em;margin:0}
h3{font-size:14.5px;font-weight:600;margin:0}
p.sub{margin:.3rem 0 0;color:var(--muted);font-size:14px;max-width:56ch}
button{font:inherit;color:inherit;background:none;border:none;cursor:pointer}
:focus-visible{outline:2px solid var(--accent);outline-offset:2px;border-radius:6px}
[hidden]{display:none!important}

/* ---- top bar ---- */
.topbar{
  position:sticky;top:0;z-index:10;
  display:flex;align-items:center;justify-content:space-between;
  padding:.85rem 1.25rem;
  background:rgba(250,250,247,.88);backdrop-filter:blur(10px);
  border-bottom:1px solid var(--line);
}
.brand{display:flex;align-items:center;gap:.6rem;font-weight:600;letter-spacing:-.01em}
.dot{width:8px;height:8px;border-radius:50%;background:var(--faint);flex:none;transition:background .2s}
.dot.on{background:var(--accent)}
.dot.busy{background:var(--warn)}
.tools{display:flex;align-items:center;gap:.5rem}
.chip{
  padding:.3rem .7rem;border-radius:999px;
  background:var(--accent-soft);color:var(--accent);
  font-size:12.5px;font-weight:600;letter-spacing:.01em;
}
.chip:hover{background:#E3EDE3}
/* Only ever rendered while armed, so it reads as a warning rather than as
   one more permanent badge to tune out. */
.chip.armed{background:#FCF6EA;color:#7A5312;box-shadow:inset 0 0 0 1px #EBDBB6}
.chip.armed:hover{background:#F7EDD9}
.iconbtn{
  width:36px;height:36px;border-radius:50%;
  display:grid;place-items:center;color:var(--muted);
}
.iconbtn:hover{background:#F0F0EB;color:var(--ink)}
.iconbtn svg{width:20px;height:20px;fill:none;stroke:currentColor;stroke-width:1.7}
.iconbtn.active{background:var(--accent);color:#fff}
.iconbtn.active:hover{background:var(--accent-hover);color:#fff}

main{max-width:880px;margin:0 auto;padding:1.75rem 1.25rem 0}
.pagehead{margin-bottom:1.25rem}

/* ---- cards ---- */
.card{
  background:var(--card);border:1px solid var(--line);border-radius:var(--r);
  box-shadow:var(--shadow);margin-bottom:1rem;overflow:hidden;
}
.card-hd{
  display:flex;align-items:center;justify-content:space-between;gap:1rem;
  padding:1rem 1.15rem;
}
.card-bd{padding:0 1.15rem 1.15rem}
.card-hd + .card-bd{padding-top:0}

/* ---- payload library ---- */
.layout{display:grid;grid-template-columns:240px 1fr;gap:1rem;align-items:start}
.files{list-style:none;margin:0;padding:0 .5rem .5rem}
.files .group{padding:.6rem .4rem .3rem;color:var(--muted);font:600 11px/1 var(--sans);letter-spacing:.05em;text-transform:uppercase}
.files .group:first-child{padding-top:.2rem}
.files button{
  width:100%;text-align:left;padding:.55rem .65rem;border-radius:9px;
  font:13px/1.4 var(--mono);color:var(--muted);
  overflow:hidden;text-overflow:ellipsis;white-space:nowrap;
}
.files button{display:flex;align-items:center;gap:.5rem}
.files button .nm{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.entrykind{margin-left:auto;flex:none;color:var(--muted);font:11px var(--sans)}
.tool-details{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:.45rem 1rem;margin:1rem 0}
.tool-details dt{color:var(--muted);font-size:13px}
.tool-details dd{margin:0;text-align:right;overflow-wrap:anywhere;font-size:13px}
.tool-auto{display:flex;align-items:center;gap:.55rem;font-size:13px;margin:1rem 0}
.tool-auto input{accent-color:var(--accent)}
.tool-setup{border-top:1px solid var(--line);padding-top:.8rem;margin-top:1rem;font-size:13px;color:var(--muted)}
.tool-setup summary{cursor:pointer;color:var(--accent);font-weight:500}
.tool-setup p{margin:.65rem 0}
.tool-setup a{color:var(--accent)}
.osicon{width:15px;height:15px;flex:none;opacity:.65}
.files button[aria-current="true"] .osicon{opacity:1}
.osicon.spacer{visibility:hidden}
.files button:hover{background:#F5F5F0;color:var(--ink)}
.files button[aria-current="true"]{background:var(--accent-soft);color:var(--accent);font-weight:600}
.blank{padding:.5rem .65rem 1rem;color:var(--faint);font-size:13px}

/* ---- fire after boot, under the run bar ---- */
.bootbar{display:flex;align-items:center;gap:.6rem;flex-wrap:wrap;
  margin-top:.75rem;padding-top:.75rem;border-top:1px solid var(--line)}
.bootbar .btn{padding:.45rem .9rem;font-size:13.5px}
.btn.ghost.boot[aria-pressed="true"]{color:#7A5312;border-color:#EBDBB6;background:#FCF6EA}
.bootstate{
  font:11.5px var(--mono);padding:.1rem .45rem;border-radius:999px;
  background:var(--bg);border:1px solid var(--line);color:var(--faint);
}
.bootstate.on{background:#FCF6EA;border-color:#EBDBB6;color:#7A5312}
.boothint{flex:1;min-width:16ch;color:var(--faint);font-size:12px}
.boothint code{font:11px var(--mono);background:var(--bg);border:1px solid var(--line);
  border-radius:5px;padding:.05rem .28rem}

/* ---- editor ---- */
.namefield{
  flex:1;min-width:0;
  background:var(--bg);border:1px solid var(--line);border-radius:9px;
  padding:.45rem .65rem;font:13px var(--mono);color:var(--ink);
}
.namefield:focus{outline:none;border-color:var(--accent);background:#fff}
textarea{
  display:block;width:100%;height:320px;resize:vertical;
  background:var(--bg);color:var(--ink);
  border:1px solid var(--line);border-radius:10px;
  padding:.85rem;font:13px/1.7 var(--mono);
  white-space:pre;overflow-wrap:normal;overflow-x:auto;
}
textarea:focus{outline:none;border-color:var(--accent);background:#fff}

.runbar{display:flex;align-items:center;gap:.65rem;padding-top:.9rem;flex-wrap:wrap}
.btn{
  padding:.6rem 1.25rem;border-radius:999px;font-weight:600;font-size:14.5px;
  background:var(--accent);color:#fff;transition:background .15s;
}
.btn:hover:enabled{background:var(--accent-hover)}
.btn:disabled{background:#DEDED8;color:#9AA29C;cursor:not-allowed}
.btn.ghost{background:transparent;color:var(--muted);border:1px solid var(--line)}
.btn.ghost:hover:enabled{background:#F5F5F0;color:var(--ink)}
.btn.ghost.stop:enabled{color:var(--danger);border-color:#E7CFCD}
.btn.danger{background:transparent;color:var(--danger);border:1px solid #E7CFCD}
.btn.danger:hover{background:#FDF3F2}
.link{color:var(--accent);font-size:13.5px;font-weight:500;padding:.25rem .4rem;border-radius:7px}
.link:hover{background:var(--accent-soft)}
.link.quiet{color:var(--muted)}
.link.quiet:hover{background:#F0F0EB;color:var(--ink)}
.delay{display:flex;align-items:center;gap:.4rem;font-size:13.5px;color:var(--muted)}
.delay input{
  width:64px;background:var(--bg);color:var(--ink);
  border:1px solid var(--line);border-radius:999px;
  padding:.42rem .6rem;font:13.5px var(--sans);text-align:center;
}
.delay input:focus{outline:none;border-color:var(--accent);background:#fff}
.host{display:flex;align-items:center;gap:.4rem;font-size:13px;color:var(--faint)}
.host .hdot{width:7px;height:7px;border-radius:50%;background:#DEDED8;flex:none}
.host.ready{color:var(--muted)}
.host.ready .hdot{background:var(--accent)}
.status{margin-left:auto;font-size:13px;color:var(--muted);font-variant-numeric:tabular-nums}
.status.err{color:var(--danger)}
.status.ok{color:var(--accent)}

pre{
  margin:0;padding:.9rem 1rem;max-height:200px;overflow:auto;
  background:var(--bg);border:1px solid var(--line);border-radius:10px;
  font:12.5px/1.65 var(--mono);color:var(--muted);white-space:pre-wrap;
}

/* ---- settings ---- */
.settings-heading{margin:1.5rem 0 .75rem}
.settings-part{margin-top:1rem;padding-top:1rem;border-top:1px solid var(--line)}
.hint{font-size:12.5px;color:var(--muted);margin:.4rem 0 0}
.details{margin-top:.8rem;color:var(--muted);font-size:12.5px}
.details summary{cursor:pointer;width:fit-content;padding:.25rem 0}
.details p{margin:.4rem 0 0;max-width:62ch}
.lock-notice{display:flex;align-items:center;justify-content:space-between;gap:.75rem;
  flex-wrap:wrap;margin:.8rem 0;padding:.65rem .8rem;border-radius:10px;background:var(--accent-soft)}
.lock-notice .hint{margin:0;color:var(--ink)}
.lock-notice .btn{padding:.4rem .75rem;font-size:13px;background:var(--card)}
.field input[type="number"]{width:8rem;max-width:100%;background:var(--bg);color:var(--ink);
  border:1px solid var(--line);border-radius:10px;padding:.65rem .8rem;font:14.5px var(--sans)}
.back{display:inline-flex;align-items:center;gap:.4rem;color:var(--muted);font-size:14px;margin-bottom:1rem;padding:.3rem .5rem;border-radius:8px}
.back:hover{background:#F0F0EB;color:var(--ink)}
.preview{display:flex;justify-content:center;margin:1.1rem 0 .3rem}
.lchoices{
  margin-top:.9rem;border:1px solid var(--line);border-radius:12px;overflow:hidden;
  max-height:310px;overflow-y:auto;
}
.lrow{
  width:100%;display:flex;align-items:center;justify-content:space-between;gap:1rem;
  padding:.7rem .9rem;border-top:1px solid var(--line);text-align:left;
}
.lrow:first-child{border-top:none}
.lrow:hover{background:#F5F5F0}
.lrow[aria-checked="true"]{background:var(--accent-soft)}
.lrow[aria-checked="true"] .lname{color:var(--accent);font-weight:600}
.lname{font-size:14px}
.larr{
  font:11px/1 var(--mono);letter-spacing:.04em;
  color:var(--muted);background:var(--bg);
  border:1px solid var(--line);border-radius:999px;padding:.28rem .55rem;flex:none;
}
.lrow[aria-checked="true"] .larr{color:var(--accent);border-color:#C3D5C4;background:#fff}
.choices{display:grid;grid-template-columns:1fr 1fr;gap:.75rem;margin-top:1rem}
.choice{
  padding:1rem .8rem .85rem;border-radius:12px;
  border:1.5px solid var(--line);background:var(--card);
  display:flex;flex-direction:column;align-items:center;gap:.7rem;
  transition:border-color .15s,background .15s;
}
.choice:hover{border-color:#CFD6CF}
.choice[aria-checked="true"]{border-color:var(--accent);background:var(--accent-soft)}
.caps{display:flex;gap:4px}
.cap{
  width:29px;height:33px;display:grid;place-items:center;
  font:12.5px/1 var(--mono);color:var(--muted);
  background:#fff;border:1px solid var(--line);border-bottom-width:2.5px;border-radius:6px;
}
.choice[aria-checked="true"] .cap{color:var(--accent);border-color:#C3D5C4}
.caplab{font-size:13.5px;color:var(--muted)}
.choice[aria-checked="true"] .caplab{color:var(--ink);font-weight:500}

/* ---- toggles, orientation, light legend ---- */
.row{
  display:flex;align-items:center;justify-content:space-between;gap:1rem;
  padding:.85rem 0;border-top:1px solid var(--line);
}
.row:first-of-type{border-top:none}
.row .lab{font-size:14.5px}
.row .lab small{display:block;color:var(--muted);font-size:12.5px;margin-top:.1rem}
.switch{
  position:relative;width:46px;height:27px;border-radius:999px;
  background:#DEDED8;flex:none;transition:background .18s;
}
.switch[aria-checked="true"]{background:var(--accent)}
.switch::after{
  content:"";position:absolute;top:3px;left:3px;width:21px;height:21px;
  border-radius:50%;background:#fff;transition:transform .18s;
  box-shadow:0 1px 3px rgba(0,0,0,.18);
}
.switch[aria-checked="true"]::after{transform:translateX(19px)}

.slide{
  display:flex;align-items:center;gap:.75rem;
  padding:.35rem 0 .85rem;border-top:none;
}
.slide .lab{font-size:14.5px;flex:none;min-width:9.5rem}
.slide input[type=range]{
  flex:1;min-width:0;accent-color:var(--accent);height:1.4rem;
}
.slide .pct{
  width:3.2ch;flex:none;font:13px var(--mono);color:var(--muted);
  text-align:right;font-variant-numeric:tabular-nums;
}

.orient{display:grid;grid-template-columns:repeat(4,1fr);gap:.6rem;margin-top:.9rem}
.orient button{
  padding:.8rem .4rem .6rem;border-radius:11px;
  border:1.5px solid var(--line);background:var(--card);
  display:flex;flex-direction:column;align-items:center;gap:.5rem;
  font-size:12px;color:var(--muted);
}
.orient button:hover{border-color:#CFD6CF}
.orient button[aria-checked="true"]{border-color:var(--accent);background:var(--accent-soft);color:var(--ink)}
.orient button:disabled,.switch:disabled,input[type=range]:disabled{opacity:.45;cursor:not-allowed}
.glyph{
  border:1.5px solid var(--faint);border-radius:3px;position:relative;background:#fff;
}
.orient button[aria-checked="true"] .glyph{border-color:var(--accent)}
.glyph.land{width:34px;height:19px}
.glyph.port{width:19px;height:34px}
/* the bar marks the top edge, so a flipped tile is readable at a glance */
.glyph::before{content:"";position:absolute;left:2px;right:2px;height:3px;background:var(--faint);top:2px}
.glyph.flip::before{top:auto;bottom:2px}
.orient button[aria-checked="true"] .glyph::before{background:var(--accent)}

.sdbar{display:flex;align-items:center;gap:.5rem;margin-top:1rem;flex-wrap:wrap}
.sdpath{font:12.5px var(--mono);color:var(--muted);flex:1;min-width:0;
  overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.sdlist{border:1px solid var(--line);border-radius:12px;overflow:hidden;margin-top:.6rem;
  max-height:260px;overflow-y:auto}
.sdrow{display:flex;align-items:center;gap:.6rem;padding:.55rem .8rem;border-top:1px solid var(--line)}
.sdrow:first-child{border-top:none}
.sdrow .fn{flex:1;min-width:0;font:13px/1.4 var(--mono);
  overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.sdrow .fs{font-size:12px;color:var(--faint);flex:none}
.sdrow.dir .fn{color:var(--accent);cursor:pointer}
.sdrow .act{font-size:12.5px;color:var(--muted);padding:.2rem .4rem;border-radius:6px;flex:none}
.sdrow .act:hover{background:#F0F0EB;color:var(--ink)}
.sdrow .act.del:hover{background:#FDF3F2;color:var(--danger)}
.sdnote{padding:.9rem .8rem;color:var(--faint);font-size:13px}
.legend{display:flex;flex-wrap:wrap;gap:.5rem 1.1rem;margin-top:.9rem}
.legend div{display:flex;align-items:center;gap:.45rem;font-size:12.5px;color:var(--muted)}
.legend i{width:9px;height:9px;border-radius:50%;flex:none}
@keyframes breathe{0%,100%{opacity:.15}50%{opacity:1}}
.legend i.pulse{animation:breathe 2.2s ease-in-out infinite}
.legend i.pulse.fast{animation-duration:.7s}

select.sel{
  width:100%;background:var(--bg);color:var(--ink);
  border:1px solid var(--line);border-radius:10px;
  padding:.65rem .8rem;font:14.5px var(--sans);cursor:pointer;
}
select.sel:focus{outline:none;border-color:var(--accent);background:#fff}

.field{margin-top:1rem}
.field label{display:block;font-size:13.5px;font-weight:500;margin-bottom:.35rem}
/* Text fields only. A bare ".field input" also caught the "show password"
   checkbox and stretched it to full width. */
.field input[type="text"],.field input[type="password"]{
  width:100%;background:var(--bg);color:var(--ink);
  border:1px solid var(--line);border-radius:10px;
  padding:.65rem .8rem;font:14.5px var(--sans);
}
.field input[type="text"]:focus,.field input[type="password"]:focus{
  outline:none;border-color:var(--accent);background:#fff;
}
.field .hint{font-size:12.5px;color:var(--muted);margin-top:.35rem}
.field .hint code{font:11.5px var(--mono);background:var(--bg);border:1px solid var(--line);
  border-radius:5px;padding:.05rem .3rem;color:var(--muted)}
.reveal{display:flex;align-items:center;gap:.4rem;margin-top:.5rem;font-size:13px;color:var(--muted)}
.notice{
  margin-top:1rem;padding:.75rem .9rem;border-radius:10px;
  background:#FCF6EA;color:#7A5312;font-size:13.5px;
}

footer{max-width:880px;margin:2rem auto 0;padding:0 1.25rem;color:var(--faint);font-size:12.5px}
footer #ver{font:11.5px var(--mono)}

/* ---- reconnect overlay ---- */
.overlay{
  position:fixed;inset:0;z-index:50;display:grid;place-items:center;
  background:rgba(250,250,247,.96);padding:2rem;text-align:center;
}
.overlay h2{font-size:21px;margin-bottom:.6rem}
.overlay p{color:var(--muted);max-width:34ch;margin:0 auto}
.overlay .net{
  display:inline-block;margin-top:1.1rem;padding:.5rem 1rem;border-radius:999px;
  background:var(--accent-soft);color:var(--accent);font:14px var(--mono);font-weight:600;
}

@media (max-width:760px){
  .layout{grid-template-columns:1fr}
  textarea{height:240px}
  h1{font-size:24px}
  main{padding-top:1.25rem}
}
@media (max-width:480px){
  .slide{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:.4rem .75rem}
  .slide .lab{grid-column:1 / -1;min-width:0}
  .slide input[type=range]{width:100%}
}
@media (max-width:360px){
  .orient{grid-template-columns:repeat(2,1fr)}
}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}
</style>
</head>
<body>

<div class="topbar">
  <div class="brand"><span class="dot" id="dot"></span>Fleabyte</div>
  <div class="tools">
    <button class="chip armed" id="armedchip" hidden
            title="A script is armed to fire at the next plug-in">ARMED</button>
    <button class="chip" id="layoutchip" title="Keyboard layout">AZERTY</button>
    <button class="iconbtn" id="lockdisplay" aria-label="Lock screen" title="Lock screen; Wi-Fi and keyboard stay on">
      <svg viewBox="0 0 24 24"><rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/></svg>
    </button>
    <button class="iconbtn" id="gear" aria-label="Settings">
      <svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3.2"/><path d="M19.4 13.5a7.7 7.7 0 0 0 0-3l1.7-1.3-1.8-3.1-2 .8a7.7 7.7 0 0 0-2.6-1.5L14.4 3h-3.6l-.3 2.4a7.7 7.7 0 0 0-2.6 1.5l-2-.8L4 9.2l1.7 1.3a7.7 7.7 0 0 0 0 3L4 14.8l1.8 3.1 2-.8a7.7 7.7 0 0 0 2.6 1.5l.3 2.4h3.6l.3-2.4a7.7 7.7 0 0 0 2.6-1.5l2 .8 1.8-3.1z"/></svg>
    </button>
  </div>
</div>

<!-- ============ main ============ -->
<main id="view-main">
  <div class="pagehead">
    <h1>Library</h1>
    <p class="sub">Run a payload or start a device tool.</p>
  </div>

  <div class="layout">
    <section class="card">
      <div class="card-hd"><h2>On this device</h2><button class="link" id="new">New</button></div>
      <ul class="files" id="list"></ul>

    </section>

    <section class="card" id="editor">
      <div class="card-hd">
        <input type="text" class="namefield" id="name" placeholder="payload-name.txt" spellcheck="false" autocapitalize="off">
        <button class="link" id="save">Save</button>
        <button class="link quiet" id="del">Delete</button>
      </div>
      <div class="card-bd">
        <textarea id="script" spellcheck="false" autocapitalize="off" autocomplete="off"
          placeholder="REM One command per line&#10;GUI r&#10;DELAY 500&#10;STRING notepad&#10;ENTER"></textarea>
        <div class="runbar">
          <button class="btn" id="run">Run</button>
          <button class="btn ghost stop" id="stop" disabled>Stop</button>
          <span class="host" id="host"><span class="hdot"></span><span id="hostlab">No host yet</span></span>
        <label class="delay">Start after
            <input type="number" id="delay" min="0" max="3600" step="1" value="0"
                   aria-label="Seconds before the payload starts">
            s
          </label>
          <span class="status" id="status">Ready</span>
        </div>
        <div class="bootbar">
          <button class="btn ghost boot" id="armboot">Fire after boot</button>
          <span class="bootstate" id="bootstate">Off</span>
          <span class="boothint">Arms one run on the next plug-in, with screen and Wi-Fi off. Hard lock blocks it.
          Begin it with <code>WAIT_FOR_HOST</code>, or set a start delay.</span>
        </div>
      </div>
    </section>
    <section class="card" id="toolpanel" hidden aria-labelledby="tooltitle">
      <div class="card-hd"><h2 id="tooltitle"></h2><span class="entrykind">Tool</span></div>
      <div class="card-bd">
        <p class="hint" id="tooldescription"></p>
        <div class="runbar">
          <button class="btn" id="toolstart">Start</button>
          <button class="btn ghost stop" id="toolstop" disabled>Stop</button>
          <span class="status" id="toolstatus" role="status" aria-live="polite">Stopped</span>
        </div>
        <p class="hint" id="toolnotice"></p>
        <dl class="tool-details" id="tooldetails"></dl>
        <label class="tool-auto"><input type="checkbox" id="toolauto">Start automatically after unlock</label>
        <p class="status" id="toolactionstatus" role="status"></p>
        <details class="tool-setup" id="toolsetup" hidden>
          <summary id="toolsetuptitle">Setup</summary>
          <p id="toolsetuphint"></p>
          <p><button class="btn ghost" id="toolsetuppayload" hidden>Open setup payload</button></p>
          <p><a id="toolsetupdownload" href="#" download>Download setup</a></p>
          <p id="toolsetupmanual"></p>
        </details>
      </div>
    </section>
  </div>

  <section class="card">
    <div class="card-hd"><h2>Run log</h2><button class="link quiet" id="clearlog">Clear</button></div>
    <div class="card-bd"><pre id="log">Nothing yet. The log fills up on the first run.</pre></div>
  </section>
</main>

<!-- ============ settings ============ -->
<main id="view-settings" hidden>
  <button class="back" id="back">&larr; Library</button>
  <div class="pagehead">
    <h1>Settings</h1>
    <p class="hint" id="settingsstatus" role="status"></p>
  </div>

  <section class="settings-group" id="settings-device" aria-labelledby="device-title">
    <h2 class="settings-heading" id="device-title">Device</h2>
  <section class="card">
    <div class="card-bd" style="padding-top:1.15rem">
      <h3>Device name</h3>
      <p class="sub">Shown on the screen and used to identify the USB drive.</p>
      <div class="field">
        <label for="devname">Device name</label>
        <input type="text" id="devname" maxlength="16" spellcheck="false" autocapitalize="off">
        <div class="hint">Up to 16 characters. The USB name updates on the next plug-in.</div>
      </div>
      <div class="runbar">
        <button class="btn" id="savename">Save name</button>
        <span class="status" id="namestatus"></span>
      </div>
    </div>
  </section>

  <section class="card">
    <div class="card-bd" style="padding-top:1.15rem">
      <h3>Keyboard layout</h3>
      <p class="sub">Match the keyboard layout of the connected computer. Changes save automatically.</p>
      <div class="preview"><span class="caps" id="caps"></span></div>
      <div class="lchoices" role="radiogroup" aria-label="Keyboard layout" id="lchoices"></div>
      <p class="hint" id="layoutstatus" role="status"></p>
    </div>
  </section>

  <section class="card">
    <div class="card-bd" style="padding-top:1.15rem">
      <h3>USB drive</h3>
      <p class="sub">Share the microSD card with the connected computer.</p>

      <div class="row" style="margin-top:.6rem">
        <span class="lab">Share card over USB<small id="drivehint">Checking for a card…</small></span>
        <button class="switch" id="drivesw" role="switch" aria-checked="false" aria-label="Share card over USB"></button>
      </div>
      <span class="status" id="drivestatus" style="margin-left:0"></span>

      <div class="sdbar">
        <button class="link quiet" id="sdup">&uarr; Up</button>
        <span class="sdpath" id="sdpath">/</span>
        <button class="link" id="sdrefresh">Refresh</button>
      </div>
      <div class="sdlist" id="sdlist"></div>
    </div>
  </section>
  </section>

  <section class="card settings-group" id="settings-display" aria-labelledby="display-title">
    <div class="card-bd" style="padding-top:1.15rem">
      <h2 id="display-title">Display &amp; LED</h2>

      <div class="lock-notice" id="screenlocknotice" hidden>
        <p class="hint" id="screenlockhint" role="status" hidden>Unlock to change screen settings.</p>
        <button class="btn ghost" id="unlockscreen" hidden>Unlock screen</button>
      </div>

      <div class="orient" role="radiogroup" aria-label="Screen orientation">
        <button role="radio" aria-checked="false" data-rot="1"><span class="glyph land"></span>Landscape</button>
        <button role="radio" aria-checked="false" data-rot="3" aria-label="Landscape flipped"><span class="glyph land flip"></span>Flipped</button>
        <button role="radio" aria-checked="false" data-rot="0"><span class="glyph port"></span>Portrait</button>
        <button role="radio" aria-checked="false" data-rot="2" aria-label="Portrait flipped"><span class="glyph port flip"></span>Flipped</button>
      </div>

      <div class="row" style="margin-top:1.1rem">
        <span class="lab">Screen<small>When unlocked, tap the device button to wake it briefly.</small></span>
        <button class="switch" id="screensw" role="switch" aria-checked="true" aria-label="Screen"></button>
      </div>
      <div class="slide">
        <span class="lab">Screen brightness</span>
        <input type="range" id="screenbright" min="0" max="100" value="100" aria-label="Screen brightness">
        <span class="pct" id="screenbrightval">100</span>
      </div>

      <div class="row">
        <span class="lab">Show Wi-Fi login at startup<small>Show the password and QR code until a device connects.</small></span>
        <button class="switch" id="accesssw" role="switch" aria-checked="true" aria-label="Show Wi-Fi login at startup"></button>
      </div>

      <div class="settings-part">
      <h3>Status LED</h3>
      <div class="row">
        <span class="lab">Enable LED<small>Show device activity with a coloured light.</small></span>
        <button class="switch" id="ledsw" role="switch" aria-checked="true" aria-label="Status LED"></button>
      </div>
      <div class="slide">
        <span class="lab">LED brightness</span>
        <input type="range" id="ledbright" min="0" max="100" value="20" aria-label="LED brightness">
        <span class="pct" id="ledbrightval">20</span>
      </div>

      <details class="details" id="ledlegend">
        <summary>LED colours</summary>
        <div class="legend">
          <div><i class="pulse" style="background:#E5484D"></i>Waiting · slow pulse</div>
          <div><i style="background:#0028C8"></i>Standby</div>
          <div><i class="pulse fast" style="background:#E5484D"></i>Running · fast pulse</div>
          <div><i style="background:#00C83C"></i>Finished, 5 s</div>
          <div><i style="background:#E5484D"></i>Error · steady, 5 s</div>
        </div>
      </details>
      </div>

      <div class="runbar">
        <button class="btn" id="savedisplay">Save display</button>
        <span class="status" id="dispstatus"></span>
      </div>
    </div>
  </section>

  <section class="card settings-group" id="settings-wifi" aria-labelledby="wifi-title">
    <div class="card-bd" style="padding-top:1.15rem">
      <h2 id="wifi-title">Wi-Fi network</h2>
      <p class="sub">The network created by this device. Saving restarts it; reconnect to the new network.</p>

      <div class="field">
        <label for="ssid">Network name</label>
        <input type="text" id="ssid" maxlength="32" spellcheck="false" autocapitalize="off" autocomplete="off">
      </div>

      <div class="field">
        <label for="pass">Password</label>
        <input type="password" id="pass" maxlength="63" spellcheck="false" autocapitalize="off" autocomplete="new-password">
        <div class="hint" id="passhint">8 to 63 characters.</div>
        <label class="reveal"><input type="checkbox" id="reveal"> Show password</label>
      </div>

      <div class="runbar">
        <button class="btn" id="savewifi">Save and restart</button>
        <span class="status" id="wifistatus"></span>
      </div>
    </div>
  </section>

  <section class="settings-group" id="settings-lock" aria-labelledby="lock-title">
    <h2 class="settings-heading" id="lock-title">Startup &amp; lock</h2>
  <section class="card">
    <div class="card-bd" style="padding-top:1.15rem">
      <h3>Startup</h3>
      <p class="hint">Changes apply on the next boot.</p>

      <div class="row">
        <span class="lab">Start in standby<small>Unlock to start Wi-Fi and keyboard.</small></span>
        <button class="switch" id="standbysw" role="switch" aria-checked="true" aria-label="Start in standby"></button>
      </div>

      <div class="settings-part">
      <h3>Button gestures</h3>
      <div class="field">
        <label for="unlockseq">Unlock gesture</label>
        <input type="text" id="unlockseq" maxlength="12" spellcheck="false" autocapitalize="characters" autocomplete="off">
        <div class="hint">S = short press, L = long press. Use 1–12 presses, e.g. <code>SSSLL</code>.</div>
      </div>

      <div class="field">
        <label for="longpress">Long press (ms)</label>
        <input type="number" id="longpress" min="50" max="5000" step="10">
        <div class="hint">Hold at least this long for an L press.</div>
      </div>

      <div class="row" style="margin-top:.3rem">
        <span class="lab">Hard-lock gesture<small>Stop payloads and turn off screen, LED, Wi-Fi and keyboard.</small></span>
        <button class="switch" id="hardlocksw" role="switch" aria-checked="true" aria-label="Hard-lock gesture"></button>
      </div>

      <div id="hardlockopts">
        <div class="field">
          <label for="hardlockseq">Hard-lock gesture</label>
          <input type="text" id="hardlockseq" maxlength="12" spellcheck="false" autocapitalize="characters" autocomplete="off">
          <div class="hint">Use S and L, e.g. <code>SSSSS</code>. Neither gesture may contain the other.</div>
        </div>
      </div>
      <div class="field">
        <label for="hardlockre">Locked plug-ins after hard lock</label>
        <input type="number" id="hardlockre" min="1" max="20" step="1">
        <div class="hint">Skip this many plug-ins before unlocking is allowed. Set to 1: replug twice to recover.</div>
      </div>

      </div>
      <div class="runbar">
        <button class="btn" id="savesecurity">Save startup &amp; lock</button>
        <span class="status" id="securitystatus" role="status"></span>
      </div>

      <div class="settings-part">
        <h3>Hard lock now</h3>
        <p class="hint">Stops payloads and turns off screen, LED, Wi-Fi and keyboard. Unlocking stays blocked for the saved plug-in count. Hold the device button for 10 seconds to reset.</p>
        <div class="runbar">
          <button class="btn danger" id="hardlocknow">Hard lock</button>
          <span class="status" id="lockresetstatus" role="status"></span>
        </div>
      </div>
    </div>
  </section>

  <section class="card">
    <div class="card-bd" style="padding-top:1.15rem">
      <h3>Reset device</h3>
      <p class="hint">Reset settings. Saved payloads are kept.</p>
      <div class="runbar">
        <button class="btn danger" id="reset">Restore defaults</button>
        <span class="status" id="resetstatus" role="status"></span>
      </div>
    </div>
  </section>
  </section>
</main>

<footer>Machines you own, or have written authorisation to test.
<span id="ver"></span></footer>

<!-- ============ reconnect overlay ============ -->
<div class="overlay" id="overlay" hidden>
  <div>
    <h2 id="overlaytitle">Restarting device</h2>
    <p id="overlaymessage">Unlock if in standby, then reconnect to this network.</p>
    <div class="net" id="newnet"></div>
  </div>
</div>

<script>
const $ = s => document.querySelector(s);
const list = $('#list'), nameIn = $('#name'), script = $('#script');
const runBtn = $('#run'), stopBtn = $('#stop'), status = $('#status'), logEl = $('#log');
let current = null, layout = 'us', dirty = false, poll = true;
let currentTool = null, toolItems = [], activeTool = '', startupTool = '';
let payloadBusy = false, toolPending = false, toolStartupPending = false;
let logSeq = 0, logText = '';

script.addEventListener('input', () => dirty = true);

async function api(path, opts) {
  const r = await fetch(path, opts);
  if (!r.ok) {
    let msg = r.status;
    try { msg = (await r.json()).error || msg; } catch (e) {}
    throw new Error(msg);
  }
  return r;
}
function form(obj) {
  const b = new URLSearchParams();
  for (const k in obj) b.append(k, obj[k]);
  return { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:b };
}

/* ---- navigation ---- */
function show(view) {
  const settings = view === 'settings';
  $('#view-main').hidden = settings;
  $('#view-settings').hidden = !settings;
  if (settings) loadSettings();
  window.scrollTo(0, 0);
}
$('#gear').onclick = () => show('settings');
$('#layoutchip').onclick = () => show('settings');
$('#back').onclick = () => show('main');

/* ---- layout ---- */
let layouts = [], arrangement = 'QWERTY';

function paintLayout() {
  $('#layoutchip').textContent = arrangement;

  const caps = ['AZERTY', 'QWERTZ', 'QWERTY'].includes(arrangement) ? arrangement : 'QWERTY';
  const capsEl = $('#caps');
  if (capsEl) {
    capsEl.innerHTML = '';
    for (const ch of caps) {
      const s = document.createElement('span');
      s.className = 'cap';
      s.textContent = ch;
      capsEl.appendChild(s);
    }
  }

  const box = $('#lchoices');
  if (!box || !layouts.length) return;
  box.innerHTML = '';
  for (const l of layouts) {
    const b = document.createElement('button');
    b.className = 'lrow';
    b.setAttribute('role', 'radio');
    b.setAttribute('aria-checked', l.code === layout);
    b.innerHTML = '<span class="lname"></span><span class="larr"></span>';
    b.querySelector('.lname').textContent = l.name;
    b.querySelector('.larr').textContent = l.arrangement;
    b.onclick = async () => {
      layout = l.code;
      arrangement = l.arrangement;
      paintLayout();
      $('#layoutstatus').textContent = '';
      try { await api('/api/layout', form({layout})); }
      catch (e) { $('#layoutstatus').textContent = e.message; }
    };
    box.appendChild(b);
  }
}

/* ---- payloads ---- */
const OS_ICON = {
  windows: '<svg class="osicon" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true"><rect x="1" y="1.6" width="6" height="6" rx=".8"/><rect x="9" y="1.6" width="6" height="6" rx=".8"/><rect x="1" y="9.4" width="6" height="6" rx=".8"/><rect x="9" y="9.4" width="6" height="6" rx=".8"/></svg>',
  macos: '<svg class="osicon" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true"><path d="M11.2 8.5c0-1.6 1.3-2.4 1.4-2.4-.8-1.1-2-1.3-2.4-1.3-1-.1-2 .6-2.5.6s-1.3-.6-2.2-.6c-1.1 0-2.2.7-2.7 1.7-1.2 2-.3 5 .8 6.6.6.8 1.2 1.7 2.1 1.7.9 0 1.2-.5 2.2-.5s1.3.5 2.2.5 1.5-.8 2-1.6c.7-.9.9-1.8.9-1.9 0 0-1.8-.7-1.8-2.8zM9.7 3.5c.5-.6.8-1.4.7-2.2-.7 0-1.6.5-2.1 1.1-.4.5-.8 1.3-.7 2.1.8.1 1.6-.4 2.1-1z"/></svg>',
  linux: '<svg class="osicon" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true"><path d="M8 .9C6.2.9 5 2.3 5 4v1.5c0 .7-.3 1.1-.8 1.8C3.3 8.5 2.4 9.9 2.4 11.3c0 1.5 1 2.6 2.1 3.2.4.2.6.5.7.9h5.6c.1-.4.3-.7.7-.9 1.1-.6 2.1-1.7 2.1-3.2 0-1.4-.9-2.8-1.8-4-.5-.7-.8-1.1-.8-1.8V4c0-1.7-1.2-3.1-3-3.1zM6.8 3.4c.4 0 .7.4.7 1s-.3 1-.7 1-.7-.4-.7-1 .3-1 .7-1zm2.4 0c.4 0 .7.4.7 1s-.3 1-.7 1-.7-.4-.7-1 .3-1 .7-1zM8 5.8c.6 0 1.3.4 1.3.8 0 .3-.8.8-1.3.8s-1.3-.5-1.3-.8c0-.4.7-.8 1.3-.8z"/></svg>',
};

function paintList(items, tools = []) {
  list.innerHTML = '';
  if (!items.length && !tools.length) {
    list.innerHTML = '<li class="blank">No payloads yet. Create one to get started.</li>';
    return;
  }
  const toolIcon = '<svg class="osicon" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true"><path d="M1 5a11 11 0 0 1 14 0M3.5 8a7 7 0 0 1 9 0M6 11a3 3 0 0 1 4 0"/><circle cx="8" cy="14" r=".6" fill="currentColor"/></svg>';
  const addEntry = (it, isTool) => {
    const li = document.createElement('li');
    const b = document.createElement('button');
    b.innerHTML = (isTool ? toolIcon : OS_ICON[it.os] || '<svg class="osicon spacer" viewBox="0 0 16 16"></svg>')
                + '<span class="nm"></span><span class="entrykind"></span>';
    b.querySelector('.nm').textContent = it.name;
    b.querySelector('.entrykind').textContent = isTool ? (it.running ? 'Running' : 'Tool') : 'Payload';
    if (it.os) b.title = it.name + ' \u2014 ' + it.os;
    b.setAttribute('aria-current', isTool ? it.id === currentTool : !currentTool && it.name === current);
    b.onclick = () => isTool ? openTool(it.id) : openPayload(it.name);
    li.appendChild(b);
    list.appendChild(li);
  };
  const addGroup = (label, entries, isTool) => {
    if (!entries.length) return;
    const hd = document.createElement('li');
    hd.className = 'group';
    hd.textContent = label;
    list.appendChild(hd);
    for (const it of entries) addEntry(it, isTool);
  };
  addGroup('Device tools', tools, true);
  addGroup('Payloads', items, false);
}

async function openPayload(n) {
  if (currentTool && n === current) {
    currentTool = null; showSelection(); refresh(); return;
  }
  if (dirty && !confirm('The current payload has unsaved changes. Discard them?')) return;
  const r = await api('/api/payload?name=' + encodeURIComponent(n));
  script.value = await r.text();
  nameIn.value = n;
  current = n;
  currentTool = null;
  showSelection();
  dirty = false;
  refresh();
}

$('#new').onclick = () => {
  if (dirty && !confirm('The current payload has unsaved changes. Discard them?')) return;
  currentTool = null;
  showSelection();
  current = null;
  nameIn.value = '';
  script.value = 'REM New payload\n';
  dirty = false;
  nameIn.focus();
  refresh();
};

$('#save').onclick = async () => {
  const n = nameIn.value.trim();
  if (!n) { status.textContent = 'Name the payload first'; nameIn.focus(); return; }
  try {
    await api('/api/payload', form({name:n, content:script.value}));
    current = n; dirty = false;
    status.textContent = 'Saved';
    refresh();
  } catch (e) { status.textContent = e.message; }
};

$('#del').onclick = async () => {
  if (!current) { status.textContent = 'No payload selected'; return; }
  if (!confirm('Delete ' + current + '?')) return;
  await api('/api/payload/delete', form({name:current}));
  current = null; nameIn.value = ''; script.value = ''; dirty = false;
  refresh();
};

runBtn.onclick = async () => {
  try {
    await api('/api/run', form({script:script.value, delay:$('#delay').value || 0}));
    setBusy(true);
    refresh();
  } catch (e) { status.textContent = e.message; }
};

/* ---- built-in device tools ---- */
function showSelection() {
  $('#editor').hidden = !!currentTool;
  $('#toolpanel').hidden = !currentTool;
}
function openTool(id) {
  if (!toolItems.some(t => t.id === id)) return;
  // Keep the editor buffer intact while inspecting a tool.
  currentTool = id;
  $('#toolactionstatus').textContent = '';
  showSelection();
  paintTool();
  paintList(lastPayloads, toolItems);
}
let lastPayloads = [];
function paintTool() {
  const tool = toolItems.find(t => t.id === currentTool);
  if (!tool) return;
  $('#tooltitle').textContent = tool.name;
  $('#tooldescription').textContent = tool.description;
  $('#toolnotice').textContent = tool.notice || '';
  $('#toolnotice').hidden = !tool.notice;
  $('#toolstatus').textContent = tool.message;
  $('#toolstatus').className = 'status' + (tool.state === 'error' ? ' err' : tool.state === 'sharing' ? ' ok' : '');
  $('#toolstart').disabled = toolPending || !!tool.running || payloadBusy || (!!activeTool && activeTool !== tool.id);
  $('#toolstop').disabled = toolPending || !tool.running;
  if (!toolStartupPending) $('#toolauto').checked = startupTool === tool.id;
  $('#toolauto').disabled = toolStartupPending;
  $('#toolsetup').hidden = !tool.setupUrl && !tool.setupPayloadName;
  $('#toolsetupdownload').hidden = !tool.setupUrl;
  $('#toolsetupdownload').href = tool.setupUrl || '#';
  $('#toolsetuptitle').textContent = tool.setupTitle || 'Setup';
  $('#toolsetuphint').textContent = tool.setupHint || '';
  $('#toolsetupmanual').textContent = tool.setupManual || '';
  $('#toolsetuppayload').hidden = !tool.setupPayloadName;
  $('#toolsetuppayload').disabled = toolPending || payloadBusy || !!activeTool;
  const details = $('#tooldetails');
  details.innerHTML = '';
  for (const item of tool.details || []) {
    const label = document.createElement('dt'), value = document.createElement('dd');
    label.textContent = item.label; value.textContent = item.value;
    details.appendChild(label); details.appendChild(value);
  }
}
async function toolAction(action) {
  if (!currentTool || toolPending) return;
  toolPending = true; paintTool();
  $('#toolactionstatus').textContent = '';
  try {
    await api('/api/tools/' + action, form({id:currentTool}));
    await refresh();
  } catch (e) { $('#toolactionstatus').textContent = e.message; }
  finally { toolPending = false; paintTool(); }
}
$('#toolstart').onclick = () => toolAction('start');
$('#toolstop').onclick = () => toolAction('stop');
$('#toolsetuppayload').onclick = async () => {
  const tool = toolItems.find(t => t.id === currentTool);
  if (!tool?.setupPayloadName || toolPending || payloadBusy || activeTool) return;
  try { await openPayload(tool.setupPayloadName); }
  catch (e) { $('#toolactionstatus').textContent = e.message; }
};
$('#toolauto').onchange = async () => {
  if (!currentTool || toolStartupPending) return;
  const previous = startupTool;
  toolStartupPending = true; paintTool();
  try {
    const id = $('#toolauto').checked ? currentTool : '';
    await api('/api/tools/startup', form({id}));
    startupTool = id;
    $('#toolactionstatus').textContent = 'Startup preference saved';
  } catch (e) { startupTool = previous; $('#toolactionstatus').textContent = e.message; }
  finally { toolStartupPending = false; paintTool(); }
};
stopBtn.onclick = () => { stopBtn.disabled = true; api('/api/stop', form({})); };
$('#clearlog').onclick = async () => {
  const r = await api('/api/log/clear', form({}));
  logSeq = (await r.json()).seq;
  logText = '';
  paintLog();
};

function paintLog() {
  logEl.textContent = logText || 'Nothing yet. The log fills up on the first run.';
  logEl.scrollTop = logEl.scrollHeight;
}

async function pollLog() {
  try {
    const d = await (await api('/api/log?since=' + logSeq)).json();
    if (d.text) {
      logText += d.text;
      if (logText.length > 8192) logText = logText.slice(-8192);
      paintLog();
    }
    logSeq = d.seq;
  } catch (e) {}
}

/* ---- settings ---- */
let rotation = 1, screenOn = true, ledOn = true;
let showAccess = true;
let screenBright = 100, ledBright = 20;
let lastScreenBright = 100, lastLedBright = 20;

function paintDisplay() {
  document.querySelectorAll('.orient button').forEach(b =>
    b.setAttribute('aria-checked', +b.dataset.rot === rotation));
  $('#screensw').setAttribute('aria-checked', screenOn);
  $('#accesssw').setAttribute('aria-checked', showAccess);
  $('#ledsw').setAttribute('aria-checked', ledOn);
  $('#ledlegend').hidden = !ledOn;
  $('#screenbright').value = screenBright;
  $('#ledbright').value = ledBright;
  $('#screenbrightval').textContent = screenBright;
  $('#ledbrightval').textContent = ledBright;
}

function paintLaunch(armedBytes) {
  const tag = $('#bootstate');
  const btn = $('#armboot');
  const on = armedBytes > 0;
  tag.textContent = on ? armedBytes + ' B armed' : 'Off';
  tag.classList.toggle('on', on);
  btn.setAttribute('aria-pressed', on);
  btn.textContent = on ? 'Disarm' : 'Fire after boot';
  $('#armedchip').hidden = !on;
}

$('#armedchip').onclick = () => {
  show('main');
  $('#armboot').scrollIntoView({block: 'center', behavior: 'smooth'});
};

document.querySelectorAll('.orient button').forEach(b => b.onclick = () => {
  if (screenLocked) return;
  rotation = +b.dataset.rot; paintDisplay();
});

/* ---- brightness clamp, now allowing true zero ---- */
function clampBright(n) {
  n = +n;
  if (isNaN(n)) n = 0;
  if (n < 0) n = 0;
  if (n > 100) n = 100;
  return n;
}

let brightTimer;
function displayFields(preview) {
  const fields = { led: ledOn ? 1 : 0, ledBright: preview || ledOn ? ledBright : lastLedBright };
  if (!screenLocked) {
    fields.screen = screenOn ? 1 : 0;
    fields.screenBright = preview || screenOn ? screenBright : lastScreenBright;
    if (!preview) {
      fields.rotation = rotation;
      fields.showAccess = showAccess ? 1 : 0;
    }
  }
  if (preview) fields.preview = 1;
  return fields;
}
function previewBright() {
  clearTimeout(brightTimer);
  brightTimer = setTimeout(() => {
    api('/api/settings/display', form(displayFields(true))).catch(e => {
      $('#dispstatus').className = 'status err';
      $('#dispstatus').textContent = e.message;
      refresh();
    });
  }, 80);
}

/* ---- screen: switch <-> slider ---- */
$('#screensw').onclick = () => {
  if (screenLocked) return;
  screenOn = !screenOn;
  if (!screenOn) {
    if (screenBright > 0) lastScreenBright = screenBright;
    screenBright = 0;
  } else {
    screenBright = lastScreenBright > 0 ? lastScreenBright : 100;
  }
  paintDisplay();
  previewBright();
};

$('#screenbright').oninput = () => {
  if (screenLocked) return;
  screenBright = clampBright($('#screenbright').value);
  if (screenBright > 0) lastScreenBright = screenBright;
  screenOn = screenBright > 0;
  $('#screensw').setAttribute('aria-checked', screenOn);
  $('#screenbrightval').textContent = screenBright;
  previewBright();
};

/* ---- LED: switch <-> slider ---- */
$('#ledsw').onclick = () => {
  ledOn = !ledOn;
  if (!ledOn) {
    if (ledBright > 0) lastLedBright = ledBright;
    ledBright = 0;
  } else {
    ledBright = lastLedBright > 0 ? lastLedBright : 20;
  }
  $('#ledlegend').hidden = !ledOn;
  paintDisplay();
  previewBright();
};

$('#ledbright').oninput = () => {
  ledBright = clampBright($('#ledbright').value);
  if (ledBright > 0) lastLedBright = ledBright;
  ledOn = ledBright > 0;
  $('#ledsw').setAttribute('aria-checked', ledOn);
  $('#ledlegend').hidden = !ledOn;
  $('#ledbrightval').textContent = ledBright;
  previewBright();
};

$('#accesssw').onclick = () => {
  if (screenLocked) return;
  showAccess = !showAccess; paintDisplay();
};

let usbDrive = false, sdPath = '/';

$('#savename').onclick = async () => {
  const st = $('#namestatus');
  st.className = 'status';
  try {
    await api('/api/settings/name', form({name: $('#devname').value.trim()}));
    st.className = 'status ok';
    st.textContent = 'Saved';
  } catch (e) { st.className = 'status err'; st.textContent = e.message; }
};

function fmtSize(n) {
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
  return (n / 1048576).toFixed(1) + ' MB';
}

async function sdBrowse(path) {
  const box = $('#sdlist');
  try {
    const r = await api('/api/sd/list?path=' + encodeURIComponent(path));
    const d = await r.json();
    sdPath = d.path;
    $('#sdpath').textContent = sdPath;
    box.innerHTML = '';
    if (!d.entries.length) {
      box.innerHTML = '<div class="sdnote">Empty folder.</div>';
      return;
    }
    d.entries.sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name));
    for (const e of d.entries) {
      const row = document.createElement('div');
      row.className = 'sdrow' + (e.dir ? ' dir' : '');
      const full = (sdPath === '/' ? '' : sdPath) + '/' + e.name;

      const nm = document.createElement('span');
      nm.className = 'fn';
      nm.textContent = e.dir ? e.name + '/' : e.name;
      if (e.dir) nm.onclick = () => sdBrowse(full);
      row.appendChild(nm);

      const sz = document.createElement('span');
      sz.className = 'fs';
      sz.textContent = e.dir ? '' : fmtSize(e.size);
      row.appendChild(sz);

      if (!e.dir) {
        const dl = document.createElement('button');
        dl.className = 'act';
        dl.textContent = 'Download';
        dl.onclick = () => { window.location = '/api/sd/download?path=' + encodeURIComponent(full); };
        row.appendChild(dl);
      }

      const rm = document.createElement('button');
      rm.className = 'act del';
      rm.textContent = 'Delete';
      rm.onclick = async () => {
        if (!confirm('Delete ' + e.name + '?')) return;
        try { await api('/api/sd/delete', form({path: full})); sdBrowse(sdPath); }
        catch (err) { box.innerHTML = '<div class="sdnote">' + err.message + '</div>'; }
      };
      row.appendChild(rm);

      box.appendChild(row);
    }
  } catch (e) {
    box.innerHTML = '<div class="sdnote">' + e.message + '</div>';
  }
}

$('#sdrefresh').onclick = () => sdBrowse(sdPath);
$('#sdup').onclick = () => {
  if (sdPath === '/') return;
  const up = sdPath.substring(0, sdPath.lastIndexOf('/')) || '/';
  sdBrowse(up);
};
$('#drivesw').onclick = async () => {
  usbDrive = !usbDrive;
  $('#drivesw').setAttribute('aria-checked', usbDrive);
  const st = $('#drivestatus');
  st.className = 'status';
  try {
    await api('/api/settings/drive', form({exposed: usbDrive ? 1 : 0}));
    st.textContent = usbDrive ? 'Attached to the host' : 'Detached';
    sdBrowse('/');
  } catch (e) {
    st.className = 'status err';
    st.textContent = e.message;
    usbDrive = false;
    $('#drivesw').setAttribute('aria-checked', false);
  }
};

$('#savedisplay').onclick = async () => {
  const st = $('#dispstatus');
  st.className = 'status';
  try {
    await api('/api/settings/display', form(displayFields(false)));
    st.className = 'status ok';
    st.textContent = screenLocked ? 'LED saved' : 'Saved';
  } catch (e) { st.className = 'status err'; st.textContent = e.message; }
};

$('#armboot').onclick = async () => {
  const st = $('#status');
  const arming = $('#armboot').getAttribute('aria-pressed') !== 'true';
  st.className = 'status';
  try {
    await api('/api/settings/launch', form({script: arming ? script.value : ''}));
    st.className = 'status ok';
    st.textContent = arming ? 'Armed for next boot' : 'Disarmed';
    refresh();
  } catch (e) { st.className = 'status err'; st.textContent = e.message; }
};

async function loadSettings() {
  $('#settingsstatus').textContent = '';
  try {
    const s = await (await api('/api/settings')).json();
    screenLocked = !!s.screenLocked;
    paintScreenLock();
    $('#ssid').value = s.ssid;
    $('#pass').value = s.password;
    $('#passhint').textContent = s.passwordMin + ' to ' + s.passwordMax + ' characters.';
    layout = s.layout;
    layouts = s.layouts || layouts;
    rotation = s.rotation;
    screenOn = !!s.screen;
    showAccess = !!s.showAccess;
    ledOn = !!s.led;
    screenBright = clampBright(s.screenBright == null ? 100 : s.screenBright);
    ledBright = clampBright(s.ledBright == null ? 20 : s.ledBright);
    if (screenBright > 0) lastScreenBright = screenBright;
    if (ledBright > 0) lastLedBright = ledBright;
    if (!screenOn) screenBright = 0;
    else if (screenBright === 0) screenBright = lastScreenBright;
    if (!ledOn) ledBright = 0;
    else if (ledBright === 0) ledBright = lastLedBright;
    if (document.activeElement !== $('#delay')) $('#delay').value = s.startDelay;
    if (document.activeElement !== $('#devname')) $('#devname').value = s.deviceName;
    if (document.activeElement !== $('#unlockseq')) $('#unlockseq').value = s.unlockSeq;
    if (document.activeElement !== $('#hardlockseq')) $('#hardlockseq').value = s.hardlockSeq;
    if (document.activeElement !== $('#longpress')) $('#longpress').value = s.longPressMs;
    if (document.activeElement !== $('#hardlockre')) $('#hardlockre').value = s.hardlockReinserts;
    $('#unlockseq').maxLength = $('#hardlockseq').maxLength = s.lockSeqMax || 12;
    $('#longpress').min = s.longPressMin;
    $('#longpress').max = s.longPressMax;
    if (s.hardlockReinsertsMin != null) $('#hardlockre').min = s.hardlockReinsertsMin;
    if (s.hardlockReinsertsMax != null) $('#hardlockre').max = s.hardlockReinsertsMax;
    hardlockEnabled = !!s.hardlockEnabled;
    standbyOnBoot = s.standbyOnBoot !== 0;
    $('#standbysw').setAttribute('aria-checked', standbyOnBoot);
    paintHardlock();
    usbDrive = !!s.usbDrive;
    $('#drivesw').setAttribute('aria-checked', usbDrive);
    $('#drivehint').textContent = s.usbCard
      ? ('Card detected, ' + s.usbSizeMB + ' MB.')
      : 'No card in the slot.';
    sdBrowse(sdPath);
    paintLayout();
    paintDisplay();
  } catch (e) { $('#settingsstatus').textContent = 'Could not load settings. Reopen settings to try again.'; }
}

$('#reveal').onchange = e => { $('#pass').type = e.target.checked ? 'text' : 'password'; };

function restarting(net) {
  poll = false;
  $('#overlaytitle').textContent = 'Restarting device';
  $('#overlaymessage').textContent = 'Unlock if in standby, then reconnect to this network.';
  $('#newnet').textContent = net;
  $('#overlay').hidden = false;
}

$('#savewifi').onclick = async () => {
  const ssid = $('#ssid').value, password = $('#pass').value;
  const ws = $('#wifistatus');
  ws.className = 'status';
  try {
    await api('/api/settings/wifi', form({ssid, password}));
    restarting(ssid);
  } catch (e) { ws.className = 'status err'; ws.textContent = e.message; }
};

let hardlockEnabled = true, standbyOnBoot = true;
$('#standbysw').onclick = () => {
  standbyOnBoot = !standbyOnBoot;
  $('#standbysw').setAttribute('aria-checked', standbyOnBoot);
};
function paintHardlock() {
  $('#hardlocksw').setAttribute('aria-checked', hardlockEnabled);
  $('#hardlockopts').hidden = !hardlockEnabled;
}
$('#hardlocksw').onclick = () => { hardlockEnabled = !hardlockEnabled; paintHardlock(); };

$('#savesecurity').onclick = async () => {
  const st = $('#securitystatus');
  st.className = 'status';
  try {
    const unlockSeq = $('#unlockseq').value.trim().toUpperCase();
    const hardlockSeq = $('#hardlockseq').value.trim().toUpperCase();
    if (unlockSeq && hardlockSeq &&
        (unlockSeq.includes(hardlockSeq) || hardlockSeq.includes(unlockSeq))) {
      throw new Error('Unlock and hard-lock sequences must differ; neither may contain the other');
    }
    await api('/api/settings/security', form({
      unlockSeq, hardlockSeq,
      standbyOnBoot: standbyOnBoot ? 1 : 0,
      longPressMs: $('#longpress').value,
      hardlockEnabled: hardlockEnabled ? 1 : 0,
      hardlockReinserts: $('#hardlockre').value
    }));
    st.className = 'status ok';
    st.textContent = 'Saved · applies next boot';
  } catch (e) { st.className = 'status err'; st.textContent = e.message; }
};

// The topbar button both shows and toggles the screen lock. When the display
// is locked the button lights up and the screen controls are disabled.
let screenLocked = false;
let screenControlsLocked = false, screenLockBusy = false;
function paintScreenLock() {
  if (screenLocked && !screenControlsLocked) clearTimeout(brightTimer);
  screenControlsLocked = screenLocked;
  const screenControls = [$('#screensw'), $('#screenbright'), $('#accesssw'),
    ...document.querySelectorAll('.orient button')];
  screenControls.forEach(control => {
    control.disabled = screenLocked;
    control.setAttribute('aria-describedby', 'screenlockhint');
  });
  const b = $('#lockdisplay');
  b.disabled = screenLockBusy;
  b.classList.toggle('active', screenLocked);
  b.setAttribute('aria-pressed', screenLocked);
  b.setAttribute('aria-label', screenLocked ? 'Unlock screen' : 'Lock screen');
  $('#screenlocknotice').hidden = !screenLocked;
  $('#screenlockhint').hidden = !screenLocked;
  $('#unlockscreen').hidden = !screenLocked;
  $('#unlockscreen').disabled = screenLockBusy;
  b.title = screenLocked
    ? 'Unlock screen'
    : 'Lock screen; Wi-Fi and keyboard stay on';
}
async function setScreenLock(locked) {
  if (screenLockBusy) return;
  screenLockBusy = true;
  clearTimeout(brightTimer);
  paintScreenLock();
  try {
    const r = await api(locked ? '/api/lock-display' : '/api/unlock-display', form({}));
    const d = await r.json();
    screenLocked = !!d.screenLocked;
    paintScreenLock();
  } catch (e) {
    $('#dispstatus').className = 'status err';
    $('#dispstatus').textContent = e.message;
  } finally {
    screenLockBusy = false;
    paintScreenLock();
  }
}
$('#lockdisplay').onclick = () => setScreenLock(!screenLocked);
$('#unlockscreen').onclick = () => setScreenLock(false);

$('#hardlocknow').onclick = async () => {
  if (!confirm('Hard lock now? This stops payloads, turns off Wi-Fi, and disconnects this page. Recovery requires the configured reinsertions or a factory reset.')) return;
  const st = $('#lockresetstatus');
  st.className = 'status';
  $('#hardlocknow').disabled = true;
  try {
    const result = await (await api('/api/hard-lock', form({}))).json();
    poll = false;
    clearTimeout(brightTimer);
    $('#overlaytitle').textContent = 'Device hard-locked';
    $('#overlaymessage').textContent = 'Screen, LED, Wi-Fi and keyboard are off. This page is disconnected.';
    $('#newnet').textContent = 'Replug ' + (result.hardlockReinserts + 1) + ' times to recover, or hold the device button for 10 seconds to reset.';
    $('#overlay').hidden = false;
  } catch (e) {
    st.className = 'status err';
    st.textContent = e.message;
    $('#hardlocknow').disabled = false;
  }
};

$('#reset').onclick = async () => {
  if (!confirm('Restore all settings and restart? Payloads are kept. Hard lock and armed boot runs are cleared.')) return;
  try {
    await api('/api/settings/reset', form({}));
    restarting('the built-in network shown on the screen');
  } catch (e) {
    $('#resetstatus').className = 'status err';
    $('#resetstatus').textContent = e.message;
  }
};

/* ---- polling ---- */
function setBusy(busy) {
  runBtn.disabled = busy || !!activeTool;
  stopBtn.disabled = !busy;
  $('#delay').disabled = busy;
  $('#dot').className = 'dot ' + (busy ? 'busy' : 'on');
}

function paintState(s) {
  const armed = s.state === 'armed';
  const busy = armed || s.state === 'running';
  payloadBusy = busy;
  activeTool = s.activeTool || '';
  if (!toolStartupPending) startupTool = s.startupTool || '';
  toolItems = s.tools || [];
  setBusy(busy);
  $('#host').className = 'host' + (s.hostSeen ? ' ready' : '');
  $('#hostlab').textContent = s.hostSeen ? 'Host ready' : 'No host yet';

  status.className = 'status' + (s.state === 'error' ? ' err' : s.state === 'done' ? ' ok' : '');
  if (armed) status.textContent = 'Starting in ' + s.countdown + ' s';
  else if (s.state === 'running') status.textContent = 'Line ' + s.line + ' of ' + s.total;
  else status.textContent = activeTool ? 'Stop the active tool to run a payload' : s.message || 'Ready';

  if (s.layout !== layout || s.arrangement !== arrangement) {
    layout = s.layout;
    arrangement = s.arrangement;
    paintLayout();
  }
  if (s.version) $('#ver').textContent = 'v' + s.version;
}

async function refresh() {
  if (!poll) return;
  let s;
  try {
    s = await (await api('/api/state')).json();
  } catch (e) {
    $('#dot').className = 'dot';
    return;
  }
  try { paintState(s); } catch (e) { console.error('paintState', e); }
  lastPayloads = s.payloads || [];
  try { paintList(lastPayloads, toolItems); paintTool(); } catch (e) { console.error('paintList', e); }
  try { paintLaunch(s.armed || 0); } catch (e) { console.error('paintLaunch', e); }
  try { screenLocked = !!s.screenLocked; paintScreenLock(); } catch (e) {}
  pollLog();
}

setInterval(refresh, 1000);
paintLayout();
refresh();
api('/api/settings').then(r => r.json())
  .then(s => { $('#delay').value = s.startDelay; })
  .catch(() => {});
</script>
</body>
</html>)HTML";
