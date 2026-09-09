// ==========================================================
// Car Cabin Temperature Prediction System
// Development firmware branch
//
// Latest repeated vehicle-test results in this repository: V5.2
// This source also contains an experimental post-V5.2 local-k
// stability draft that has not been validated by a new test series.
// ==========================================================


#ifdef ESP32

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#else

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#endif


#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>



// ==========================================================
// TEMPERATURE SENSOR
// ==========================================================

#define ONE_WIRE_BUS 13


OneWire oneWire(ONE_WIRE_BUS);

DallasTemperature sensors(&oneWire);



// ==========================================================
// WI-FI SOFT ACCESS POINT
// ==========================================================

const char* apSSID = "CarTemp";
const char* apPassword = "CarTemp123";

AsyncWebServer server(80);


// ==========================================================
// TIMING AND MODEL SETTINGS
// ==========================================================


// Temperature sensor update interval.
const unsigned long TEMPERATURE_DELAY = 1000;


// Raw temperature sampling interval.
const unsigned long SAMPLE_DELAY = 500;


// Adaptive correction interval.
const unsigned long CORRECTION_INTERVAL = 10000;

const unsigned long INITIAL_K_INTERVAL = 5000;


// Number of prediction windows.

#define MODEL_COUNT 3


// Initial prediction windows, in seconds.

int modelTimes[MODEL_COUNT] =
{
  30,
  45,
  60
};



// Maximum number of raw samples stored in memory.

#define MAX_SAMPLES 125



float temperatureSamples[MAX_SAMPLES];

unsigned long sampleTimes[MAX_SAMPLES];


int sampleCount = 0;



// ==========================================================
// TEMPERATURE VARIABLES
// ==========================================================


float currentTempF = 0;


float ambientEstimate = 75.0;


String temperatureF = "";


String tempStatus = "Loading...";



// ==========================================================
// PREDICTION MODEL STATE
// ==========================================================


float initialPredictions[MODEL_COUNT];

float correctedPredictions[MODEL_COUNT];

float adaptiveK[MODEL_COUNT];

float initialK[MODEL_COUNT];



// ==========================================================
// TEST STATE
// ==========================================================


unsigned long testStartMillis = 0;


unsigned long lastTemperatureRead = 0;


unsigned long lastSampleRead = 0;


unsigned long lastCorrectionUpdate = 0;


unsigned long lastInitialKUpdate = 0;



bool testActive = false;


// ==========================================================
// EXPERIMENTAL LOCAL-K INTERVAL STATE
//
// Future-work draft only. This logic is not part of the repeated
// V5.2 vehicle-test dataset reported in docs/testing-results.md.
// ==========================================================

float currentInitialK = -1;

int initialKIntervalStartIndex = -1;

int initialKIntervalNumber = 0;

const float INITIAL_K_CAP = 0.30;

const float INITIAL_K_INTERVAL_SECONDS = 5.0;


bool modelCreated[MODEL_COUNT] = 
{
  false,
  false,
  false
};


float actualTimeSeconds = -1;



float testStartTemperature = 0;



// ==========================================================
// WEB INTERFACE STATE
// ==========================================================


String predictedTimeText = "Calculating...";



// ==========================================================
// SENSOR VALIDATION
// ==========================================================


bool isSensorFailure(float temp)
{

  return int(temp) == -196;

}



// ==========================================================
// POLL STATUS
// ==========================================================


void updateTemperatureStatus(float temp)
{

  if(isSensorFailure(temp))
  {

    tempStatus = "Sensor Failure";

    return;

  }



  if(temp < 68 || temp > 72)
  {

    tempStatus = "Not Ideal Temperature";

  }

  else
  {

    tempStatus = "Ideal Temperature";

  }

}



// ==========================================================
// TEMPERATURE READING
// ==========================================================


String readTemperatureF()
{

  sensors.requestTemperatures();


  float temp = sensors.getTempFByIndex(0);



  if(isSensorFailure(temp))
  {

    updateTemperatureStatus(temp);

    return "Sensor Failure";

  }



  currentTempF = temp;


  updateTemperatureStatus(temp);



  return String(temp,2);

}



// ==========================================================
// SERIAL SAMPLE MESSAGE
// ==========================================================


