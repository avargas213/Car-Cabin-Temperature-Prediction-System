# System Design

## Overview

The Car Cabin Temperature Prediction System is an ESP32-based embedded system that measures cabin temperature, estimates how long it will take to approach a target/reference temperature, updates that estimate as new data becomes available, logs experiments to a computer, and serves a local web interface.

The system combines:

- ESP32 firmware
- DS18B20 digital temperature sensing
- Newton's Law of Cooling/Heating
- Regression-based estimation of the thermal constant `k`
- Adaptive correction
- Serial communication
- Python data logging
- Local Wi-Fi web interface

---

## High-Level Architecture

```text
DS18B20 Temperature Sensor
          |
          v
       ESP32
          |
          +-------------------------------+
          |                               |
          v                               v
Temperature Sampling              Local Wi-Fi Web Server
          |                               |
          v                               v
Initial k Estimation             Browser User Interface
          |
          v
30 / 45 / 60 s Initial Predictions
          |
          v
Adaptive Newton Correction
          |
          v
Corrected Prediction
          |
          +-------------------------------+
          |
          v
Structured Serial Messages
          |
          v
Python Data Logger
          |
          v
CSV / Experimental Analysis
```

---

## Hardware

### ESP32

The ESP32 performs the primary embedded processing:

- reads the temperature sensor,
- stores experiment samples,
- calculates initial `k`,
- generates predictions,
- performs adaptive corrections,
- hosts the local web server,
- outputs structured serial data.

### DS18B20 Temperature Sensor

The temperature sensor communicates through the OneWire protocol.

The current firmware uses:

```cpp
#define ONE_WIRE_BUS 13
```

Temperature is read approximately once per second, while experimental samples are stored approximately every 0.5 seconds.

---

## Timing Parameters

| Process | Current Interval |
|---|---:|
| Temperature sensor update | 1 second |
| Raw experimental sample | 0.5 second |
| Initial local `k` interval | 5 seconds |
| Adaptive prediction correction | 10 seconds |
| Initial prediction windows | 30, 45, 60 seconds |

These timers operate independently so that temperature display, regression sampling, and adaptive correction can occur at different rates.

---

## Physical Prediction Model

The prediction system is based on Newton's Law of Cooling/Heating.

A difference-form representation is:

```text
T(t) - Ta = (T0 - Ta)e^(-kt)
```

where:

- `T(t)` = measured temperature at time `t`
- `Ta` = ambient/reference temperature
- `T0` = initial temperature
- `k` = experimentally estimated thermal constant

The exponential relationship can be transformed into a linear regression problem:

```text
ln(|T - Ta|) = b - kt
```

The slope of the regression line is approximately:

```text
slope = -k
```

Therefore:

```text
k = -slope
```

Once `k` is known, the system solves the exponential equation for the remaining time.

The implementation uses a practical target difference of approximately 1°F from the reference temperature.

---

## Current V5.2 Initial Prediction Pipeline

The latest completed and vehicle-tested initial-prediction architecture is V5.2:

```text
Start test
    |
    v
Collect temperature samples
    |
    v
Exclude first 20 seconds from initial-k estimation
    |
    v
Estimate initial k using post-startup data
    |
    +-----------------------------+
    |             |               |
   30 s          45 s            60 s
    |             |               |
    v             v               v
 Initial        Initial         Initial
Prediction     Prediction      Prediction
```

---

## Startup Data Exclusion

Vehicle testing during the V5.x development showed that roughly the first 15–20 seconds frequently contained unstable thermal behavior.

Possible sources included:

- sensor response delay,
- HVAC stabilization,
- airflow establishment,
- delayed heat transfer to the occupant area,
- transient cabin conditions.

V5.2 therefore excludes the first 20 seconds from the initial `k` calculation and removes the earlier 15-second prediction model.

---

## Initial `k` Estimation in V5.2

V5.2 retains the weighted initial-`k` development from V5.0/V5.1, but only uses post-startup temperature data. Later portions of the available sampling window receive greater influence because testing showed that the earliest measurements were less representative of the long-term cabin thermal response.

The objective is not to create a theoretically perfect estimate, but to produce a more stable first prediction from limited early data.

---

## Initial Predictions

The system creates three prediction models:

| Model | Time of Initial Prediction |
|---|---:|
| 30-second model | ~30 s |
| 45-second model | ~45 s |
| 60-second model | ~60 s |

The total predicted completion time is calculated from the elapsed experiment time plus the predicted remaining time.

---

## Planned V6.0 Direction

