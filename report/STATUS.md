# Report revision pending: 500 Hz requirement

The existing report.pdf, report.tex, figures, summary and provenance describe
the measured 100 Hz / 256-sample experiments. Preserve these as historical
evidence. Do not submit them as 500 Hz results.

The current firmware polls at 500 Hz with 1024 samples per window (2.048 s;
0.48828125 Hz FFT-bin spacing). The sensor divider is now 0 (1125 Hz internal
output); DLPF6 and +/-2 g are retained. New hardware validation and three new
recordings are required. The capture script rejects the old 100 Hz firmware.

After capture, update analyze_results.py to explicitly select the new datasets
and use 500 Hz, 2000 us timing and 1024-sample FFT validation. Then revise the
methods, tables, analysis and PDF from the new evidence. Current analyze_results.py
settings intentionally remain tied to the historical recordings.