void printSample(unsigned long elapsed, float temp)
{

  Serial.print("SAMPLE,");

  Serial.print(elapsed);

  Serial.print(",");

  Serial.println(temp,2);

}



// ==========================================================
// START PREDICTION TEST
// ==========================================================


void startPredictionTest()
{

  currentInitialK = -1;
  initialKIntervalStartIndex = -1;
  initialKIntervalNumber = 0;

  Serial.println();

  Serial.println("==============================");

  Serial.println("NEW PREDICTION TEST START");

  Serial.println("==============================");



  sampleCount = 0;


  for(int i = 0; i < MODEL_COUNT; i++)
  {
    modelCreated[i] = false;
  }


  testActive = true;


  actualTimeSeconds = -1;



  for(int i=0;i<MODEL_COUNT;i++)
  {

    initialPredictions[i] = -1;

    correctedPredictions[i] = -1;

    adaptiveK[i] = -1;

    initialK[i] = -1;

  }



  testStartMillis = millis();


  testStartTemperature = currentTempF;



  Serial.print("START_TEMP:");

  Serial.println(testStartTemperature,2);



  Serial.print("AMBIENT_ESTIMATE:");

  Serial.println(ambientEstimate,2);



}



// ==========================================================
// CAPTURE RAW SAMPLE
// ==========================================================


void captureSample()
{

  if(sampleCount >= MAX_SAMPLES)
  {

    return;

  }



  unsigned long now = millis();



  temperatureSamples[sampleCount] = currentTempF;


  sampleTimes[sampleCount] = now;



  unsigned long elapsed =
      (now - testStartMillis) / 1000;



  printSample(
    elapsed,
    currentTempF
  );



  sampleCount++;



  Serial.print("SAMPLE_COUNT:");

  Serial.println(sampleCount);



}

// ==========================================================
// PREDICTION MODEL AND ADAPTIVE-CORRECTION LOGIC
//
// Note: the consecutive local-k initial estimator below is an
// experimental post-V5.2 development draft. Reported test results
// in this repository come from the completed V5.2 test campaign.
// ==========================================================



// ==========================================================
// COUNT SAMPLES THROUGH A GIVEN TIME
// ==========================================================


int getSamplesAtTime(int seconds)
{

  int count = 0;


  for(int i = 0; i < sampleCount; i++)
  {

    unsigned long elapsed =
      (sampleTimes[i]-testStartMillis)/1000.0f;


    if(elapsed <= seconds)
    {

      count++;

    }

  }


  return count;

}



// ==========================================================
// INITIAL PREDICTION FROM ACCEPTED K
// ==========================================================

float calculateInitialPrediction(
    int samplesUsed,
    int modelIndex)
{
  if(currentInitialK <= 0)
  {
    return -1;
  }

  float currentDifference =
      currentTempF - ambientEstimate;

  if(currentDifference <= 0)
  {
    return 0;
  }

  // --------------------------------------------------------
  // Predict until the cabin is within 1°F of the ambient estimate.
  //
  // Example:
  // ambientEstimate = 75
  // target = 76
  // --------------------------------------------------------

  const float TARGET_OFFSET = 1.0;

  float targetDifference =
      TARGET_OFFSET;


  if(currentDifference <= targetDifference)
  {
    return 0;
  }


  float remaining =
      log(
        currentDifference /
        targetDifference
      )
      /
      currentInitialK;


  if(remaining < 0)
  {
    return -1;
  }


  return remaining;
}





// ==========================================================
// CALCULATE K FOR A 5-SECOND INTERVAL
//
// Example:
// 20 -> 25
// 25 -> 30
// 30 -> 35
// etc.
// ==========================================================

