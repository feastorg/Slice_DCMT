// Bench check for lmd18200 readCurrent() (#2): hold motor 1 of a gen2 DCMT at
// fixed PWM steps and print the averaged readCurrent() at each, for comparison
// with a DC ammeter in series with the motor. Not shipped anywhere.
//
// Steps: 0, 64, 128, 192, 255, each held 6 s, then brake. 255 is DC drive, so
// the ammeter and the sense output agree best there; below 255 both average a
// PWM waveform differently. Type "s" and Enter to brake at any time.
#include <Arduino.h>
#include <LMD18200.h>

static const uint8_t PWM_PIN = 6, DIR_PIN = 7, BRAKE_PIN = 8, CSENSE_PIN = A6, TFLAG_PIN = 9;
static const float SENSE_OHMS = 4420.0f;
static const int STEPS[] = {0, 64, 128, 192, 255};
static const unsigned long HOLD_MS = 6000;

static LMD18200 motor(PWM_PIN, DIR_PIN, BRAKE_PIN, CSENSE_PIN, TFLAG_PIN);
static int step = 0;
static bool stopped = false;
static unsigned long stepStart = 0, lastPrint = 0;

void setup()
{
    Serial.begin(115200);
    motor.begin();
    motor.write(0);
    Serial.println(F("pwm,amps_avg32,thermal"));
    stepStart = millis();
}

void loop()
{
    if (Serial.available() && Serial.read() == 's')
    {
        motor.brake();
        stopped = true;
        Serial.println(F("braked"));
    }
    if (stopped)
        return;

    if (millis() - stepStart >= HOLD_MS)
    {
        step++;
        stepStart = millis();
        if (step >= (int)(sizeof STEPS / sizeof STEPS[0]))
        {
            motor.brake();
            stopped = true;
            Serial.println(F("done, braked"));
            return;
        }
    }
    motor.write(STEPS[step]);

    if (millis() - lastPrint >= 500)
    {
        lastPrint = millis();
        float sum = 0;
        for (int i = 0; i < 32; i++)
            sum += motor.readCurrent(SENSE_OHMS);
        Serial.print(STEPS[step]);
        Serial.print(',');
        Serial.print(sum / 32, 3);
        Serial.print(',');
        Serial.println(motor.isThermalWarning() ? 1 : 0);
    }
}
