import serial
import csv
import os
import time


# ============================================================
# SETTINGS
# ============================================================

PORT = "COM6"
BAUD = 115200

DATA_FOLDER = (
    r"C:\Users\alber\OneDrive - University of Tennessee"
    r"\Documents\TemperatureSensorProject\Data"
)

# Initial prediction model times
MODEL_TIMES = [30, 45, 60]

INITIAL_FILES = {}
CORRECTION_FILES = {}


for model_time in MODEL_TIMES:

    INITIAL_FILES[model_time] = os.path.join(
        DATA_FOLDER,
        f"Initial_Model_{model_time}s.csv"
    )

    CORRECTION_FILES[model_time] = os.path.join(
        DATA_FOLDER,
        f"Correction_{model_time}s.csv"
    )


os.makedirs(DATA_FOLDER, exist_ok=True)


# ============================================================
# CONNECT ESP32
# ============================================================

def connect_esp32():
    """Continuously try to connect to the ESP32."""

    while True:

        try:

            print("Waiting for ESP32...")

            ser = serial.Serial(
                PORT,
                BAUD,
                timeout=1
            )

            # Give ESP32 time to reset after serial connection
            time.sleep(2)

            ser.reset_input_buffer()

            print("ESP32 Connected")

            return ser

        except serial.SerialException:

            time.sleep(1)


# ============================================================
# GET NEXT TEST NUMBER
# ============================================================

def get_next_test_number():
    """
    Find the highest existing test number across all
    initial-model and correction files.
    """

    highest = 0

    all_files = (
        list(INITIAL_FILES.values())
        + list(CORRECTION_FILES.values())
    )

    for file in all_files:

        if not os.path.exists(file):
            continue

        try:

            with open(
                file,
                "r",
                newline=""
            ) as f:

                reader = csv.DictReader(f)

                for row in reader:

                    try:

                        number = int(
                            row["Test Number"]
                        )

                        highest = max(
                            highest,
                            number
                        )

                    except (
                        ValueError,
                        TypeError,
                        KeyError
                    ):

                        pass

        except OSError:

            pass

    return highest + 1


# ============================================================
# ERROR CALCULATION
# ============================================================

def percent_error(prediction, actual):
    """
    Calculate absolute percentage error.

    Error is calculated relative to the actual time.
    """

    if prediction is None or actual is None:
        return ""

    if actual == 0:
        return ""

    return round(
        abs(prediction - actual)
        / actual
        * 100,
        2
    )


# ============================================================
# PREDICTION CHANGE CALCULATION
# ============================================================

def prediction_change_percent(
    initial_prediction,
    corrected_prediction
):
    """
    Calculate the absolute percentage change from
    the initial prediction to the corrected prediction.
    """

    if (
        initial_prediction is None
        or corrected_prediction is None
    ):
        return ""

    if initial_prediction == 0:
        return ""

    return round(
        abs(
            corrected_prediction
            - initial_prediction
        )
        / initial_prediction
        * 100,
        2
    )


# ============================================================
# CREATE CSV FILES
# ============================================================

def create_files():
    """Create CSV files and headers if they do not exist."""

    # --------------------------------------------------------
    # INITIAL MODEL HEADERS
    # --------------------------------------------------------

    initial_headers = [
        "Test Number",
        "Sample Number",
        "Experiment Time Seconds",
        "Test Time Seconds",
        "Temperature F"
    ]

    # --------------------------------------------------------
    # CORRECTION HEADERS
    # --------------------------------------------------------

    correction_headers = [
        "Test Number",
        "Experiment Time Seconds",
        "Temperature F",
        "Initial Prediction Seconds",
        "Corrected Prediction Seconds",
        "Prediction Change Percent",
        "Actual Time Seconds",
        "Initial Error Percent",
        "Final Error Percent"
    ]

    # --------------------------------------------------------
    # CREATE INITIAL MODEL FILES
    # --------------------------------------------------------

    for model_time in MODEL_TIMES:

        file = INITIAL_FILES[model_time]

        if not os.path.exists(file):

            with open(
                file,
                "w",
                newline=""
            ) as f:

                writer = csv.writer(f)

                writer.writerow(
                    initial_headers
                )

    # --------------------------------------------------------
    # CREATE CORRECTION FILES
    # --------------------------------------------------------

    for model_time in MODEL_TIMES:

        file = CORRECTION_FILES[model_time]

        if not os.path.exists(file):

            with open(
                file,
                "w",
                newline=""
            ) as f:

                writer = csv.writer(f)

                writer.writerow(
                    correction_headers
                )


# ============================================================
# LOGGER
# ============================================================

