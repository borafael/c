#ifndef RLYEH_BENCH_H
#define RLYEH_BENCH_H

/* Headless CPU-raytrace benchmark on the R'lyeh scene — no SDL/GL, so it runs
 * anywhere. The harness calls this when RLYEH_BENCH=1 and returns its result
 * as the process exit code. RLYEH_BENCH_FRAMES and RT_CPU_THREADS tune it. */
int run_bench(void);

#endif /* RLYEH_BENCH_H */
