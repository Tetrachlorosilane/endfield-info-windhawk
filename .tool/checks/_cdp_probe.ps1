# One-shot CDP probe: dump the vinyl page anchors the lyric source wants.
# Usage: pwsh -File .tool/checks/_cdp_probe.ps1 [-Port 9229] [-Out path]
param([int]$Port = 9229, [string]$Out = '')
$ErrorActionPreference = 'Continue'

$list = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/json/list" -TimeoutSec 5 -ErrorAction Stop
Write-Host "=== /json/list ($Port) ==="
foreach ($t in $list) {
    Write-Host ("id={0} type={1} title='{2}'" -f $t.id, $t.type, $t.title)
    Write-Host ("  url={0}" -f $t.url)
    Write-Host ("  ws={0}" -f $t.webSocketDebuggerUrl)
    Write-Host ("  devtoolsFrontendUrl={0}" -f $t.devtoolsFrontendUrl)
}

$expr = @'
(function(){
  function q(s){ try { return document.querySelector(s); } catch(e){ return null; } }
  function qa(s){ try { return document.querySelectorAll(s); } catch(e){ return []; } }
  function outer(e,n){ try { return e ? ('' + e.outerHTML).slice(0,n||600) : null; } catch(x){ return 'ERR'; } }
  var o = { href: location.href, docTitle: document.title, readyState: document.readyState };
  o.vinylRoot = !!document.getElementById('VINYL_COVER_ELEMENT_ID');
  o.vinylRootOuter = outer(document.getElementById('VINYL_COVER_ELEMENT_ID'), 400);
  var cur = q('li.line.current');
  o.curLineFound = !!cur;
  o.curLineOuter = outer(cur, 600);
  o.curLineDataTime = cur ? cur.getAttribute('data-time') : null;
  o.lineCount = qa('li.line').length;
  o.currentCount = qa('li.current').length;
  // first 3 lines, to see the shape
  var ls = qa('li.line'); var samp = [];
  for (var i=0;i<ls.length && i<3;i++) samp.push(outer(ls[i], 260));
  o.firstLines = samp;
  // title candidates
  var cands = [
    '.TrackTitleContainer_t1s72en4 span.title',
    '#VINYL_COVER_ELEMENT_ID span.title',
    'span.cmd-typography.title',
    'span.title',
    '.display span.title'
  ];
  o.titleCands = [];
  for (var c=0;c<cands.length;c++){
    var e = q(cands[c]);
    o.titleCands.push({ sel: cands[c], found: !!e,
      attr: e ? e.getAttribute('title') : null,
      text: e ? ('' + e.textContent).slice(0,120) : null,
      innerText: e ? ('' + (e.innerText||'')).slice(0,120) : null,
      outer: outer(e, 300) });
  }
  // time sources
  o.media = [];
  var mv = qa('audio,video');
  for (var m=0;m<mv.length;m++){ o.media.push({tag:mv[m].tagName, t:mv[m].currentTime, dur:mv[m].duration, paused:mv[m].paused}); }
  o.timeTexts = [];
  var all = document.querySelectorAll('*');
  for (var i2=0;i2<all.length && o.timeTexts.length<8;i2++){
    var t = ('' + (all[i2].innerText||'')).trim();
    if (/^\d{1,3}:\d{2}(\s*\/\s*\d{1,3}:\d{2})?$/.test(t)) {
      o.timeTexts.push({ c: (''+(all[i2].className||'')).slice(0,50), t: t });
    }
  }
  // store scalars: what does the store say the line number is right now
  try {
    if(!window.__ei_probe_require__){
      webpackJsonp.push([[987661],{987661:function(m,e,require){window.__ei_probe_require__=require;}},[[987661]]]);
    }
    var req=window.__ei_probe_require__, cache=req.c||{}, id=window.__ei_store_id__;
    if(!id){
      for(var k in cache){ try{ var ex=cache[k]&&cache[k].exports; if(!ex) continue;
        var cd=[ex,ex.a,ex.default];
        for(var i3=0;i3<cd.length;i3++){ if(cd[i3]&&typeof cd[i3].getStore==='function'){ id=k; window.__ei_store_id__=k; break; } }
      }catch(e){} if(id) break; }
    }
    o.storeId = id || null;
    if(id){
      var ex2=cache[id].exports;
      var h=[ex2,ex2.a,ex2.default].filter(function(x){return x&&typeof x.getStore==='function';})[0];
      var g=h.getStore(), st=(g&&typeof g.getState==='function')?g.getState():g;
      var p=(st&&st.playing)||{}, a=(st&&st['async:lyric'])||{};
      o.store = { name:p.resourceName||'', trackId:p.resourceTrackId||'', state:p.playingState,
        lineNum:p.playingLyricLineNumber, lines:(a.lyricLines||[]).length,
        tlines:(a.tlyricLines||[]).length, offset:a.offset, usedLyric:a.currentUsedLyric,
        durationMs:(p.curTrack&&p.curTrack.duration)||0 };
    }
  } catch(x){ o.storeError = '' + x; }
  return JSON.stringify(o);
})()
'@

function Invoke-Eval {
    param([string]$Url, [string]$Expression)
    $ws = New-Object System.Net.WebSockets.ClientWebSocket
    $cts = New-Object System.Threading.CancellationTokenSource
    $cts.CancelAfter(5000)
    $ws.ConnectAsync([Uri]$Url, $cts.Token).Wait(5000) | Out-Null
    $payload = @{ id = 1; method = 'Runtime.evaluate'; params = @{ expression = $Expression; returnByValue = $true } } |
        ConvertTo-Json -Depth 6 -Compress
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($payload)
    $seg = New-Object System.ArraySegment[byte] -ArgumentList @(, $bytes)
    $ws.SendAsync($seg, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, $cts.Token).Wait(5000) | Out-Null
    $buf = New-Object byte[] 65536
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt 40; $i++) {
        $recvSeg = New-Object System.ArraySegment[byte] -ArgumentList @(, $buf)
        $r = $ws.ReceiveAsync($recvSeg, $cts.Token)
        $r.Wait(5000) | Out-Null
        [void]$sb.Append([System.Text.Encoding]::UTF8.GetString($buf, 0, $r.Result.Count))
        if ($r.Result.EndOfMessage -and $sb.ToString() -match '"id"\s*:\s*1') { break }
    }
    try { $ws.Dispose() } catch { }
    return $sb.ToString()
}

$report = New-Object System.Text.StringBuilder
foreach ($t in $list) {
    if (-not $t.webSocketDebuggerUrl) { continue }
    $raw = Invoke-Eval -Url $t.webSocketDebuggerUrl -Expression $expr
    [void]$report.AppendLine("=== target id=$($t.id) title='$($t.title)' url=$($t.url) ===")
    [void]$report.AppendLine($raw)
    [void]$report.AppendLine('')
    Write-Host "=== target id=$($t.id) title='$($t.title)' ==="
    $m = [regex]::Match($raw, '"value"\s*:\s*"((?:[^"\\]|\\.)*)"', 'Singleline')
    if ($m.Success) {
        try {
            $inner = ('"' + $m.Groups[1].Value + '"') | ConvertFrom-Json
            $inner | ConvertFrom-Json | ConvertTo-Json -Depth 8
        } catch { Write-Host "parse failed"; Write-Host $raw }
    } else { Write-Host "no value in reply:"; Write-Host $raw }
}
if ($Out) { [System.IO.File]::WriteAllText($Out, $report.ToString(), (New-Object System.Text.UTF8Encoding($false))); Write-Host "saved: $Out" }
