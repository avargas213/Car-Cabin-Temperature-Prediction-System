// ==========================================================
// Temperature Prediction System V6.0
// Newton Cooling Model Validation Version
// Part 1/3
// ==========================================================


#ifdef ESP33

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
// SENSOR SETUP
// ==========================================================

#define ONE_WIRE_BUS 13


OneWire oneWire(ONE_WIRE_BUS);

DallasTemperature sensors(&oneWire);



// ==========================================================
// WIFI / SOFT AP SETUP
// ==========================================================

const char* apSSID = "CarTemp";
const char* apPassword = "CarTemp123";

AsyncWebServer server(80);


// ==========================================================
// EXPERIMENT SETTINGS
// ==========================================================


// How often temperature sensor updates
const unsigned long TEMPERATURE_DELAY = 1000;


// Initial sampling rate
const unsigned long SAMPLE_DELAY = 500;


// Correction update interval
const unsigned long CORRECTION_INTERVAL = 10000;

const unsigned long INITIAL_K_INTERVAL = 5000;


// Number of prediction models

#define MODEL_COUNT 3


// Sampling windows

int modelTimes[MODEL_COUNT] =
{
  30,
  45,
  60
};



// Maximum stored samples

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
// MODEL STORAGE
// ==========================================================


float initialPredictions[MODEL_COUNT];

float correctedPredictions[MODEL_COUNT];

float adaptiveK[MODEL_COUNT];

float initialK[MODEL_COUNT];



// ==========================================================
// TEST VARIABLES
// ==========================================================


unsigned long testStartMillis = 0;


unsigned long lastTemperatureRead = 0;


unsigned long lastSampleRead = 0;


unsigned long lastCorrectionUpdate = 0;


unsigned long lastInitialKUpdate = 0;



bool testActive = false;


// ==========================================================
// INITIAL K INTERVAL TRACKING
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
// WEB PAGE VARIABLES
// ==========================================================


String predictedTimeText = "Calculating...";



// ==========================================================
// SENSOR FAILURE CHECK
// ==========================================================


bool isSensorFailure(float temp)
{

  return int(temp) == -196;

}



// ==========================================================
// STATUS UPDATE
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
// READ TEMPERATURE
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
// SERIAL SAMPLE OUTPUT
// ==========================================================


void printSample(unsigned long elapsed, float temp)
{

  Serial.print("SAMPLE,");

  Serial.print(elapsed);

  Serial.print(",");

  Serial.println(temp,2);

}



// ==========================================================
// START TEST
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
// CAPTURE INITIAL SAMPLES
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
// Temperature Prediction System V4.2
// Newton Cooling Model Validation Version
// Part 2/3
// Model Calculations + Adaptive Correction
// ==========================================================



// ==========================================================
// FIND NUMBER OF SAMPLES AT A GIVEN TIME
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
// INITIAL PREDICTION USING CURRENT ACCEPTED K
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
  // Target is defined as ambient + 1°F.
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
// CALCULATE K FOR ONE 5-SECOND INTERVAL
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
// UPDATE INITIAL K USING 5-SECOND INTERVALS
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
  // Find the first sample at or after 20 seconds
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
  // Determine when the current 5-second interval ends
  // --------------------------------------------------------

  float intervalStartElapsed =
      (sampleTimes[initialKIntervalStartIndex]
       - testStartMillis) / 1000.0;

  float intervalEndElapsed =
      intervalStartElapsed
      + INITIAL_K_INTERVAL_SECONDS;


  // --------------------------------------------------------
  // Check whether we have reached the end
  // of this 5-second interval
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
  // Calculate RAW K for this interval
  // --------------------------------------------------------

  float rawK =
      calculateIntervalK(
          initialKIntervalStartIndex,
          intervalEndIndex
      );


  if(rawK <= 0)
  {
    Serial.println("INITIAL_K_INTERVAL_INVALID");

    // Move forward anyway so one bad interval
    // does not permanently stop the system.

    initialKIntervalStartIndex =
        intervalEndIndex;

    return;
  }


  // --------------------------------------------------------
  // FIRST INTERVAL
  //
  // 20 -> 25 becomes the reference K.
  // No cap is applied because there is nothing
  // to compare it against yet.
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
  // ALL FOLLOWING INTERVALS
  // Compare RAW K to PREVIOUS ACCEPTED K
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
  // Move to the NEXT interval
  //
  // 20->25 becomes 25->30
  // 25->30 becomes 30->35
  // etc.
  // --------------------------------------------------------

  initialKIntervalStartIndex =
      intervalEndIndex;
}


// ==========================================================
// NEWTON COOLING PREDICTION
//
// Ta - T = (Ta - T0)e^-kt
//
// Returns remaining seconds
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
  // Adaptive smoothing of k
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
// APPLY ADAPTIVE CORRECTIONS
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
    // 25% Prediction Jump Limit
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
// CHECK ACTUAL COMPLETION
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
// Temperature Prediction System V4.2
// Newton Cooling Model Validation Version
// Part 3/3
// Setup + Loop + Web Interface
// ==========================================================



// ==========================================================
// HTML PAGE
// ==========================================================


const char index_html[] PROGMEM = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<meta name="viewport" content="width=device-width, initial-scale=1">

<title>CarTemp</title>