def main():

    ser = connect_esp32()

    create_files()

    print()
    print("LOGGER READY")
    print()


    # ========================================================
    # TEST VARIABLES
    # ========================================================

    test_number = 0

    experiment_start = None

    sample_number = 0

    actual_time = None

    # --------------------------------------------------------
    # Initial predictions for each model time
    # --------------------------------------------------------

    initial_predictions = {
        30: None,
        45: None,
        60: None
    }

    # --------------------------------------------------------
    # Most recent corrected prediction for each model time
    # --------------------------------------------------------

    last_corrected_predictions = {
        30: None,
        45: None,
        60: None
    }


    # ========================================================
    # SERIAL LOOP
    # ========================================================

    try:

        while True:

            line = (
                ser.readline()
                .decode(errors="ignore")
                .strip()
            )

            if not line:
                continue

            print(line)


            # =================================================
            # NEW TEST
            # =================================================

            if line == "NEW PREDICTION TEST START":

                test_number = get_next_test_number()

                experiment_start = time.time()

                sample_number = 0

                actual_time = None

                initial_predictions = {
                    30: None,
                    45: None,
                    60: None
                }

                last_corrected_predictions = {
                    30: None,
                    45: None,
                    60: None
                }

                print()
                print("==============================")
                print("NEW TEST STARTED")
                print("TEST:", test_number)
                print("==============================")
                print()

                continue


            # =================================================
            # IGNORE EVERYTHING UNTIL TEST STARTS
            # =================================================

            if experiment_start is None:

                continue


            # =================================================
            # SAMPLE
            # =================================================

            if line.startswith("SAMPLE,"):

                values = line.split(",")

                try:

                    # Expected format:
                    #
                    # SAMPLE,
                    # experiment_time,
                    # temperature

                    sample_time = float(
                        values[1]
                    )

                    temperature = float(
                        values[2]
                    )

                    sample_number += 1

                    # ------------------------------------------------
                    # Determine integer test-time second.
                    #
                    # Examples:
                    #
                    # 0.48 -> 0
                    # 1.01 -> 1
                    # 1.48 -> 1
                    # 2.01 -> 2
                    #
                    # This matches the existing data structure.
                    # ------------------------------------------------

                    test_time_seconds = int(
                        sample_time
                    )

                    # ------------------------------------------------
                    # Save raw sample to ALL initial-model files.
                    #
                    # The raw data is the same underlying experiment
                    # data for the 30, 45, and 60 second models.
                    # ------------------------------------------------

                    for model_time in MODEL_TIMES:

                        with open(
                            INITIAL_FILES[model_time],
                            "a",
                            newline=""
                        ) as f:

                            writer = csv.writer(f)

                            writer.writerow([
                                test_number,
                                sample_number,
                                sample_time,
                                test_time_seconds,
                                temperature
                            ])

                except (
                    ValueError,
                    IndexError
                ):

                    print(
                        "ERROR READING SAMPLE:",
                        line
                    )


            # =================================================
            # INITIAL MODEL
            # =================================================

            if line.startswith("INITIAL_MODEL,"):

                values = line.split(",")

                try:

                    # Expected format:
                    #
                    # INITIAL_MODEL,
                    # experiment_time,
                    # temperature,
                    # model_time,
                    # prediction

                    experiment_time_from_esp = float(
                        values[1]
                    )

                    temperature = float(
                        values[2]
                    )

                    model_time = int(
                        values[3]
                    )

                    prediction = float(
                        values[4]
                    )

                    # ------------------------------------------------
                    # Only store the prediction in memory.
                    #
                    # DO NOT write it into the raw initial-model CSV.
                    # ------------------------------------------------

                    if model_time in MODEL_TIMES:

                        initial_predictions[
                            model_time
                        ] = prediction

                        print(
                            "INITIAL PREDICTION STORED:",
                            model_time,
                            "seconds =",
                            prediction,
                            "seconds"
                        )

                except (
                    ValueError,
                    IndexError
                ):

                    print(
                        "ERROR READING INITIAL MODEL:",
                        line
                    )


            # =================================================
            # CORRECTION / ADAPTIVE UPDATE
            # =================================================

            if line.startswith("CORRECTION_UPDATE,"):

                values = line.split(",")

                try:

                    # Expected format:
                    #
                    # CORRECTION_UPDATE,
                    # experiment_time,
                    # temperature,
                    # model_time,
                    # corrected_prediction

                    experiment_time_from_esp = float(
                        values[1]
                    )

                    temperature = float(
                        values[2]
                    )

                    model_time = int(
                        values[3]
                    )

                    corrected_prediction = float(
                        values[4]
                    )

                    # ------------------------------------------------
                    # Make sure this is one of our active model times.
                    # ------------------------------------------------

                    if model_time not in MODEL_TIMES:

                        continue

                    initial_prediction = (
                        initial_predictions[
                            model_time
                        ]
                    )

                    # ------------------------------------------------
                    # Store most recent corrected prediction.
                    # ------------------------------------------------

                    last_corrected_predictions[
                        model_time
                    ] = corrected_prediction

                    # ------------------------------------------------
                    # Calculate prediction change.
                    # ------------------------------------------------

                    change_percent = (
                        prediction_change_percent(
                            initial_prediction,
                            corrected_prediction
                        )
                    )

                    # ------------------------------------------------
                    # Correction data is only written beginning
                    # when the adaptive model starts sending updates.
                    # ------------------------------------------------

                    with open(
                        CORRECTION_FILES[model_time],
                        "a",
                        newline=""
                    ) as f:

                        writer = csv.writer(f)

                        writer.writerow([
                            test_number,
                            experiment_time_from_esp,
                            temperature,
                            initial_prediction,
                            corrected_prediction,
                            change_percent,
                            "",
                            "",
                            ""
                        ])

                except (
                    ValueError,
                    IndexError
                ):

                    print(
                        "ERROR READING CORRECTION UPDATE:",
                        line
                    )


            # =================================================
            # TEST COMPLETE
            # =================================================

            if line.startswith(
                "ACTUAL_TIME_SECONDS:"
            ):

                try:

                    actual_time = float(
                        line.replace(
                            "ACTUAL_TIME_SECONDS:",
                            ""
                        ).strip()
                    )

                except ValueError:

                    print(
                        "ERROR READING ACTUAL TIME:",
                        line
                    )

                    continue


                # ------------------------------------------------
                # Use the host computer's current elapsed time
                # for the experiment-time value of the final row.
                #
                # This keeps the final row aligned with the moment
                # the ESP32 reports the actual completion time.
                # ------------------------------------------------

                final_experiment_time = round(
                    time.time()
                    - experiment_start,
                    2
                )


                # =================================================
                # SAVE FINAL RESULT FOR EACH MODEL
                # =================================================

                for model_time in MODEL_TIMES:

                    initial_prediction = (
                        initial_predictions[
                            model_time
                        ]
                    )

                    corrected_prediction = (
                        last_corrected_predictions[
                            model_time
                        ]
                    )

                    # ------------------------------------------------
                    # If no correction update was received, use the
                    # initial prediction as the final prediction.
                    # ------------------------------------------------

                    if corrected_prediction is None:

                        corrected_prediction = (
                            initial_prediction
                        )

                    initial_error = (
                        percent_error(
                            initial_prediction,
                            actual_time
                        )
                    )

                    final_error = (
                        percent_error(
                            corrected_prediction,
                            actual_time
                        )
                    )

                    final_change = (
                        prediction_change_percent(
                            initial_prediction,
                            corrected_prediction
                        )
                    )

                    # ------------------------------------------------
                    # Get the last temperature recorded by the
                    # correction system if possible.
                    #
                    # The normal correction rows already contain
                    # temperature. The final row intentionally leaves
                    # temperature blank, matching your example.
                    # ------------------------------------------------

                    with open(
                        CORRECTION_FILES[model_time],
                        "a",
                        newline=""
                    ) as f:

                        writer = csv.writer(f)

                        writer.writerow([
                            test_number,
                            final_experiment_time,
                            "",
                            initial_prediction,
                            corrected_prediction,
                            final_change,
                            actual_time,
                            initial_error,
                            final_error
                        ])


                # =================================================
                # TEST SUMMARY
                # =================================================

                print()
                print("==============================")
                print("TEST SAVED")
                print("TEST:", test_number)
                print("ACTUAL TIME:", actual_time)
                print("==============================")
                print()


                # =================================================
                # RESET FOR NEXT TEST
                # =================================================

                experiment_start = None

                sample_number = 0

                actual_time = None

                initial_predictions = {
                    30: None,
                    45: None,
                    60: None
                }

                last_corrected_predictions = {
                    30: None,
                    45: None,
                    60: None
                }

                print("LOGGER COMPLETE")
                print("Program exiting.")
                print()

                return


    # ========================================================
    # STOP LOGGER
    # ========================================================

    except KeyboardInterrupt:

        print()
        print("LOGGER STOPPED BY USER")


    finally:

        if ser.is_open:

            ser.close()

        print("Serial connection closed.")


# ============================================================
# RUN LOGGER
# ============================================================

if __name__ == "__main__":

    main()