float calculateIntervalK(int startIndex, int endIndex)
{
  if(startIndex < 0 || endIndex <= startIndex)
  {
    return -1;
  }

  float sumTime = 0;
  float sumLog = 0;
  float sumTimeSquared = 0;
  float sumTimeLog = 0;

  int valid = 0;

  unsigned long intervalStartTime =
      sampleTimes[startIndex];

  for(int i = startIndex; i <= endIndex; i++)
  {
    float difference =
        temperatureSamples[i] - ambientEstimate;

    if(difference <= 0)
    {
      continue;
    }

    float t =
        (sampleTimes[i] - intervalStartTime) / 1000.0;

    float y =
        log(difference);

    sumTime += t;
    sumLog += y;
    sumTimeSquared += t * t;
    sumTimeLog += t * y;

    valid++;
  }

  if(valid < 3)
  {
    return -1;
  }

  float denominator =
      (valid * sumTimeSquared)
      -
      (sumTime * sumTime);

  if(abs(denominator) < 0.000001)
  {
    return -1;
  }

  float slope =
      ((valid * sumTimeLog)
      -
      (sumTime * sumLog))
      /
      denominator;

  float k = -slope;

  if(k <= 0)
  {
    return -1;
  }

  return k;
}


// ==========================================================
// UPDATE INITIAL K FROM 5-SECOND INTERVALS
//
// 20 -> 25
// 25 -> 30
// 30 -> 35
// ...
// ==========================================================

void updateInitialK()
{
  if(sampleCount < 3)
  {
    return;
  }

  // --------------------------------------------------------
  // Begin initial-k estimation at the first sample at or after 20 seconds.
  // --------------------------------------------------------

  if(initialKIntervalStartIndex < 0)
  {
    for(int i = 0; i < sampleCount; i++)
    {
      float elapsed =
          (sampleTimes[i] - testStartMillis) / 1000.0;

      if(elapsed >= 20.0)
      {
        initialKIntervalStartIndex = i;

        Serial.println("INITIAL_K_INTERVAL_STARTED");

        break;
      }
    }
  }

  if(initialKIntervalStartIndex < 0)
  {
    return;
  }

  // --------------------------------------------------------
  // Determine the end of the current 5-second interval.
  // --------------------------------------------------------

  float intervalStartElapsed =
      (sampleTimes[initialKIntervalStartIndex]
       - testStartMillis) / 1000.0;

  float intervalEndElapsed =
      intervalStartElapsed
      + INITIAL_K_INTERVAL_SECONDS;


  // --------------------------------------------------------
  // Wait until enough data exists to close the current interval.
  // --------------------------------------------------------

  int intervalEndIndex = -1;

  for(int i = initialKIntervalStartIndex;
      i < sampleCount;
      i++)
  {
    float elapsed =
        (sampleTimes[i] - testStartMillis) / 1000.0;

    if(elapsed >= intervalEndElapsed)
    {
      intervalEndIndex = i;
      break;
    }
  }


  if(intervalEndIndex < 0)
  {
    return;
  }


  // --------------------------------------------------------
  // Calculate the raw k value for this interval.
  // --------------------------------------------------------

  float rawK =
      calculateIntervalK(
          initialKIntervalStartIndex,
          intervalEndIndex
      );


  if(rawK <= 0)
  {
    Serial.println("INITIAL_K_INTERVAL_INVALID");

    // Skip an invalid interval so it does not stop later updates.

    initialKIntervalStartIndex =
        intervalEndIndex;

    return;
  }


  // --------------------------------------------------------
  // FIRST VALID INTERVAL
  //
  // The first valid interval establishes the reference k.
  // No limiter is applied until a previous accepted k exists.
  // --------------------------------------------------------

  if(currentInitialK < 0)
  {
    currentInitialK = rawK;

    initialKIntervalNumber = 1;

    Serial.print("INITIAL_K_INTERVAL,");
    Serial.print(intervalStartElapsed);
    Serial.print(",");
    Serial.print(intervalEndElapsed);
    Serial.print(",");
    Serial.print(rawK, 8);
    Serial.print(",");
    Serial.println(currentInitialK, 8);
  }


  // --------------------------------------------------------
  // FOLLOWING INTERVALS
  // Limit raw k relative to the previously accepted k.
  // --------------------------------------------------------

  else
  {
    float previousK =
        currentInitialK;


    float minK =
        previousK * (1.0 - INITIAL_K_CAP);


    float maxK =
        previousK * (1.0 + INITIAL_K_CAP);


    float acceptedK =
        rawK;


    if(acceptedK > maxK)
    {
      acceptedK = maxK;
    }


    if(acceptedK < minK)
    {
      acceptedK = minK;
    }


    currentInitialK =
        acceptedK;


    initialKIntervalNumber++;


    Serial.print("INITIAL_K_INTERVAL,");
    Serial.print(intervalStartElapsed);
    Serial.print(",");
    Serial.print(intervalEndElapsed);
    Serial.print(",");

    Serial.print("RAW_K=");
    Serial.print(rawK, 8);

    Serial.print(",PREVIOUS_K=");
    Serial.print(previousK, 8);

    Serial.print(",ACCEPTED_K=");
    Serial.println(currentInitialK, 8);
  }


  // --------------------------------------------------------
  // Advance to the next non-overlapping interval.
  //
  // 20->25 becomes 25->30
  // 25->30 becomes 30->35
  // etc.
  // --------------------------------------------------------

  initialKIntervalStartIndex =
      intervalEndIndex;
}