A future V6.0 concept was documented after V5.2. The proposed design would calculate local `k` values over consecutive 5-second intervals after the 20-second startup exclusion and limit changes between consecutive accepted values to approximately ±30%.

The goal would be to determine whether local parameter limiting can make the initial prediction more stable without preventing legitimate changes in cabin thermal behavior.

This local-`k` concept was not part of the completed V5.2 validation campaign. Any draft implementation in the development firmware should be treated as experimental future work rather than as a validated system improvement.

---

## Adaptive Newton Correction

The initial prediction is not treated as final.

After an initial model is created, the firmware periodically recalculates the Newton model using the accumulated temperature data.

Adaptive updates occur approximately every 10 seconds.

The adaptive stage uses its own stability mechanisms.

### Adaptive `k` Limiter

A newly calculated adaptive `k` is limited to approximately ±10% of the previous adaptive value.

### Adaptive Smoothing

After limiting, the system applies:

```text
k_adaptive,new =
0.7(k_adaptive,previous)
+
0.3(k_calculated)
```

This allows gradual adaptation while reducing sensitivity to individual measurements.

---

## Prediction-Change Limiter

The system contains an additional limiter on the predicted completion time itself.

If an adaptive update would change the previous prediction by more than 25%, the new prediction is restricted to the 25% boundary.

This creates multiple stability layers:

```text
Startup-data filter
        ↓
V5.2 startup-data filtering / initial-k estimation
        ↓
Initial prediction
        ↓
Adaptive ±10% k limiter
        ↓
70/30 k smoothing
        ↓
25% prediction-change limiter
        ↓
Final corrected prediction
```

Each layer addresses a different source of instability.

---

## Serial Communication

The ESP32 sends structured serial messages to the Python logger.

### Test Start

```text
NEW PREDICTION TEST START
```

### Temperature Sample

```text
SAMPLE,experiment_time,temperature
```

### Initial Prediction

```text
INITIAL_MODEL,experiment_time,temperature,model_time,prediction
```

### Adaptive Correction

```text
CORRECTION_UPDATE,experiment_time,temperature,model_time,corrected_prediction
```

### Test Completion

```text
ACTUAL_TIME_SECONDS:value
```

This protocol keeps the firmware responsible for real-time modeling while the computer handles long-term experimental storage and analysis.

---

## Python Data Logger

The Python logger:

1. connects to the ESP32 at 115200 baud,
2. waits for the start message,
3. records raw samples,
4. stores initial model predictions,
5. records adaptive updates,
6. receives the actual completion time,
7. calculates initial and final prediction errors,
8. saves CSV results,
9. closes the connection after the completed test.

The logger separates raw initial-model data from correction data so that experiments can be analyzed later without mixing measurements and model outputs.

---

## Web Interface

The ESP32 creates a local Wi-Fi access point and hosts an HTTP server.

The browser interface displays:

- current temperature,
- estimated time remaining,
- target/reference temperature,
- estimated time remaining once the 60-second model is available,
- ideal / not-ideal temperature status.

The main endpoints are:

```text
/temperaturef
/target
/status
/estimate
```

The interface polls the ESP32 periodically while maintaining a local countdown between prediction updates.

---

## Vehicle Sensor Placement

Controlled testing originally placed the sensor near a repeatable heat source so algorithm changes could be compared under similar conditions.

Later vehicle testing moved the sensor near the driver's seating / upper-body area.

This location was selected because the project is intended to predict the temperature experienced by an occupant rather than the temperature directly at an HVAC vent.

This introduces realistic effects such as:

- cabin thermal mass,
- uneven airflow,
- delayed heat distribution,
- changing outdoor conditions,
- non-uniform cabin temperature.

---

## Design Philosophy

The system intentionally avoids adding model complexity unless testing shows a clear benefit.

A dynamic ambient-temperature-estimation approach was investigated but rejected because it added sensitivity and reduced consistency.

The final development direction therefore favors:

```text
robustness
+ repeatability
+ understandable behavior
+ adaptive correction
```

over adding parameters solely for theoretical sophistication.

---

## Current Limitations

The current model assumes that the observed thermal behavior is sufficiently close to an exponential Newton-type response.

Potential sources of prediction error include:

- changing outdoor temperature,
- sunlight,
- changing HVAC behavior,
- airflow differences,
- sensor placement,
- vehicle geometry,
- non-uniform cabin temperature,
- early transient behavior,
- limited test sample size.

V5.2 is the latest completed and vehicle-tested system. The proposed local-`k` limiter remains experimental future work and has not been validated with a repeated vehicle-test dataset.
