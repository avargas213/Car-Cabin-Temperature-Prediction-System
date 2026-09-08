Development History

Overview

The Car Cabin Temperature Prediction System was developed through repeated testing and algorithm refinement rather than as a single final model. The project began by comparing multiple mathematical prediction methods, then progressively focused on a Newton's Law of Cooling/Heating model with adaptive correction.

The development process emphasized three goals:

Improve prediction accuracy.

Prevent unstable prediction jumps.

Improve robustness under real vehicle conditions.

A recurring design principle throughout the later stages was:

Robustness and consistency over unnecessary theoretical complexity.

Development Timeline

Version / Stage

Main Change

Problem Being Addressed

Result / Decision

Early prototype

Compared linear regression, two-point estimation, Newton model, and quadratic regression

Determine which mathematical model could represent temperature change most effectively

Newton's Law model became the main prediction method because it matched the exponential thermal behavior and supported adaptive updates

V1

Basic adaptive Newton correction

A fixed initial prediction could not respond to new temperature information

Recalculating k during the experiment improved final accuracy, but intermediate predictions could jump significantly

V2

70/30 smoothing of adaptive k

Individual recalculations of k were too sensitive to noise

Prediction updates became more gradual, but large raw changes could still influence the model

V3.1

±20% adaptive k limiter

Smoothing alone did not prevent unrealistic k changes

Added a hard limit before smoothing

V3.2

±10% adaptive k limiter

The ±20% limit still allowed relatively large changes

Increased stability by tightening the allowable adaptive k change

V4.1

20% prediction-change limiter

Even a limited k could still occasionally produce a large prediction jump

Added a second stability layer directly on the predicted completion time

V4.2

More restrictive prediction stability testing

Investigate whether tighter prediction limiting improved smoothness

V4.x testing confirmed that adaptive correction could converge accurately, but initial prediction quality remained inconsistent

V5.0 ambient-estimation experiment

Attempted to estimate ambient temperature dynamically

Reduce reliance on a fixed ambient/target parameter

Rejected because the added parameter increased sensitivity and inconsistency

V5.0 / V5.1 weighted initial k

Weighted later portions of the initial temperature curve more heavily

Early sensor and airflow transients distorted initial k

Reduced some variability, but the first 15–20 seconds still affected the result

V5.2

Removed first 20 seconds from initial k calculation; removed 15 s model

Early startup data was repeatedly identified as unstable

Retained only 30 s, 45 s, and 60 s initial models; vehicle testing showed later windows were generally more accurate

V6.0

Consecutive non-overlapping 5-second local k calculations with ±30% change limit

k could still shift substantially after the startup filter

Current implementation. Each local k is compared with the previous accepted value before an initial prediction is generated

Current status

V6.0 implemented, not fully re-tested

Limited time available for a new vehicle test series

V6.0 is documented as the current architecture, but no quantitative performance improvement is claimed without supporting test data

Early Model Investigation

The earliest version of the system evaluated several prediction approaches:

Linear regression

Simple two-point estimation

Newton's Law of Cooling/Heating

Quadratic regression

The Newton model was selected as the primary model because temperature change approached the target exponentially rather than linearly.

The basic model is:

T(t) - Ta = (T0 - Ta)e^(-kt)

where:

T(t) = measured temperature at time t

Ta = ambient or reference temperature

T0 = starting temperature

k = thermal response constant

The key parameter is k. Much of the later development focused on making the estimated k more stable.

V1 — Basic Adaptive Newton Correction

Change

V1 recalculated the Newton model during the experiment as new temperature data became available.

Motivation

A single initial estimate could be inaccurate if the earliest temperature measurements did not represent the full thermal response.

Behavior

The adaptive system substantially improved final predictions, but intermediate updates could become unstable because every new k value was accepted directly.

A 10-test controlled dataset from this early adaptive stage produced:

Metric

Result

Average initial prediction error

18.34%

Average final prediction error

4.73%

Relative reduction in average error

74.2%

Maximum final prediction error

9.54%

Minimum final prediction error

0.05%

These results established an important direction for the project: adaptive correction worked, but prediction stability needed improvement.

V2 — Adaptive k Smoothing

V2 introduced a weighted update:

k_new = 0.7(k_previous) + 0.3(k_calculated)

Instead of completely replacing the previous k, the system retained 70% of the earlier value and incorporated 30% of the newly calculated value.

This reduced sensitivity to a single noisy update, but smoothing alone could not prevent a very large raw k from influencing the model.

V3.1 — ±20% Adaptive k Limiter

V3.1 added a hard restriction before smoothing.

The process became:

New temperature data
        ↓
Calculate raw k
        ↓
