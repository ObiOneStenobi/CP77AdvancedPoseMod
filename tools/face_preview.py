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
h3{margin:12px 0 4px;font-size:12px;text-transform:uppercase;color:var(--dim);letter-spacing:.05em;display:flex;
 align-items:center;gap:6px;cursor:pointer;user-select:none}h3 i{font-style:normal;width:10px}h3 button{margin-left:auto;font-size:11px;padding:1px 6px}
.row.bi input{background:linear-gradient(90deg,transparent 49.5%,#555 49.5%,#555 50.5%,transparent 50.5%)}
.row{display:grid;grid-template-columns:1fr 110px 34px;gap:6px;align-items:center;padding:1px 0}
.row span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.row b{font-weight:400;color:var(--dim);text-align:right}
.row.on span{color:var(--accent)}input[type=range]{width:100%}input[type=text]{width:100%;padding:4px;background:var(--bg);color:var(--fg);border:1px solid #333}
button,select{background:#2c3038;color:var(--fg);border:1px solid #3a3f48;padding:3px 8px;border-radius:3px}
#info{color:var(--dim)}
</style></head><body>
<div id="side"><input id="filter" type="text" placeholder="Filter poses"><div id="list"></div></div>
<div id="views">
 <div id="bar"><select id="head"></select><label><input id="link" type="checkbox" checked> L+R linked</label>
  <label><input id="restOn" type="checkbox" checked> rest</label><button id="reset">Reset</button><span id="count"></span>
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
// most used poses first in each zone (the others follow in file order); opposite poses share one -100..100 slider
const ORDER=['brows_raise_in','brows_raise_out','brows_lower','brows_lateral','blink','widen','oculi_squint_outer_lower',
 'oculi_squint_outer_upper','oculi_squint_inner','snear','breathe_in','compress','puff','suck','corner_up','corner_dn',
 'purse','funnel','corner_stretch','upper_raise','lower_raise','stretch','corner_wide','pull','nasolabialDeepener',
 'corner_sharp_up','suck_up','suck_dn','puff_up','puff_dn','apart_up','apart_dn','chin_raise','lips_mid_shift_l','lips_mid_shift_up',
 'jaw_mid_open','jaw_mid_shift_l','jaw_mid_shift_back'];
const BIPOLAR=[['nose_l_breathe_in','nose_l_breathe_out'],['nose_r_breathe_in','nose_r_breathe_out'],
 ['lips_mid_shift_l','lips_mid_shift_r'],['lips_mid_shift_dn','lips_mid_shift_up'],['jaw_mid_shift_l','jaw_mid_shift_r'],
 ['jaw_mid_shift_back','jaw_mid_shift_fwd'],['tongue_mid_base_l','tongue_mid_base_r'],['tongue_mid_base_dn','tongue_mid_base_up'],
 ['tongue_mid_base_back','tongue_mid_base_front'],['tongue_mid_tip_l','tongue_mid_tip_r'],['tongue_mid_tip_dn','tongue_mid_tip_up'],
 ['tongue_mid_twist_l','tongue_mid_twist_r']];
const ZONES=['brows','eyelids','nose','cheeks','lips','jaw','ears','tongue'],closed={tongue:true};
const rank=n=>{const s=n.replace(/^[a-z]+_[lr]_/,m=>m.split('_')[0]+'_');const i=ORDER.findIndex(o=>s.endsWith(o)||s===o);return i<0?999:i};
const pretty=s=>s.replace(/_/g,' ');
// one control per slider: {zone,label,neg?,pos,title}
function controls(){const poses=HEADS[head].data.poses,by={},link=$('link').checked,out=[],done=new Set();
 for(const p of poses)by[p.name]=p;
 for(const p of poses){if(done.has(p.name))continue;
  const bi=BIPOLAR.find(b=>b.includes(p.name)&&by[b[0]]&&by[b[1]]);
  if(link&&p.side==='r'&&p.mirror)continue;
  let c;if(bi){[bi[0],bi[1]].forEach(n=>done.add(n));
   let common=bi[1].split('_').filter((w,i)=>w===bi[0].split('_')[i]).join('_');
   if(link&&p.mirror)common=common.replace(/^([a-z]+)_l(_|$)/,'$1$2')+'  L+R:';
   c={zone:p.zone,neg:bi[0],pos:bi[1],label:`${pretty(common)}  ${bi[0].split('_').pop()} ↔ ${bi[1].split('_').pop()}`}}
  else{done.add(p.name);c={zone:p.zone,pos:p.name,label:pretty(link&&p.mirror?p.name.replace(/^([a-z]+)_l_/,'$1_'):p.name)+(link&&p.mirror?'  L+R':'')}}
  if(link&&p.side==='l'&&p.mirror)c.mirror={[p.name]:p.mirror,...(c.neg?{[c.neg]:by[c.neg].mirror,[c.pos]:by[c.pos].mirror}:{})};
  c.rank=Math.min(rank(c.pos),c.neg?rank(c.neg):999);out.push(c)}
 return out.sort((a,b)=>ZONES.indexOf(a.zone)-ZONES.indexOf(b.zone)||a.rank-b.rank)}
function setW(c,v){const put=(n,w)=>{weights[n]=w;const m=c.mirror&&c.mirror[n];if(m)weights[m]=w};
 if(c.neg){put(c.neg,Math.max(0,-v));put(c.pos,Math.max(0,v))}else put(c.pos,v)}
function build(){const list=$('list'),f=$('filter').value.toLowerCase();list.innerHTML='';let zone='',all=controls(),count=0;
 for(const c of all){if(f&&!(c.label+c.pos+(c.neg||'')).toLowerCase().includes(f)&&!c.zone.includes(f))continue;
  if(c.zone!==zone){zone=c.zone;const z=zone,n=all.filter(x=>x.zone===z).length,h=document.createElement('h3');
   h.innerHTML=`<i>${closed[z]&&!f?'▸':'▾'}</i>${z} <span>(${n})</span><button>Reset</button>`;
   h.onclick=()=>{closed[z]=!closed[z];build()};
   h.querySelector('button').onclick=e=>{e.stopPropagation();all.filter(x=>x.zone===z).forEach(x=>setW(x,0));build();draw()};
   list.appendChild(h)}
  if(closed[zone]&&!f)continue;count++;
  const v=c.neg?(weights[c.pos]||0)-(weights[c.neg]||0):(weights[c.pos]||0),w=Math.round(v*100);
  const row=document.createElement('div');row.className='row'+(w?' on':'')+(c.neg?' bi':'');
  row.innerHTML=`<span title="${c.neg?c.neg+' / ':''}${c.pos}">${c.label}</span><input type=range min=${c.neg?-100:0} max=100 value=${w}><b>${w}</b>`;
  // update only this row while dragging: rebuilding the list would drop the slider under the mouse
  row.querySelector('input').oninput=e=>{const x=+e.target.value;setW(c,x/100);row.querySelector('b').textContent=x;
   row.classList.toggle('on',x!==0);draw()};
  list.appendChild(row)}
 $('count').textContent=`${all.length} sliders`}
for(const k in HEADS){const o=document.createElement('option');o.value=k;o.textContent=k+' · '+HEADS[k].data.head;$('head').appendChild(o)}
$('head').onchange=e=>{head=e.target.value;build();draw()};$('filter').oninput=build;$('restOn').onchange=draw;$('link').onchange=build;
$('reset').onclick=()=>{weights={};build();draw()};
document.querySelectorAll('canvas').forEach(c=>c.addEventListener('wheel',e=>{e.preventDefault();zoom*=e.deltaY<0?1.1:1/1.1;draw()},{passive:false}));
addEventListener('resize',draw);build();draw();
</script></body></html>
"""

if __name__ == "__main__":
    main(sys.argv[1:])
