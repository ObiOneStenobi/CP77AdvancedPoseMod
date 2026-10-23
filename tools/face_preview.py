"""Writes a self-contained HTML preview of the expression editor poses (face/*.json) on the face_rig bones.

Usage: python tools/face_preview.py [--work-dir <dir>] [--out <file.html>]

Reads dist/red4ext/plugins/AdvancedPoseMod/face/<head>.json and the face_rig of the same head from the work folder of
tools/extract_face_poses.ps1 (default %TEMP%\\apm_face, json\\h0_000_<head>_c__basehead_skeleton.rig.json); the page is
written there (face_preview.html). The bones are drawn as dots, front and side view, in the rest pose (grey) and posed.

The pose math is the one planned for the plugin: for a weight w between two inbetweens (the first one is the rest
pose at weight 0) the rotation is slerped and the translation lerped; poses are summed per bone (rotations multiplied in
pose order, translations added) and a bone's local transform is rest rotation * pose rotation, rest translation + pose
translation.
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FACE_DIR = os.path.join(ROOT, "dist", "red4ext", "plugins", "AdvancedPoseMod", "face")


def load_rig(path):
    with open(path, encoding="utf-8") as f:
        rig = json.load(f)["Data"]["RootChunk"]
    rest = []
    for t in rig["boneTransforms"]:
        q, p = t["Rotation"], t["Translation"]
        rest.append([q["i"], q["j"], q["k"], q["r"], p["X"], p["Y"], p["Z"]])
    return {"names": [x["$value"] for x in rig["boneNames"]], "parents": rig["boneParentIndexes"], "rest": rest}


def main(argv):
    work = os.path.join(os.environ.get("TEMP", "."), "apm_face")
    out = None
    while argv:
        if argv[0] == "--work-dir":
            work = argv[1]
        elif argv[0] == "--out":
            out = argv[1]
        else:
            sys.exit(__doc__)
        argv = argv[2:]
    out = out or os.path.join(work, "face_preview.html")
    heads = {}
    for file in sorted(os.listdir(FACE_DIR)):
        if not file.endswith(".json"):
            continue
        key = file[:-5]
        with open(os.path.join(FACE_DIR, file), encoding="utf-8") as f:
            data = json.load(f)
        rig = os.path.join(work, "json", data["head"] + "_skeleton.rig.json")
        if not os.path.exists(rig):
            sys.exit(f"Missing {rig} (run tools/extract_face_poses.ps1 first)")
        heads[key] = {"data": data, "rig": load_rig(rig)}
    if not heads:
        sys.exit(f"No face data in {FACE_DIR}")
    page = PAGE.replace("/*DATA*/null", json.dumps(heads, separators=(",", ":")))
    with open(out, "w", encoding="utf-8") as f:
        f.write(page)
    print(f"Written {out} ({len(page) / 1024:.0f} KB, heads: {', '.join(heads)})")


PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Face Pose Preview</title>
<style>
:root{--bg:#16181c;--panel:#1f2228;--fg:#e4e6ea;--dim:#8a909a;--accent:#4fb3ff;--rest:#5a606a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:13px system-ui,sans-serif;display:flex;height:100vh}
#side{width:360px;overflow-y:auto;background:var(--panel);padding:10px 12px}
#views{flex:1;display:flex;flex-direction:column;min-width:0}
#bar{display:flex;gap:10px;align-items:center;padding:8px 12px;flex-wrap:wrap}
#canvases{flex:1;display:flex;min-height:0}canvas{flex:1;min-width:0;width:50%;height:100%}
h3{margin:12px 0 4px;font-size:12px;text-transform:uppercase;color:var(--dim);letter-spacing:.05em}
.row{display:grid;grid-template-columns:1fr 110px 34px;gap:6px;align-items:center;padding:1px 0}
.row span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.row b{font-weight:400;color:var(--dim);text-align:right}
.row.on span{color:var(--accent)}input[type=range]{width:100%}input[type=text]{width:100%;padding:4px;background:var(--bg);color:var(--fg);border:1px solid #333}
button,select{background:#2c3038;color:var(--fg);border:1px solid #3a3f48;padding:3px 8px;border-radius:3px}
#info{color:var(--dim)}
</style></head><body>
<div id="side"><input id="filter" type="text" placeholder="Filter poses"><div id="list"></div></div>
<div id="views">
 <div id="bar"><select id="head"></select><label><input id="link" type="checkbox" checked> L+R linked</label>
  <label><input id="restOn" type="checkbox" checked> rest</label><button id="reset">Reset</button>
  <span id="info"></span></div>
 <div id="canvases"><canvas id="front"></canvas><canvas id="profile"></canvas></div>
</div>
<script>
const HEADS=/*DATA*/null;
let head=Object.keys(HEADS)[0],weights={},zoom=1;
const $=id=>document.getElementById(id);
const qmul=(a,b)=>[a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
 a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]];
function qrot(q,v){const p=qmul(qmul(q,[v[0],v[1],v[2],0]),[-q[0],-q[1],-q[2],q[3]]);return[p[0],p[1],p[2]]}
function slerp(a,b,t){let d=a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3];if(d<0){b=b.map(x=>-x);d=-d}
 if(d>0.9995){const r=a.map((x,i)=>x+(b[i]-x)*t),n=Math.hypot(...r);return r.map(x=>x/n)}
 const th=Math.acos(d),s=Math.sin(th),wa=Math.sin((1-t)*th)/s,wb=Math.sin(t*th)/s;return a.map((x,i)=>x*wa+b[i]*wb)}
// delta of one pose at weight w: {rigBone:[q,t]}
function poseDelta(pose,w,map){const ibs=pose.inbetweens;let lo={w:0,t:[]},hi=ibs[ibs.length-1];
 for(const ib of ibs){if(ib.w>=w){hi=ib;break}lo=ib}
 const f=hi.w>lo.w?Math.min(1,(w-lo.w)/(hi.w-lo.w)):1,get=ib=>{const m={};for(const r of ib.t)m[r[0]]=r;return m};
 const a=get(lo),b=get(hi),out={};
 for(const k of new Set([...Object.keys(a),...Object.keys(b)])){const ra=a[k]||[0,0,0,0,1,0,0,0],rb=b[k]||[0,0,0,0,1,0,0,0];
  out[map[k]]=[slerp(ra.slice(1,5),rb.slice(1,5),f),[0,1,2].map(i=>ra[5+i]+(rb[5+i]-ra[5+i])*f)]}
 return out}
function solve(useWeights){const H=HEADS[head],rig=H.rig,n=rig.names.length,map=H.data.bones.map(b=>rig.names.indexOf(b));
 const local=rig.rest.map(r=>[r.slice(0,4),r.slice(4,7)]);
 if(useWeights)for(const p of H.data.poses){const w=weights[p.name]||0;if(w<=0)continue;
  const d=poseDelta(p,w,map);for(const k in d){if(k<0)continue;const L=local[k];L[0]=qmul(L[0],d[k][0]);L[1]=L[1].map((x,i)=>x+d[k][1][i])}}
 const wq=new Array(n),wp=new Array(n);
 for(let i=0;i<n;i++){const p=rig.parents[i],L=local[i];
  if(p<0||p>=i){wq[i]=L[0];wp[i]=L[1]}else{wq[i]=qmul(wq[p],L[0]);const v=qrot(wq[p],L[1]);wp[i]=wp[p].map((x,j)=>x+v[j])}}
 return wp}
function draw(){const rest=solve(false),posed=solve(true),H=HEADS[head],used=new Set(H.data.bones.map(b=>H.rig.names.indexOf(b)));
 let moved=0,maxd=0;posed.forEach((p,i)=>{const d=Math.hypot(p[0]-rest[i][0],p[1]-rest[i][1],p[2]-rest[i][2]);if(d>1e-5)moved++;maxd=Math.max(maxd,d)});
 $('info').textContent=`${H.data.head} · ${H.data.poses.length} poses · moved ${moved} bones, max ${(maxd*1000).toFixed(1)} mm`;
 for(const [id,ax] of [['front',0],['profile',1]]){const c=$(id),r=c.getBoundingClientRect(),dpr=devicePixelRatio||1;
  c.width=r.width*dpr;c.height=r.height*dpr;const g=c.getContext('2d');g.scale(dpr,dpr);
  const pts=[...used].map(i=>rest[i]);const xs=pts.map(p=>p[ax]),zs=pts.map(p=>p[2]);
  const cx=(Math.min(...xs)+Math.max(...xs))/2,cz=(Math.min(...zs)+Math.max(...zs))/2;
  const span=Math.max(Math.max(...xs)-Math.min(...xs),Math.max(...zs)-Math.min(...zs));
  const s=Math.min(r.width,r.height)*0.85/span*zoom,P=p=>[r.width/2+(p[ax]-cx)*s*(ax===1?-1:1),r.height/2-(p[2]-cz)*s];
  g.fillStyle=getComputedStyle(document.body).getPropertyValue('--dim');g.fillText(ax?'side':'front',8,14);
  for(const i of used){const a=P(rest[i]),b=P(posed[i]);
   if($('restOn').checked){g.fillStyle='#5a606a';g.fillRect(a[0]-1.5,a[1]-1.5,3,3)}
   const d=Math.hypot(posed[i][0]-rest[i][0],posed[i][1]-rest[i][1],posed[i][2]-rest[i][2]);
   if(d>1e-5){g.strokeStyle='rgba(79,179,255,.5)';g.beginPath();g.moveTo(a[0],a[1]);g.lineTo(b[0],b[1]);g.stroke()}
   g.fillStyle=d>1e-5?'#4fb3ff':'#c9ccd2';g.beginPath();g.arc(b[0],b[1],2.2,0,7);g.fill()}}}
function build(){const list=$('list'),f=$('filter').value.toLowerCase();list.innerHTML='';let zone='';
 for(const p of HEADS[head].data.poses){if(f&&!p.name.toLowerCase().includes(f)&&!p.zone.includes(f))continue;
  if(p.zone!==zone){zone=p.zone;const h=document.createElement('h3');h.textContent=zone;list.appendChild(h)}
  const row=document.createElement('div');row.className='row'+((weights[p.name]||0)>0?' on':'');
  const w=Math.round((weights[p.name]||0)*100);
  row.innerHTML=`<span title="${p.name} · ${p.inbetweens.map(i=>i.w).join('/')}">${p.name}</span><input type=range min=0 max=100 value=${w}><b>${w}</b>`;
  row.querySelector('input').oninput=e=>{const v=e.target.value/100;weights[p.name]=v;
   if($('link').checked&&p.mirror)weights[p.mirror]=v;build();draw()};
  list.appendChild(row)}}
for(const k in HEADS){const o=document.createElement('option');o.value=k;o.textContent=k+' · '+HEADS[k].data.head;$('head').appendChild(o)}
$('head').onchange=e=>{head=e.target.value;build();draw()};$('filter').oninput=build;$('restOn').onchange=draw;
$('reset').onclick=()=>{weights={};build();draw()};
document.querySelectorAll('canvas').forEach(c=>c.addEventListener('wheel',e=>{e.preventDefault();zoom*=e.deltaY<0?1.1:1/1.1;draw()},{passive:false}));
addEventListener('resize',draw);build();draw();
</script></body></html>
"""

if __name__ == "__main__":
    main(sys.argv[1:])