// ==========================================================
// ADAPTIVE NEWTON PREDICTION
//
// Ta - T = (Ta - T0)e^-kt
//
// Returns the predicted remaining time in seconds.
// ==========================================================

float calculateNewtonPrediction(int samplesUsed, int modelIndex)
{


  if(samplesUsed < 3)
  {

    return -1;

  }



  float sumTime = 0;

  float sumLog = 0;

  float sumTimeSquared = 0;

  float sumTimeLog = 0;



  int valid = 0;



  for(int i = 0; i < samplesUsed; i++)
  {


    float t =
      (sampleTimes[i] - sampleTimes[0]) / 1000.0;



    float difference =
      temperatureSamples[i] - ambientEstimate;

    if(difference <= 0)
    {
      continue;
    }

    float y = log(difference);


    sumTime += t;

    sumLog += y;

    sumTimeSquared += t*t;

    sumTimeLog += t*y;



    valid++;


  }




  if(valid < 3)
  {

    return -1;

  }




  float denominator =
      (valid * sumTimeSquared)
      -
      (sumTime * sumTime);



  if(abs(denominator) < 0.000001)
  {

    return -1;

  }



  float slope =
      ((valid * sumTimeLog)
      -
      (sumTime * sumLog))
      /
      denominator;




  float k = -slope;


  if(k <= 0)
  {

    return -1;

  }



  // ================================
  // Limit and smooth changes in the adaptive k value.
  // ================================


  if(adaptiveK[modelIndex] < 0)
  {

    adaptiveK[modelIndex] = k;

  }

  else
  {

    float maxKChange = 0.10;



    float minK =
      adaptiveK[modelIndex]
      *
      (1.0 - maxKChange);



    float maxK =
      adaptiveK[modelIndex]
      *
      (1.0 + maxKChange);



    if(k < minK)
    {

      k = minK;

    }


    if(k > maxK)
    {

      k = maxK;

    }



    adaptiveK[modelIndex] =
      0.7 * adaptiveK[modelIndex]
      +
      0.3 * k;


  }





  float currentDifference =
      currentTempF - ambientEstimate;



  if(currentDifference <= 0)
  {

    return 0;

  }




  float remaining =

      -log(1.0 / currentDifference)
      /
      adaptiveK[modelIndex];



  return remaining;


}




// ==========================================================
// UPDATE ADAPTIVE PREDICTIONS
// ==========================================================


void updatePredictionCorrections()
{


  bool anyModelReady = false;


  for(int i=0;i<MODEL_COUNT;i++)
  {

    if(modelCreated[i])
    {
      anyModelReady = true;
    }

  }


if(!anyModelReady)
{
  return;
}




  Serial.println();

  Serial.println("====== CORRECTION UPDATE ======");




  for(int i = 0; i < MODEL_COUNT; i++)
  {

    if(!modelCreated[i])
    {
      continue;
    }



    int seconds =
      modelTimes[i];



    int samples =
      sampleCount;



    float remaining =
      calculateNewtonPrediction(
        samples,
        i
      );



    if(remaining < 0)
    {

      continue;

    }




    float elapsed =
      (millis() - testStartMillis)
      /
      1000.0;



    float newPrediction =
      elapsed + remaining;



    float oldPrediction =
      correctedPredictions[i];




    if(oldPrediction < 0)
    {

      oldPrediction =
        initialPredictions[i];

    }




    float percentChange = 0;



    if(oldPrediction > 0)
    {


      percentChange =
        abs(newPrediction-oldPrediction)
        /
        oldPrediction;


    }




    // ================================
    // Limit each adaptive prediction update to a 25% change.
    // ================================


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




    correctedPredictions[i] =
      newPrediction;




    Serial.print("CORRECTION_UPDATE,");

    Serial.print(elapsed);

    Serial.print(",");

    Serial.print(currentTempF);

    Serial.print(",");

    Serial.print(seconds);

    Serial.print(",");

    Serial.println(newPrediction);



    Serial.print("MODEL_");

    Serial.print(seconds);

    Serial.print("_CHANGE_PERCENT:");

    Serial.println(percentChange * 100.0);



  }


}



