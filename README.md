# Car Cabin Temperature Prediction System

An ESP32-based embedded system that measures vehicle cabin temperature and predicts how long the cabin will take to approach a target temperature using Newton's Law of Cooling/Heating and adaptive model correction.

![System Overview](images/System_Overview.jpeg)

## Overview

The system combines an **ESP32**, **DS18B20 temperature sensor**, embedded **C++**, a **Python serial data logger**, a local **Wi-Fi web interface**, and repeated experimental testing.

The latest fully vehicle-tested algorithm version is **V5.2**. Development progressed through multiple versions as testing exposed instability in early predictions, adaptive updates, and startup temperature data.

### What the system does

1. Measures cabin temperature with a DS18B20 sensor.
2. Collects temperature samples on the ESP32.
3. Estimates a thermal-response constant `k` from the measured data.
4. Generates initial predictions at 30, 45, and 60 seconds.
5. Continues recalculating the model as more data becomes available.
6. Stabilizes adaptive changes with `k` limiting, smoothing, and prediction limiting.
7. Sends structured serial data to a Python logger for analysis.
8. Serves a local browser interface over the ESP32's Wi-Fi access point.

---

## Key Results

V5.2 was evaluated using five vehicle experiments. Each experiment produced 30-, 45-, and 60-second model results from the same underlying temperature curve.

| Prediction Window | Mean Initial Error | Mean Final Error | Final Error SD |
|---|---:|---:|---:|
| 30 s | 64.76% | 9.53% | 4.12% |
| 45 s | 49.86% | 7.34% | 4.32% |
| 60 s | 43.52% | **6.28%** | 4.98% |

Across the V5.2 dataset, adaptive correction reduced mean absolute prediction error by approximately **85%** for all three prediction windows.

Earlier controlled testing also showed the value of adaptive correction: one 10-test baseline decreased from **18.34% average initial error to 4.73% average final error**.

The broader development dataset includes:

- 10 controlled adaptive-model baseline tests,
- 5 V4.2 vehicle experiments,
- 5 V5.1 vehicle experiments,
- 5 V5.2 vehicle experiments,
- 55 vehicle model-window result sets across V4.2, V5.1, and V5.2.

Because different version groups were tested under different vehicle/environmental conditions, cross-version numbers are treated as development evidence rather than a perfectly controlled accuracy leaderboard.

![V5.2 Prediction Error by Window](images/v5_2_error_by_window.png)

---

## System Architecture

```text
DS18B20 Temperature Sensor
          |
          v
        ESP32
          |
          v
Temperature Sampling
          |
          v
Initial k Estimation
          |
          v
30 / 45 / 60 Second Predictions
          |
          v
Adaptive Newton Correction
          |
          +----------------------+
          |                      |
          v                      v
 Local Web Interface        Serial Output
                                 |
                                 v
                          Python Data Logger
                                 |
                                 v
                         Experimental Data
```

![System Process Overview](images/System_Process_Overview.jpeg)

---

## Prediction Model

The prediction model is based on Newton's Law of Cooling/Heating:

```text
T(t) - Ta = (T0 - Ta)e^(-kt)
```

where:

- `T(t)` is the measured temperature,
- `Ta` is the ambient/reference temperature,
- `T0` is the starting temperature,
- `k` is the estimated thermal-response constant.

The exponential relationship is transformed into a regression problem so that `k` can be estimated from measured temperature samples.

### V5.2 initial prediction strategy

Vehicle testing showed that the first approximately 15–20 seconds of a test frequently contained startup transients caused by factors such as sensor response, HVAC stabilization, airflow establishment, and delayed cabin heat transfer.

V5.2 therefore:

- excludes the first 20 seconds from the initial `k` calculation,
- removes the earlier 15-second prediction model,
- retains 30-, 45-, and 60-second initial predictions,
- preserves the adaptive correction system developed during earlier versions.

### Adaptive correction

After the initial prediction, the system continues updating as new temperature data becomes available.

The adaptive stage includes:

- approximately ±10% adaptive `k` limiting,
- 70/30 `k` smoothing,
- a 25% prediction-change limit.

