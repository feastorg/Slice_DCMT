// Host stand-in for the Arduino core: the calls the firmware makes, no more.
// Time, pins and the serial port are defined in host.cpp.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <string>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

unsigned long millis();
void noInterrupts();
void interrupts();
int digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t val);
void pinMode(uint8_t pin, uint8_t mode);
inline void analogWrite(uint8_t, int) {}
inline int analogRead(uint8_t) { return 0; }

template <typename T, typename L, typename H>
inline T constrain(T x, L lo, H hi) { return x < lo ? lo : (x > hi ? hi : x); }

#define F(s) (s)
#define PSTR(s) (s)
#define PROGMEM
typedef const char *PGM_P;
#define strncmp_P strncmp
#define strlen_P strlen
#define strcmp_P strcmp

// main.cpp includes <avr/wdt.h> only on AVR builds; host builds get it here.
#include <avr/wdt.h>

// Everything printed is appended to out, so a test can read the replies.
class Print
{
public:
    std::string out;
    void print(const char *s) { out += s; }
    void println(const char *s) { out += s; out += "\n"; }
    void println() { out += "\n"; }
    template <typename T> void print(T v) { out += std::to_string(v); }
    template <typename T> void println(T v) { out += std::to_string(v); out += "\n"; }
    template <typename T> void print(T v, int) { out += std::to_string(v); }
    template <typename T> void println(T v, int) { out += std::to_string(v); out += "\n"; }
};

// Bytes a test appends to in are what the firmware reads.
class HardwareSerial : public Print
{
public:
    std::string in;
    void begin(unsigned long) {}
    int available() { return (int)in.size(); }
    int read()
    {
        if (in.empty())
            return -1;
        int c = (unsigned char)in[0];
        in.erase(0, 1);
        return c;
    }
};
extern HardwareSerial Serial;

enum { A0 = 14, A1, A2, A3, A4, A5, A6, A7 };
