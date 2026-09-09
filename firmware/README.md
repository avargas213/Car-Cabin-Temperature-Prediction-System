# Firmware

## Overview

The ESP32 firmware controls the embedded side of the Car Cabin Temperature Prediction System.

It is responsible for:

- reading cabin temperature from a DS18B20 temperature sensor,
- recording temperature samples during a prediction test,
- estimating the Newton cooling/heating constant `k`,
- generating initial predictions at 30, 45, and 60 seconds,
- applying adaptive prediction corrections,
- sending structured serial data to the Python data logger,
- hosting a local Wi-Fi access point,
- serving a web interface showing temperature, status, and estimated time remaining.

The latest completed and vehicle-tested algorithm version is **V5.2**. A V6.0 local-`k` limiter was designed as future work but was not implemented or experimentally evaluated.

---

## Hardware Interface

The system uses a DS18B20 digital temperature sensor connected to the ESP32 through the OneWire protocol.

The firmware periodically reads the sensor while also storing higher-frequency samples for prediction and experiment logging.

---

## Prediction Windows

V5.2 uses three initial prediction windows:

```text
30 seconds
45 seconds
60 seconds
```

Earlier versions also tested a 15-second model. That model was removed after vehicle testing showed that the first approximately 15–20 seconds frequently contained unstable startup behavior.

---

## Prediction Model

The system uses Newton's Law of Cooling/Heating:

```text
T(t) - Ta = (T0 - Ta)e^(-kt)
```

where:

- `T(t)` is the measured temperature at time `t`,
- `Ta` is the ambient/reference temperature,
- `T0` is the starting temperature,
- `k` is the estimated thermal response constant.

The exponential relationship is transformed into a regression problem so that `k` can be estimated from measured temperature data. The calculated `k` is then used to estimate the remaining time required to approach the target/reference temperature.

---

## V5.2 Startup Filter

Vehicle testing during V4.2 and V5.1 showed that the first approximately 15–20 seconds of the temperature response were often less representative of the long-term cabin behavior.

Potential causes included:

- sensor response delay,
- HVAC startup behavior,
- airflow stabilization,
- delayed heat transfer,
- transient cabin conditions.

V5.2 therefore excludes the first 20 seconds from the initial `k` calculation.

---

## V5.2 Initial `k` Estimation

V5.2 builds on the weighted initial-`k` approach developed during V5.0/V5.1. Post-startup portions of the temperature curve are used to estimate the initial thermal response, with later data receiving greater influence than the earliest usable measurements.

The purpose is to reduce sensitivity to startup transients while still producing an initial prediction quickly enough to be useful.

---

## Initial Predictions

Initial predictions are generated at approximately 30, 45, and 60 seconds.

Each model estimates the remaining time required to approach the target/reference temperature and converts that value into a predicted total experiment completion time.

---

## Adaptive Correction

The initial prediction is not treated as final. As additional temperature data becomes available, the firmware recalculates the Newton model and updates the prediction.

The adaptive stage includes stabilization methods developed during the V2-V4.x iterations, including:

- adaptive `k` smoothing,
- adaptive `k` change limiting,
- prediction-change limiting.

These mechanisms reduce the effect of individual noisy calculations while preserving the model's ability to adapt.

---

## Serial Communication

The ESP32 sends structured serial messages to the Python logger. The logger records raw samples, initial predictions, adaptive updates, and the final observed completion time.

Typical message categories include:

```text
NEW PREDICTION TEST START
SAMPLE,...
INITIAL_MODEL,...
CORRECTION_UPDATE,...
ACTUAL_TIME_SECONDS:...
```

See [`../data_logger/README.md`](../data_logger/README.md) for the logging workflow and output format.

---

## Web Interface

The ESP32 creates a local Wi-Fi access point and serves a browser-based interface for displaying:

- current cabin temperature,
- estimated time remaining,
- estimated target time,
- temperature status.

The current UI is still being redesigned, so the screenshot in the repository should be treated as a prototype/placeholder rather than the final interface.

---

## Planned V6.0 Development

A possible V6.0 improvement was designed after V5.2. The proposed approach would:

1. continue excluding the first 20 seconds,
2. calculate local `k` values over consecutive 5-second intervals,
3. compare each local `k` with the previous accepted value,
4. limit changes between accepted values to approximately ±30%,
5. use the accepted value for the 30-, 45-, and 60-second initial predictions.

This design was **not implemented or experimentally tested**. It remains a documented future-development direction only.

---

## Current Status

The firmware repository is being cleaned up around the tested V5.2 baseline. The immediate remaining work is to finalize the code version used for the portfolio, redesign the web UI, and replace the placeholder UI screenshot.

Additional V6.0 development and testing can be performed later if the project is continued.
