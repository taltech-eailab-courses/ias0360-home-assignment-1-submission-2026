"""Validate the three recorded datasets and reproduce report tables/figure.
Run from any directory with Python 3 and matplotlib installed.
Each observation is one per-axis feature vector for a 256-sample window.
"""
import csv
import hashlib
import json
import math
import statistics as st
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent
RESULTS = HERE.parent / 'results'
FILES = {
    'Stationary': 'stationary_20260926_152045.csv',
    'Slow': 'slow_movement_20260926_152151.csv',
    'Faster': 'fast_movement_20260926_152235.csv',
}
all_rows, summaries, provenance = {}, [], []
for condition, filename in FILES.items():
    path = RESULTS / filename
    with path.open() as f:
        reader = csv.DictReader(f)
        assert reader.fieldnames == 'window,axis,fs_hz,min_dt_us,max_dt_us,mean_g,variance_g2,std_g,min_g,max_g,peak_hz,peak_g'.split(',')
        source = list(reader)
    assert len(source) == 24
    rows = [{k: (v if k == 'axis' else float(v)) for k,v in r.items()} for r in source]
    assert all(math.isfinite(v) for r in rows for k,v in r.items() if k != 'axis')
    windows = sorted(set(r['window'] for r in rows))
    assert len(windows) == 8 and windows == list(range(int(windows[0]), int(windows[0])+8))
    for w in windows:
        assert sorted(r['axis'] for r in rows if r['window']==w) == list('xyz')
    for r in rows:
        assert r['min_g'] <= r['mean_g'] <= r['max_g']
        assert r['std_g'] >= 0 and r['peak_g'] >= 0
        assert abs(r['std_g']**2-r['variance_g2']) < 2e-6 # CSV rounding
        assert r['fs_hz'] == 100 and r['min_dt_us'] == r['max_dt_us'] == 10000
        assert 0 < r['peak_hz'] <= r['fs_hz']/2
        assert abs(r['peak_hz']-round(r['peak_hz']*256/r['fs_hz'])*r['fs_hz']/256) <= 0.000501
    all_rows[condition] = rows
    provenance.append({'condition': condition, 'file': '../results/'+filename,
                       'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                       'windows': [int(w) for w in windows], 'rows': len(rows)})
    for axis in 'xyz':
        rr = [r for r in rows if r['axis']==axis]
        summaries.append(dict(condition=condition, axis=axis, windows=len(rr),
            mean_of_mean_g=st.mean(r['mean_g'] for r in rr),
            mean_std_g=st.mean(r['std_g'] for r in rr),
            min_std_g=min(r['std_g'] for r in rr), max_std_g=max(r['std_g'] for r in rr),
            mean_peak_g=st.mean(r['peak_g'] for r in rr),
            min_peak_hz=min(r['peak_hz'] for r in rr), max_peak_hz=max(r['peak_hz'] for r in rr),
            overall_min_g=min(r['min_g'] for r in rr), overall_max_g=max(r['max_g'] for r in rr)))
with (HERE/'summary.csv').open('w', newline='') as f:
    w=csv.DictWriter(f, fieldnames=list(summaries[0])); w.writeheader(); w.writerows(summaries)
(HERE/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
y = {s['condition']:s for s in summaries if s['axis']=='y'}
lines=[]
for condition in FILES:
    axis_stats={s['axis']:s for s in summaries if s['condition']==condition}
    freq = '--' if condition=='Stationary' else f"{y[condition]['min_peak_hz']:.3f}"
    lines.append(condition+' & '+' & '.join(f"{axis_stats[a]['mean_std_g']:.6f}" for a in 'xyz')+' & '+f"{y[condition]['mean_peak_g']:.6f}"+' & '+freq+' '+chr(92)*2)
(HERE/'table_rows.tex').write_text(chr(92)+'newcommand{'+chr(92)+'ResultsRows}{%\n'+'\n'.join(lines)+'%\n}\n')
macros = {
    'SlowStdRatio': f"{y['Slow']['mean_std_g']/y['Stationary']['mean_std_g']:.1f}",
    'FastStdRatio': f"{y['Faster']['mean_std_g']/y['Slow']['mean_std_g']:.2f}",
    'SlowPeakRatio': f"{y['Slow']['mean_peak_g']/y['Stationary']['mean_peak_g']:.0f}",
    'FastPeakRatio': f"{y['Faster']['mean_peak_g']/y['Slow']['mean_peak_g']:.2f}",
    'StationaryZMean': f"{next(s['mean_of_mean_g'] for s in summaries if s['condition']=='Stationary' and s['axis']=='z'):.4f}",
    'FastYMin': f"{y['Faster']['overall_min_g']:.3f}",
    'FastYMax': f"{y['Faster']['overall_max_g']:.3f}",
}
(HERE/'metrics.tex').write_text('\n'.join(chr(92)+'newcommand{'+chr(92)+k+'}{'+v+'}' for k,v in macros.items())+'\n')
plt.rcParams.update({'font.family':'serif','font.size':9,'axes.spines.top':False,'axes.spines.right':False,'pdf.fonttype':42})
fig, axes=plt.subplots(1,2,figsize=(7.0,2.35),layout='constrained')
colors={'Stationary':'#515151','Slow':'#24618b','Faster':'#a95814'}
markers={'Stationary':'o','Slow':'s','Faster':'^'}
for i,(condition,rows) in enumerate(all_rows.items()):
    rr=[r for r in rows if r['axis']=='y']
    offsets=[(j-3.5)*0.025 for j in range(8)]
    axes[0].scatter([i+v for v in offsets],[r['std_g'] for r in rr],color=colors[condition],marker=markers[condition],s=20)
    axes[0].plot([i-0.2,i+0.2],[y[condition]['mean_std_g']]*2,color='black',lw=1.3)
    if condition!='Stationary':
        axes[1].plot(range(1,9),[r['peak_hz'] for r in rr],color=colors[condition],marker=markers[condition],ms=4,lw=1,label=condition)
axes[0].set_yscale('log'); axes[0].set_xticks(range(3),list(FILES))
axes[0].set_ylabel('Y-axis standard deviation (g)')
axes[0].set_title('(a) Window variation (log scale)',fontsize=9)
axes[0].grid(axis='y',alpha=0.2)
axes[1].set_ylim(0,2.05); axes[1].set_xticks(range(1,9)); axes[1].set_yticks([0,0.781,1.562,1.953])
axes[1].set_xlabel('Window order within each recording')
axes[1].set_ylabel('Y-axis peak frequency (Hz)')
axes[1].set_title('(b) Movement frequency',fontsize=9)
axes[1].legend(frameon=False,loc='lower right'); axes[1].grid(axis='y',alpha=0.2)
fig.savefig(HERE/'results_comparison.pdf')
fig.savefig(HERE/'results_comparison.png',dpi=220)
print(json.dumps({'y_axis':y,'macros':macros},indent=2))
print('Validated 72 rows: 24 windows across three conditions; regenerated report assets.')
