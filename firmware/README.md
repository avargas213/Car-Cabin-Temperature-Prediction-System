# Firmware

## Overview

The firmware runs on the ESP32 and controls the embedded side of the Car Cabin Temperature Prediction System.

It is responsible for:

- Reading cabin temperature from a DS18B20 temperature sensor
- Recording temperature samples during a prediction test
- Estimating the Newton cooling/heating constant `k`
- Generating initial predictions at 30, 45, and 60 seconds
- Applying adaptive prediction corrections during the test
- Sending structured serial data to the Python data logger
- Hosting a local Wi-Fi access point
- Serving a web interface showing temperature, status, and estimated time remaining

The current implementation is Version 6.0.

---

## Hardware Interface

The system uses a DS18B20 temperature sensor connected through the OneWire protocol.

```cpp
#define ONE_WIRE_BUS 13

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
```

The sensor is read once per second during operation.

```cpp
const unsigned long TEMPERATURE_DELAY = 1000;
```

Raw prediction-test samples are collected every 500 ms.

```cpp
const unsigned long SAMPLE_DELAY = 500;
```

---

## Prediction Windows

Three initial prediction models are evaluated during each test:

```cpp
#define MODEL_COUNT 3

int modelTimes[MODEL_COUNT] =
{
  30,
  45,
  60
};
```

This allows the effect of different initial observation periods to be compared using the same underlying temperature experiment.

---

## Prediction Model

The system uses Newton's Law of Cooling/Heating to estimate the remaining time required for the cabin temperature to approach the target temperature.

The model is based on:

```text
T(t) - Ta = (T0 - Ta)e^(-kt)
```

where:

- `T(t)` is the temperature at time `t`
- `Ta` is the estimated ambient temperature
- `T0` is the starting temperature
- `k` is the thermal response constant

The prediction system estimates `k` from measured temperature data and uses it to calculate the remaining time.

The current implementation defines the target as being within approximately 1°F of the ambient estimate.

```cpp
const float TARGET_OFFSET = 1.0;
```

---

## Initial `k` Estimation

Version 6.0 does not immediately use the earliest temperature data to estimate `k`.

Instead, the initial estimation begins at approximately 20 seconds into the test.

From that point, the firmware evaluates consecutive 5-second intervals:

```text
20-25 s
25-30 s
30-35 s
35-40 s
...
```

Each interval is used to calculate a raw `k` value using a linear regression of:

```text
ln(T - Ta)
```

against time.

The first valid interval becomes the initial accepted `k`.

Later interval values are compared against the previously accepted value.

---

## Initial `k` Limiter

To reduce large changes caused by noisy measurements or short-term temperature fluctuations, each new initial `k` value is limited to ±30% of the previously accepted value.

```cpp
const float INITIAL_K_CAP = 0.30;
```

The allowed range is calculated as:

```cpp
float minK =
    previousK * (1.0 - INITIAL_K_CAP);

float maxK =
    previousK * (1.0 + INITIAL_K_CAP);
```

If the new raw value falls outside this range, it is limited before becoming the new accepted `k`.

This helps make the initial prediction process more stable.

---

## Initial Predictions

Initial predictions are generated when each prediction window is reached.

For example:

- 30-second model is created after approximately 30 seconds
- 45-second model is created after approximately 45 seconds
- 60-second model is created after approximately 60 seconds

The accepted `k` value available at that time is used to calculate the remaining time.

The firmware then converts that remaining time into a predicted total experiment completion time.

```cpp
float prediction =
    elapsed + remaining;
```

The prediction is stored as both the initial and current corrected prediction.

---

## Adaptive Correction

After the initial prediction is created, the firmware continues collecting temperature data.

Every 10 seconds, the prediction can be recalculated using the newer temperature data.

```cpp
const unsigned long CORRECTION_INTERVAL = 10000;
```

The adaptive model recalculates `k` from the available experiment data.

To prevent unstable changes, the adaptive `k` value is limited to approximately ±10% of the previous adaptive value before smoothing is applied.

```cpp
float maxKChange = 0.10;
```

The accepted value is then smoothed using:

```cpp
adaptiveK[modelIndex] =
    0.7 * adaptiveK[modelIndex]
    +
    0.3 * k;
```

This gives greater weight to the previous accepted value while still allowing the model to respond to new temperature behavior.

---

## Prediction Change Limiter

The firmware also limits how much the predicted completion time can change during a single adaptive update.

If the newly calculated prediction differs from the previous prediction by more than 25%, the change is limited.

```cpp
if(percentChange > 0.25)
{
    if(newPrediction > oldPrediction)
    {
        newPrediction =
            oldPrediction * 1.25;
    }
    else
    {
        newPrediction =
            oldPrediction * 0.75;
    }
}
```

This prevents a single noisy update from causing a large jump in the displayed prediction.

---

## Serial Communication

The firmware sends structured messages over the serial connection so the Python data logger can record each test.

### Raw Temperature Sample

```text
SAMPLE,experiment_time,temperature
```

### Initial Prediction

```text
INITIAL_MODEL,experiment_time,temperature,model_time,prediction
```

### Adaptive Update

```text
CORRECTION_UPDATE,experiment_time,temperature,model_time,corrected_prediction
```

### Actual Completion Time

```text
ACTUAL_TIME_SECONDS:value
```

These messages are parsed by the Python logger in the `data_logger` directory.

---

## Test Completion

The firmware continuously checks whether the cabin temperature has reached the target condition.

When the temperature reaches the ambient estimate, the firmware records the actual experiment completion time.

```cpp
actualTimeSeconds =
    (millis() - testStartMillis)
    / 1000.0;
```

The completion time is then transmitted over serial for error calculation and data analysis.

---

## Wi-Fi Access Point

The ESP32 creates its own Wi-Fi access point rather than requiring an external router.

The network is configured in the firmware and the ESP32 hosts the web server locally.

```cpp
WiFi.mode(WIFI_AP);

WiFi.softAP(
    apSSID,
    apPassword
);
```

The web server runs on port 80.

```cpp
AsyncWebServer server(80);
```

---

## Web Interface

The firmware contains an embedded HTML, CSS, and JavaScript interface.

The interface displays:

- Current temperature
- Estimated time remaining
- Estimated clock time when the target temperature will be reached
- Current temperature status

The browser periodically requests updated values from the ESP32 using HTTP endpoints.

### Temperature

```text
/temperaturef
```

### Temperature Status

```text
/status
```

### Prediction Estimate

```text
/estimate
```

The interface refreshes temperature, status, and prediction data approximately every 10 seconds while maintaining a one-second countdown display between updates.

---

## Main Firmware Flow

The firmware follows this general sequence:

```text
Start ESP32
      |
      v
Initialize temperature sensor
      |
      v
Start Wi-Fi access point and web server
      |
      v
Begin prediction test
      |
      v
Read temperature
      |
      v
Collect raw samples
      |
      v
Estimate initial k
      |
      v
Generate 30 / 45 / 60 second predictions
      |
      v
Continue sampling
      |
      v
Apply adaptive corrections every 10 seconds
      |
      v
Update web interface
      |
      v
Detect actual completion
      |
      v
Send completion time to Python logger
```

---

## Source File

The full firmware implementation is located in:

```text
CarCabinTemperaturePredictionSystem.cpp
```

The source file includes the sensor interface, prediction model, adaptive correction system, serial communication, Wi-Fi access point, web server, and user interface.