// ==========================================================
// CHECK TEST COMPLETION
// ==========================================================


void checkActualCompletion()
{


  if(!testActive)
  {

    return;

  }




  if(currentTempF <= ambientEstimate)
  {


    actualTimeSeconds =
      (millis()-testStartMillis)
      /
      1000.0;




    Serial.print("ACTUAL_TIME_SECONDS:");

    Serial.println(actualTimeSeconds);



    Serial.println("TEST_COMPLETE");



    testActive = false;



  }



}

// ==========================================================
// WEB INTERFACE, SETUP, AND MAIN LOOP
// ==========================================================



// ==========================================================
// EMBEDDED WEB PAGE
// ==========================================================


const char index_html[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>CarTemp</title>

<style>
:root {
  --bg-top: #eef3f9;
  --bg-bottom: #f8fafc;
  --card: rgba(255,255,255,0.94);
  --text: #172033;
  --muted: #6f7b8d;
  --border: rgba(23,32,51,0.08);
  --shadow: 0 18px 45px rgba(35, 52, 78, 0.10);
  --accent: #2f6fed;
  --accent-soft: #eaf1ff;
  --good: #16855b;
  --good-soft: #e8f7f0;
  --warn: #b55c14;
  --warn-soft: #fff1e5;
  --danger: #b33a3a;
  --danger-soft: #fdecec;
}

* {
  box-sizing: border-box;
}

html {
  min-height: 100%;
  background: var(--bg-bottom);
}

body {
  margin: 0;
  min-height: 100vh;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif;
  color: var(--text);
  background:
    radial-gradient(circle at top right, rgba(47,111,237,0.10), transparent 34%),
    linear-gradient(180deg, var(--bg-top) 0%, var(--bg-bottom) 72%);
}

.shell {
  width: min(100%, 520px);
  margin: 0 auto;
  padding:
    max(34px, env(safe-area-inset-top))
    18px
    max(34px, env(safe-area-inset-bottom));
}

.header {
  margin: 12px 4px 26px;
}

.brand-row {
  display: flex;
  align-items: center;
  gap: 12px;
}

.brand-mark {
  width: 42px;
  height: 42px;
  border-radius: 13px;
  display: grid;
  place-items: center;
  background: var(--accent);
  box-shadow: 0 10px 24px rgba(47,111,237,0.24);
}

.brand-mark::before {
  content: "";
  width: 18px;
  height: 18px;
  border: 3px solid white;
  border-top-color: transparent;
  border-radius: 50%;
  transform: rotate(-35deg);
}

.brand {
  font-size: 2rem;
  line-height: 1;
  font-weight: 800;
  letter-spacing: -0.045em;
}

.subtitle {
  margin: 8px 0 0 54px;
  color: var(--muted);
  font-size: 0.95rem;
}

.card {
  background: var(--card);
  border: 1px solid var(--border);
  border-radius: 24px;
  box-shadow: var(--shadow);
  backdrop-filter: blur(12px);
  -webkit-backdrop-filter: blur(12px);
}

.temperature-card {
  padding: 26px;
  margin-bottom: 16px;
}

.eyebrow {
  color: var(--muted);
  font-size: 0.78rem;
  font-weight: 700;
  letter-spacing: 0.12em;
  text-transform: uppercase;
}

.temperature-row {
  display: flex;
  justify-content: space-between;
  align-items: flex-end;
  gap: 18px;
  margin-top: 10px;
}

.temperature {
  font-size: clamp(4rem, 19vw, 5.4rem);
  line-height: 0.95;
  font-weight: 800;
  letter-spacing: -0.065em;
  white-space: nowrap;
}

.unit {
  font-size: 0.45em;
  margin-left: 4px;
  vertical-align: 0.45em;
  letter-spacing: -0.02em;
}

.target-chip {
  flex: 0 0 auto;
  margin-bottom: 4px;
  padding: 9px 12px;
  border-radius: 999px;
  background: var(--accent-soft);
  color: var(--accent);
  font-size: 0.84rem;
  font-weight: 700;
  white-space: nowrap;
}

.grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 14px;
  margin-bottom: 16px;
}

