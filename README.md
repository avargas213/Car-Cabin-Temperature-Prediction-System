# Car Cabin Temperature Prediction System

An ESP32-based embedded temperature prediction system that uses a DS18B20 sensor, Newton's Law of Cooling/Heating, adaptive correction, and real-time data logging to estimate how long a vehicle cabin will take to approach a target temperature.

![System Overview](images/System_Overview.jpeg)

## Overview

This project was developed to explore whether a low-cost embedded system could predict vehicle cabin temperature changes using real-time sensor data.

The ESP32 collects temperature measurements from a DS18B20 sensor, estimates the thermal response of the cabin using a Newton-based exponential model, generates an initial completion-time prediction, and continuously corrects that prediction as more data becomes available.

The project progressed through multiple algorithm versions focused on:

- improving prediction accuracy
- reducing unstable prediction changes
- filtering unreliable startup data
- improving robustness in real vehicle conditions
- balancing prediction speed with consistency

The current implementation is **V6.0**.

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
   Local Web Interface      Serial Output
                                 |
                                 v
                          Python Data Logger
                                 |
                                 v
                         Experimental Data
```

![System Process Overview](images/System_Process_Overview.jpeg)

## Hardware

- ESP32 WROOM development board
- DS18B20 digital temperature sensor
- OneWire communication
- Breadboard and jumper wiring
- USB serial connection for experimental logging

![Circuit Overview](images/Circuit_Overview.jpeg)

## Prediction Model

The system is based on Newton's Law of Cooling/Heating:

```text
T(t) - Ta = (T0 - Ta)e^(-kt)
```

where:

- `T(t)` is the measured temperature
- `Ta` is the ambient/reference temperature
- `T0` is the initial temperature
- `k` represents the thermal response of the system

The exponential relationship is transformed into a linear regression problem so that `k` can be estimated from measured temperature data.

The predicted completion time is then calculated using the estimated value of `k`.

## Current V6.0 Algorithm

The current implementation improves the initial estimate by filtering and limiting unstable changes in `k`.

### Initial prediction

1. Collect temperature samples.
2. Exclude the first 20 seconds from the initial `k` calculation.
3. Calculate a local `k` for consecutive 5-second intervals.
4. Use the first valid interval as the initial reference.
5. Limit each subsequent accepted `k` to within ±30% of the previous accepted value.
6. Generate initial predictions at:
   - 30 seconds
   - 45 seconds
   - 60 seconds

### Adaptive correction

After the initial prediction, the system continues updating the model as new temperature data becomes available.

The adaptive model includes:

- ±10% adaptive `k` limitation
- 70/30 `k` smoothing
- 25% maximum prediction change per correction

These mechanisms allow the model to respond to new data while reducing unrealistic prediction jumps.

## Testing

Development included both controlled heating experiments and real vehicle testing.

Early controlled testing demonstrated that adaptive correction substantially improved prediction accuracy. In one 10-test evaluation, average prediction error decreased from **18.34% initially to 4.73% after adaptive correction**, a 74.2% reduction in average error.

Later testing moved into a vehicle environment with the sensor positioned near the driver's seating area rather than directly beside the HVAC output. This allowed the system to measure temperature changes closer to what an occupant would actually experience.

V5.2 vehicle testing compared 30-, 45-, and 60-second prediction windows using the same temperature curve for each test. This reduced experimental differences between model comparisons and showed that longer initial sampling generally produced more consistent initial predictions.

![V5.2 Prediction Error](images/v5_2_error_by_window.png)

![Adaptive Convergence Example](images/v5_2_adaptive_convergence_test1.png)

V6.0 is the current implemented architecture, but a complete repeated V6.0 vehicle-testing campaign was not completed. Therefore, no unsupported claim is made that V6.0 quantitatively outperforms V5.2.

## Web Interface

The ESP32 creates a local Wi-Fi access point and hosts a browser-based interface displaying:

- current cabin temperature
- estimated time remaining
- estimated target time
- current temperature status

![Web Interface](images/Website_UI_example.jpeg)

## Data Logging

A Python serial logger records experiment data from the ESP32.

The logger stores:

- elapsed experiment time
- temperature samples
- initial predictions
- adaptive correction updates
- actual completion time
- initial prediction error
- final prediction error

This allowed algorithm changes to be evaluated using recorded experimental data instead of relying only on visual observations.

## Development

The system went through several major iterations:

| Version | Main Improvement |
|---|---|
| V1 | Basic adaptive Newton correction |
| V2 | Adaptive `k` smoothing |
| V3.1 | ±20% adaptive `k` limiter |
| V3.2 | ±10% adaptive `k` limiter |
| V4.1 | Prediction-change limiter |
| V4.2 | Improved adaptive prediction stability |
| V5.0 / V5.1 | Weighted initial `k` investigation |
| V5.2 | Removed unstable first 20 seconds |
| V6.0 | Consecutive 5-second initial `k` calculations with ±30% limiting |

The development process increasingly prioritized **robustness and consistency over unnecessary model complexity**.

For the complete development process, see:

[Development History](docs/development-history.md)

## Repository Structure

```text
.
├── firmware/
│   └── CarCabinTemperaturePredictionSystem.cpp
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
│
├── .gitignore
└── README.md
```

## Documentation

More detailed documentation is available here:

- [System Design](docs/system-design.md)
- [Development History](docs/development-history.md)
- [Testing Results](docs/testing-results.md)
- [Firmware Documentation](firmware/README.md)
- [Data Logger Documentation](data_logger/README.md)
- [Sample Data](sample_data/README.md)

## Technologies

- C++
- Python
- ESP32
- DS18B20
- OneWire
- Serial communication
- HTML / JavaScript
- Embedded web server
- Newton's Law of Cooling/Heating
- Linear regression
- Experimental data analysis

## Current Status

The current V6.0 firmware is implemented and combines:

- startup-data filtering
- consecutive local thermal-constant estimation
- initial `k` limiting
- adaptive model correction
- prediction smoothing and limiting
- serial experimental logging
- local web-based output

Further work would focus on collecting a larger V6.0 vehicle dataset and evaluating the current initial `k` limiter across a wider range of environmental conditions.