These mechanisms reduce sudden prediction jumps without completely preventing the model from responding to new data.

---

## Vehicle Testing

Later development moved from a controlled heat-source setup into a vehicle environment. The sensor was positioned near the driver's seating / upper-body area rather than directly at the HVAC outlet so that measurements better represented the temperature experienced by an occupant.

### Final corrected error across the V5.2 tests

![V5.2 Final Error by Test](images/v5_2_final_error_by_test.png)

### Representative adaptive convergence

The example below shows one V5.2 60-second model test. The initial estimate was substantially above the observed completion time, while repeated adaptive corrections moved the prediction closer to the actual result.

![Adaptive Convergence Example](images/v5_2_adaptive_convergence_test1.png)

More complete methodology, per-test results, earlier-version context, and limitations are documented in [Testing Results](docs/testing-results.md).

---

## Hardware

- ESP32 WROOM development board
- DS18B20 digital temperature sensor
- OneWire communication
- Breadboard and jumper wiring
- USB serial connection for test logging

![Circuit Overview](images/Circuit_Overview.jpeg)

---

## Web Interface

The ESP32 creates a local Wi-Fi access point and hosts a mobile-friendly browser interface showing:

- current cabin temperature in Fahrenheit,
- the model target/reference temperature,
- estimated time remaining once the 60-second model is available,
- ideal / not-ideal temperature status.

![Web Interface Preview](images/Website_UI_example.jpeg)

*UI concept preview. This image is intended to be replaced with a real screenshot from the live ESP32 interface after final UI testing.*

---

## Data Logging

A Python serial logger records:

- elapsed experiment time,
- raw temperature samples,
- initial predictions,
- adaptive correction updates,
- actual completion time,
- initial prediction error,
- final prediction error.

Representative V5.2 files are included in [`sample_data/`](sample_data/). The complete local development dataset is intentionally excluded from the public repository.

---

## Development History

| Version | Main Development |
|---|---|
| V1 | Basic adaptive Newton correction |
| V2 | Adaptive `k` smoothing |
| V3.1 | ±20% adaptive `k` limiter |
| V3.2 | ±10% adaptive `k` limiter |
| V4.1 | Prediction-change limiter |
| V4.2 | Adaptive stability evaluation |
| V5.0 / V5.1 | Weighted initial `k` investigation |
| V5.2 | Removed unstable first 20 seconds and 15-second model |
| Future work | Consecutive local-`k` stability experiment and additional vehicle testing |

The full version-by-version reasoning is documented in [Development History](docs/development-history.md).

---

## Repository Structure

```text
.
├── firmware/
│   ├── CarCabinTemperaturePredictionSystem.cpp
│   └── README.md
│
├── data_logger/
│   ├── logger.py
│   └── README.md
│
├── sample_data/
│   ├── v5.2_adaptive_correction_45s.xlsx
│   ├── v5.2_initial_model_45s.xlsx
│   ├── v5.2_vehicle_test_conditions.xlsx
│   └── README.md
│
├── docs/
│   ├── development-history.md
│   ├── system-design.md
│   └── testing-results.md
│
├── images/
├── .gitignore
└── README.md
```

---

## Documentation

- [System Design](docs/system-design.md)
- [Development History](docs/development-history.md)
- [Testing Results](docs/testing-results.md)
- [Firmware Documentation](firmware/README.md)
- [Python Data Logger](data_logger/README.md)
- [Representative Sample Data](sample_data/README.md)

---

## Technologies

- C++
- Python
- ESP32
- DS18B20
- OneWire
- Serial communication
- HTML / CSS / JavaScript
- Embedded web server
- Newton's Law of Cooling/Heating
- Linear regression
- Experimental data analysis

---

## Project Status

**V5.2 is the latest fully vehicle-tested algorithm version.**

The remaining portfolio polish is primarily visual:

- replace the UI concept preview with a real screenshot from the live ESP32 interface,
- add a clear photo of the prototype installed in the vehicle.

A consecutive local-`k` limiter was explored as a possible future development direction, but no quantitative performance claims are made for that work.