<style>

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  font-family: Arial, sans-serif;
  background: #f5f7fa;
  color: #222;
  text-align: center;
}

.container {
  max-width: 500px;
  margin: auto;
  padding: 30px 20px;
}

.title {
  font-size: 2.2rem;
  font-weight: bold;
  margin-bottom: 35px;
}

.card {
  background: white;
  border-radius: 20px;
  padding: 25px 20px;
  margin-bottom: 18px;
  box-shadow: 0 4px 15px rgba(0,0,0,0.08);
}

.label {
  font-size: 1rem;
  color: #777;
  text-transform: uppercase;
  letter-spacing: 1px;
  margin-bottom: 10px;
}

.temperature {
  font-size: 3.5rem;
  font-weight: bold;
}

.estimate {
  font-size: 2.5rem;
  font-weight: bold;
}

.arrival {
  margin-top: 10px;
  font-size: 1.2rem;
  color: #666;
}

.status {
  font-size: 1.2rem;
  font-weight: bold;
}

</style>

</head>


<body>

<div class="container">

  <div class="title">
    CarTemp
  </div>


  <div class="card">

    <div class="label">
      Current Temperature
    </div>

    <div class="temperature">
      <span id="temperature">--</span>°F
    </div>

  </div>


  <div class="card">

    <div class="label">
      Estimated Time
    </div>

    <div class="estimate">
      <span id="estimate">Calculating...</span>
    </div>

    <div class="arrival">
      Estimated ideal temperature:
      <span id="arrival">--:--</span>
    </div>

  </div>


  <div class="card">

    <div class="status" id="status">
      Loading...
    </div>

  </div>

</div>


<script>

let remainingSeconds = -1;

let lastUpdateTime = Date.now();


// ==========================================
// FORMAT TIME
// ==========================================

function formatTime(seconds)
{

  if(seconds < 0)
  {
    return "Calculating...";
  }

  seconds = Math.max(0, Math.round(seconds));

  let minutes = Math.floor(seconds / 60);

  let remaining = seconds % 60;

  return minutes + " min " +
         String(remaining).padStart(2, "0") +
         " sec";

}


// ==========================================
// UPDATE TEMPERATURE
// ==========================================

function updateTemperature()
{

  fetch("/temperaturef")

  .then(response => response.text())

  .then(data =>
  {

    document.getElementById("temperature").innerHTML = data;

  });

}


// ==========================================
// UPDATE STATUS
// ==========================================

function updateStatus()
{

  fetch("/status")

  .then(response => response.text())

  .then(data =>
  {

    document.getElementById("status").innerHTML = data;

  });

}


// ==========================================
// GET NEW PREDICTION
// ==========================================

function updatePrediction()
{

  fetch("/estimate")

  .then(response => response.text())

  .then(data =>
  {

    remainingSeconds = parseFloat(data);

    lastUpdateTime = Date.now();

    updateDisplay();

  });

}


// ==========================================
// UPDATE DISPLAY
// ==========================================

function updateDisplay()
{

  if(remainingSeconds < 0)
  {

    document.getElementById("estimate").innerHTML =
      "Calculating...";

    document.getElementById("arrival").innerHTML =
      "--:--";

    return;

  }


  let elapsedSinceUpdate =
    (Date.now() - lastUpdateTime) / 1000;


  let displayedSeconds =
    Math.max(
      0,
      remainingSeconds - elapsedSinceUpdate
    );


  document.getElementById("estimate").innerHTML =
    formatTime(displayedSeconds);


  // ========================================
  // ESTIMATED CLOCK TIME
  // ========================================

  let arrivalTime =
    new Date(
      Date.now() + displayedSeconds * 1000
    );


  let hours = arrivalTime.getHours();

  let minutes = arrivalTime.getMinutes();

  let suffix = hours >= 12 ? "PM" : "AM";

  hours = hours % 12;

  if(hours === 0)
  {
    hours = 12;
  }

  minutes =
    String(minutes).padStart(2, "0");


  document.getElementById("arrival").innerHTML =
    hours + ":" + minutes + " " + suffix;

}


// ==========================================
// UPDATE UI CLOCK
// ==========================================

setInterval(function()
{

  updateDisplay();

}, 1000);


// ==========================================
// TEMPERATURE UPDATE
// ==========================================

setInterval(function()
{

  updateTemperature();

}, 10000);


// ==========================================
// STATUS UPDATE
// ==========================================

setInterval(function()
{

  updateStatus();

}, 10000);


// ==========================================
// PREDICTION UPDATE
// ==========================================

setInterval(function()
{

  updatePrediction();

}, 10000);


// Initial requests

updateTemperature();

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





// Sensor

sensors.begin();





temperatureF =
readTemperatureF();





// ==========================================================
// START ESP32 SOFT AP
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
// WEB SERVER
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
// START PREDICTION TEST
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
// SENSOR UPDATE
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
// SAMPLE COLLECTION
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


// Create each Newton model at its own sampling time

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
// CORRECTIONS
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



// Display best prediction (60 sec model)

if(correctedPredictions[2] > 0)

{

predictedTimeText =
formatTime(
correctedPredictions[2]
);


}


}






// ==========================================================
// COMPLETION
// ==========================================================


checkActualCompletion();



}