.info-card {
  min-height: 154px;
  padding: 21px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
}

.info-value {
  margin-top: 15px;
  font-size: 1.7rem;
  line-height: 1.05;
  font-weight: 800;
  letter-spacing: -0.035em;
}

.info-note {
  margin-top: 9px;
  color: var(--muted);
  font-size: 0.78rem;
  line-height: 1.4;
}

.status-card {
  padding: 22px;
}

.status-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 15px;
}

.status-label {
  color: var(--muted);
  font-size: 0.78rem;
  font-weight: 700;
  letter-spacing: 0.12em;
  text-transform: uppercase;
}

.status-pill {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 9px 12px;
  border-radius: 999px;
  font-size: 0.86rem;
  font-weight: 750;
  background: var(--warn-soft);
  color: var(--warn);
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: currentColor;
}

.status-pill.ideal {
  background: var(--good-soft);
  color: var(--good);
}

.status-pill.not-ideal {
  background: var(--warn-soft);
  color: var(--warn);
}

.status-pill.failure {
  background: var(--danger-soft);
  color: var(--danger);
}

.status-text {
  margin-top: 17px;
  font-size: 1.42rem;
  line-height: 1.2;
  font-weight: 800;
  letter-spacing: -0.025em;
}

.footer {
  margin: 19px 4px 0;
  color: var(--muted);
  text-align: center;
  font-size: 0.75rem;
}

@media (max-width: 390px) {
  .grid {
    grid-template-columns: 1fr;
  }

  .info-card {
    min-height: 132px;
  }

  .temperature-row {
    align-items: center;
  }

  .target-chip {
    font-size: 0.76rem;
    padding: 8px 10px;
  }
}
</style>
</head>

<body>
<div class="shell">

  <header class="header">
    <div class="brand-row">
      <div class="brand-mark"></div>
      <div class="brand">CarTemp</div>
    </div>
    <div class="subtitle">Live cabin temperature prediction</div>
  </header>

  <section class="card temperature-card">
    <div class="eyebrow">Current temperature</div>

    <div class="temperature-row">
      <div class="temperature">
        <span id="temperature">--</span><span class="unit">&deg;F</span>
      </div>

      <div class="target-chip">
        Target <span id="targetChip">--</span>&deg;F
      </div>
    </div>
  </section>

  <div class="grid">
    <section class="card info-card">
      <div>
        <div class="eyebrow">Estimated time</div>
        <div class="info-value" id="estimate">Calculating...</div>
      </div>

      <div class="info-note" id="estimateNote">
        Available after the 60-second model is ready.
      </div>
    </section>

    <section class="card info-card">
      <div>
        <div class="eyebrow">Ideal target</div>
        <div class="info-value">
          <span id="targetTemperature">--</span>&deg;F
        </div>
      </div>

      <div class="info-note">
        Reference temperature used by the prediction model.
      </div>
    </section>
  </div>

  <section class="card status-card">
    <div class="status-top">
      <div class="status-label">Cabin status</div>

      <div class="status-pill not-ideal" id="statusPill">
        <span class="status-dot"></span>
        <span id="statusShort">Checking</span>
      </div>
    </div>

    <div class="status-text" id="status">
      Loading...
    </div>
  </section>

  <div class="footer">
    ESP32 local interface
  </div>

</div>

<script>
let remainingSeconds = -1;
let lastUpdateTime = Date.now();

