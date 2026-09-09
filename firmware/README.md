# Firmware

## Overview

`CarCabinTemperaturePredictionSystem.cpp` contains the ESP32-side firmware for the project.

The firmware handles:

- DS18B20 temperature acquisition,
- temperature sample storage,
- Newton-model prediction calculations,
- adaptive prediction correction,
- structured serial output for the Python logger,
- ESP32 Wi-Fi access-point setup,
- the local mobile web interface.

## Validation Status

The latest algorithm version with a complete repeated **vehicle-test dataset is V5.2**.

The current source file is a **development branch**. It also contains an experimental draft of the later consecutive local-`k` stability idea along with the redesigned web interface. That local-`k` draft has **not** been validated with a repeated vehicle-test series, so the quantitative results reported in this repository are based on V5.2 and earlier completed testing.

This distinction is intentional:

```text
Reported performance -> V5.2 vehicle testing
Current source branch -> ongoing development / UI polish
Experimental local-k logic -> future validation required
```

---

## Hardware Interface

The project uses a DS18B20 digital temperature sensor connected to the ESP32 through the OneWire protocol.

The current source uses:

```cpp
#define ONE_WIRE_BUS 13
```

The ESP32 reads the sensor, stores temperature samples, performs prediction calculations, and serves the local interface.

---

## Prediction Windows

The project evaluates three initial prediction times in the final tested V5.2 configuration:

```text
30 seconds
45 seconds
60 seconds
```

Earlier versions also evaluated a 15-second model. Vehicle testing showed that the first approximately 15–20 seconds frequently contained unstable startup behavior, which motivated removing that prediction window.

---

## Prediction Model

The core model is based on Newton's Law of Cooling/Heating:

```text
T(t) - Ta = (T0 - Ta)e^(-kt)
```

where:

- `T(t)` is measured temperature,
- `Ta` is the ambient/reference temperature,
- `T0` is the starting temperature,
- `k` is the estimated thermal-response constant.

The exponential relationship is transformed into a regression problem so that `k` can be estimated from measured temperature data.

---

## V5.2 Tested Development Direction

V5.2 improved the initial-prediction stage by excluding the first 20 seconds from the initial `k` estimation.

The change was motivated by repeated vehicle testing that showed early measurements could be affected by:

- sensor response delay,
- HVAC startup behavior,
- airflow stabilization,
- delayed heat transfer,
- transient cabin conditions.

V5.2 retained 30-, 45-, and 60-second prediction windows and preserved the adaptive correction system developed during V2-V4.x.

---

## Adaptive Correction

After an initial prediction is generated, the system continues recalculating the Newton model as additional temperature data becomes available.

The adaptive stage uses stabilization mechanisms developed during earlier testing:

- approximately ±10% adaptive `k` limiting,
- 70/30 adaptive `k` smoothing,
- a 25% maximum prediction change per correction.

These mechanisms reduce sudden changes while preserving the model's ability to adapt.

---

## Serial Communication

The firmware sends structured messages that are parsed by `data_logger/logger.py`.

Typical message categories include:

```text
NEW PREDICTION TEST START
SAMPLE,...
INITIAL_MODEL,...
CORRECTION_UPDATE,...
ACTUAL_TIME_SECONDS:...
```

See [Data Logger Documentation](../data_logger/README.md) for the logging format and stored output fields.

---

## Web Interface

The ESP32 creates a local Wi-Fi access point and serves a mobile-friendly browser interface.

The current interface displays:

- current temperature in Fahrenheit,
- model target/reference temperature,
- estimated time remaining,
- ideal / not-ideal status.

The browser uses the following routes:

```text
/temperaturef
/target
/status
/estimate
```

Before the 60-second model is available, the interface displays `Calculating...`.

---

## Experimental Local-`k` Draft

The development source also contains a draft of the proposed consecutive local-`k` stability approach.

The concept:

1. excludes the first 20 seconds,
2. estimates local `k` over consecutive 5-second intervals,
3. compares a new local value with the previously accepted value,
4. limits unusually large changes,
5. uses the accepted value for later prediction calculations.

This logic is included for continued development only. It does **not** have a repeated vehicle-test dataset and is not used to justify the reported V5.2 performance numbers.

---

## Current Portfolio Status

The embedded source, logger, documentation, representative data, and testing analysis are organized for portfolio review.

The main remaining visual update is replacing the UI concept image in the repository with a screenshot captured from the live ESP32 interface after final testing.