Compare with previous adaptive k
        ↓
Limit to ±20%
        ↓
Apply 70/30 smoothing
        ↓
Generate updated prediction

The purpose was to address instability at its source rather than only smoothing the output afterward.

V3.2 — ±10% Adaptive k Limiter

V3.2 reduced the allowed adaptive k change from ±20% to ±10%.

This was a parameter-tuning step rather than a new model. The aim was to increase stability while preserving the ability to adapt.

V4.1 — Prediction Change Limiter

V4.1 introduced a second stability mechanism.

Even when k was constrained, the nonlinear prediction equation could still turn a moderate parameter change into a large completion-time change. Therefore, the predicted completion time itself was limited.

V4.1 initially tested a 20% maximum update.

Conceptually:

Adaptive k limiter
        ↓
New Newton prediction
        ↓
Compare with previous prediction
        ↓
Prediction-change limiter
        ↓
Displayed / stored corrected prediction

V4.2 — Adaptive Model Evaluation

V4.2 continued the stability investigation and became the adaptive foundation used by the later V5.x and V6.0 designs.

The most important conclusion from this phase was not that the adaptive model needed to be replaced. Instead, testing showed:

Final corrected predictions were generally much better than initial predictions.

The adaptive model could compensate for an inaccurate starting estimate.

The largest remaining problem was the quality of the initial prediction.

This shifted development effort away from rewriting the adaptive correction system and toward improving the initial k.

V5.0 — Dynamic Ambient Temperature Experiment

One V5.0 experiment attempted to estimate the ambient-temperature parameter from the early temperature data rather than relying on a fixed reference.

This approach was not retained.

Adding another estimated parameter made the prediction more sensitive to:

early sensor noise,

small temperature-curve changes,

airflow differences,

vehicle geometry,

environmental conditions.

The project therefore returned to a simpler architecture. This was an important engineering decision: a mathematically more flexible model was rejected because it was less robust.

V5.0 / V5.1 — Weighted Initial k

A separate V5.x development path addressed early-data instability by calculating multiple initial k values and weighting later portions of the sampling window more heavily.

The rationale was that the beginning of a vehicle heating/cooling event can contain:

sensor response delay,

HVAC stabilization,

changing airflow,

transient heat transfer,

uneven cabin temperature distribution.

Later measurements were expected to better represent the long-term thermal trend.

V5.1 Finding

Vehicle testing showed that weighting reduced some influence from the earliest samples, but it did not remove the problem. The first roughly 15–20 seconds continued to introduce substantial variability.

This led to a simpler conclusion:

If a region of data is consistently unreliable, reducing its weight may be less effective than excluding it entirely.

V5.2 — Startup Data Exclusion

V5.2 removed the first 20 seconds from the initial k calculation.

It also removed the 15-second initial prediction because a 15-second prediction could not be produced after excluding the first 20 seconds.

The remaining prediction windows became:

30 seconds
45 seconds
60 seconds

The adaptive correction model remained in place.

This version was tested in a vehicle and is the latest version in the project with a complete repeated-test dataset.

V6.0 — Consecutive 5-Second Initial k Limiter

V6.0 is the current implemented design.

Instead of calculating one initial k from all post-startup data, the firmware calculates local k values over consecutive, non-overlapping 5-second intervals:

0–20 s   excluded

20–25 s  → first accepted k
25–30 s  → new raw k → ±30% limit
30–35 s  → new raw k → ±30% limit
35–40 s  → new raw k → ±30% limit
...

Each new raw value is compared with the previous accepted k, not always with the first value.

The accepted range is:

0.70(k_previous) ≤ k_accepted ≤ 1.30(k_previous)

The current accepted k is used when the 30-, 45-, and 60-second initial prediction times are reached.

Why This Is Different From V3.x

V3.x and V6.0 limit different parameters at different stages:

Mechanism

Stage

Limit

Purpose

V6.0 initial k limiter

Before / during initial predictions

±30% per 5-second interval

Prevent unstable early local k changes

Adaptive k limiter

After an initial model exists

±10%

Stabilize adaptive recalculation

Adaptive k smoothing

After adaptive limit

70% old / 30% new

Reduce noise further

Prediction-change limiter

Final adaptive output

25%

Prevent a single large displayed prediction jump

Current Project Status

V6.0 is the current firmware architecture, but a full repeated V6.0 vehicle-test dataset was not completed.

Therefore:

V6.0 is presented as an implemented design improvement.

V5.2 is used for the latest quantitative vehicle-testing analysis.

No claim is made that V6.0 is more accurate than V5.2 without experimental evidence.

This distinction is intentional and reflects the project's experimental approach: algorithm changes are separated from validated performance claims.