function formatTime(seconds)
{
  if(seconds < 0)
  {
    return "Calculating...";
  }

  seconds = Math.max(0, Math.round(seconds));

  let minutes = Math.floor(seconds / 60);
  let remaining = seconds % 60;

  if(minutes <= 0)
  {
    return remaining + " sec";
  }

  return minutes + " min " +
         String(remaining).padStart(2, "0") +
         " sec";
}

function updateTemperature()
{
  fetch("/temperaturef")
  .then(response => response.text())
  .then(data =>
  {
    document.getElementById("temperature").textContent = data;
  });
}

function updateTarget()
{
  fetch("/target")
  .then(response => response.text())
  .then(data =>
  {
    let value = parseFloat(data);

    if(!Number.isNaN(value))
    {
      let formatted =
        Number.isInteger(value) ? value.toFixed(0) : value.toFixed(1);

      document.getElementById("targetChip").textContent = formatted;
      document.getElementById("targetTemperature").textContent = formatted;
    }
  });
}

function updateStatus()
{
  fetch("/status")
  .then(response => response.text())
  .then(data =>
  {
    const status = data.trim();

    document.getElementById("status").textContent = status;

    const pill = document.getElementById("statusPill");
    const shortText = document.getElementById("statusShort");

    pill.className = "status-pill";

    if(status === "Ideal Temperature")
    {
      pill.classList.add("ideal");
      shortText.textContent = "Ideal";
    }
    else if(status === "Sensor Failure")
    {
      pill.classList.add("failure");
      shortText.textContent = "Sensor error";
    }
    else
    {
      pill.classList.add("not-ideal");
      shortText.textContent = "Not ideal";
    }
  });
}

function updatePrediction()
{
  fetch("/estimate")
  .then(response => response.text())
  .then(data =>
  {
    const value = parseFloat(data);

    if(Number.isNaN(value) || value < 0)
    {
      remainingSeconds = -1;
    }
    else
    {
      remainingSeconds = value;
    }

    lastUpdateTime = Date.now();
    updateDisplay();
  });
}

function updateDisplay()
{
  const estimate = document.getElementById("estimate");
  const note = document.getElementById("estimateNote");

  if(remainingSeconds < 0)
  {
    estimate.textContent = "Calculating...";
    note.textContent =
      "Available after the 60-second model is ready.";
    return;
  }

  let elapsedSinceUpdate =
    (Date.now() - lastUpdateTime) / 1000;

  let displayedSeconds =
    Math.max(
      0,
      remainingSeconds - elapsedSinceUpdate
    );

  estimate.textContent = formatTime(displayedSeconds);
  note.textContent = "60-second model with adaptive correction.";
}

setInterval(updateDisplay, 1000);

setInterval(function()
{
  updateTemperature();
  updateStatus();
}, 2000);

setInterval(updatePrediction, 2000);

updateTemperature();
updateTarget();
updateStatus();
updatePrediction();

</script>
</body>
</html>

)rawliteral";







// ==========================================================
// FORMAT TIME
// ==========================================================


String formatTime(float seconds)
{


if(seconds < 0)
{

return "Calculating...";

}



int total =
(int)(seconds+0.5);



int minutes =
total/60;



int remaining =
total%60;



return String(minutes)
+
" min "
+
String(remaining)
+
" sec";

}




// ==========================================================
// SETUP
// ==========================================================


void setup()
{


Serial.begin(115200);



Serial.println();

Serial.println("==============================");

Serial.println("Temperature Prediction System");

Serial.println("==============================");





// Initialize the temperature sensor.

sensors.begin();





temperatureF =
readTemperatureF();





// ==========================================================
// START ESP32 SOFT ACCESS POINT
// ==========================================================

WiFi.mode(WIFI_AP);

bool apStarted = WiFi.softAP(
  apSSID,
  apPassword
);

if (apStarted)
{
  Serial.println("SoftAP Started");

  Serial.print("Network Name: ");
  Serial.println(apSSID);

  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());
}
else
{
  Serial.println("SoftAP Failed");
}


// ========================================================
// WEB SERVER ROUTES
// ========================================================

  server.on(
    "/",
    HTTP_GET,
    [](AsyncWebServerRequest *request)
    {
      request->send(
        200,
        "text/html",
        index_html
      );
    }
  );


  server.on(
    "/temperaturef",
    HTTP_GET,
    [](AsyncWebServerRequest *request)
    {
      request->send(
        200,
        "text/plain",
        temperatureF
      );
    }
  );


  server.on(
    "/status",
    HTTP_GET,
    [](AsyncWebServerRequest *request)
    {
      request->send(
        200,
        "text/plain",
        tempStatus
      );
    }
  );


  server.on(
    "/target",
    HTTP_GET,
    [](AsyncWebServerRequest *request)
    {
      request->send(
        200,
        "text/plain",
        String(ambientEstimate, 1)
      );
    }
  );


  server.on(
    "/estimate",
    HTTP_GET,
    [](AsyncWebServerRequest *request)
    {

      if(correctedPredictions[2] < 0)
      {
        request->send(
          200,
          "text/plain",
          "-1"
        );

        return;
      }


      float elapsed =
        (millis() - testStartMillis) / 1000.0;


      float remaining =
        correctedPredictions[2] - elapsed;


      if(remaining < 0)
      {
        remaining = 0;
      }


      request->send(
        200,
        "text/plain",
        String(remaining, 1)
      );

    }
  );


  server.begin();

  Serial.println("Web Server Started");


// ========================================================
// BEGIN PREDICTION TEST
// ========================================================


startPredictionTest();




lastTemperatureRead =
millis();


lastSampleRead =
millis();


lastCorrectionUpdate =
millis();


lastInitialKUpdate =
millis();

}




// ==========================================================
// LOOP
// ==========================================================


void loop()
{


unsigned long now =
millis();


if(
testActive &&
now-lastInitialKUpdate >= INITIAL_K_INTERVAL
)
{

  updateInitialK();

  lastInitialKUpdate = now;

}


// ==========================================================
// UPDATE TEMPERATURE SENSOR
// ==========================================================


if(
now - lastTemperatureRead
>=
TEMPERATURE_DELAY
)

{


temperatureF =
readTemperatureF();



Serial.print("TEMP:");

Serial.println(currentTempF,2);



lastTemperatureRead =
now;


}







// ==========================================================
// COLLECT RAW SAMPLES
// ==========================================================


if(
testActive
&&
now-lastSampleRead >= SAMPLE_DELAY
)

{


captureSample();



lastSampleRead =
now;


// Create each initial model when its prediction window is reached.

for(int i = 0; i < MODEL_COUNT; i++)
{

  if(!modelCreated[i])
  {

    int requiredTime = modelTimes[i];


    float predictionElapsed =
      (millis()-testStartMillis)/1000;



    if(predictionElapsed >= requiredTime)
    {

      int samples =
        getSamplesAtTime(requiredTime);



      float remaining =
        calculateInitialPrediction(
          samples,
          i
        );


      float elapsed =
              (millis()-testStartMillis)
              /
              1000.0;


      float prediction =
              elapsed + remaining;


      initialPredictions[i] =
              prediction;


      correctedPredictions[i] =
              prediction;



      modelCreated[i] = true;



      Serial.print("INITIAL_MODEL,");

      Serial.print(
        (millis()-testStartMillis)/1000.0
      );

      Serial.print(",");

      Serial.print(currentTempF);

      Serial.print(",");

      Serial.print(requiredTime);

      Serial.print(",");

      Serial.println(prediction);



      Serial.print("MODEL_");

      Serial.print(requiredTime);

      Serial.println("_READY");


    }

  }

}


}







// ==========================================================
// ADAPTIVE CORRECTIONS
// ==========================================================


bool anyModelReady = false;

for(int i = 0; i < MODEL_COUNT; i++)
{
  if(modelCreated[i])
  {
    anyModelReady = true;
  }
}


if(
anyModelReady
&&
testActive
&&
now-lastCorrectionUpdate >= CORRECTION_INTERVAL
)

{


updatePredictionCorrections();



lastCorrectionUpdate =
now;



// Use the 60-second model for the web-interface estimate.

if(correctedPredictions[2] > 0)

{

predictedTimeText =
formatTime(
correctedPredictions[2]
);


}


}






// ==========================================================
// TEST COMPLETION
// ==========================================================


checkActualCompletion();



}