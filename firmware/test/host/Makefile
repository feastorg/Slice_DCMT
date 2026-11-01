# Host tests: the real firmware sources (firmware/src) built with the host
# compiler against the real CRUMBS core, the contracts headers and
# bread-slice-core, with the Arduino core and the motor libraries stubbed
# (stubs/). The motor tests swap the motor-library stubs for the real
# DCMotorServo, DCMotorTacho and PID_v1 and drive a modelled motor
# (motor_sim.h). See README.md.
#
#   make            build and run every test: the stub tests for all four
#                   envs, then the motor tests
#   make test       the stub tests only
#   make motor      the motor tests only
#   make LIBDEPS=…  take CRUMBS, the contracts, the core and the motor
#                   libraries from another libdeps dir
#   make motor DCMS=… OUT=…  take DCMotorServo's src/ from another
#                   checkout, built apart from the default build
#
# The libraries come from PlatformIO's libdeps, so the tests use the
# versions the firmware builds with. Run `pio pkg install -e gen2_nano` in
# firmware/ first.

FW      ?= ../..
LIBDEPS ?= $(FW)/.pio/libdeps/gen2_nano
CRUMBS  := $(LIBDEPS)/CRUMBS/src
CONTRACTS := $(LIBDEPS)/bread-crumbs-contracts/include
CORE    := $(LIBDEPS)/bread-slice-core/src
DCMS    ?= $(LIBDEPS)/DCMotorServo/src
PID     := $(LIBDEPS)/PID
OUT     ?= build

CC      ?= cc
CXX     ?= c++
CFLAGS  := -std=c11 -O1 -g -DCRUMBS_MAX_HANDLERS=12
CXXFLAGS := -std=c++17 -O1 -g -DARDUINO=100 -DCRUMBS_MAX_HANDLERS=12 -DBSC_HOST_TEST

# One build per PlatformIO env: the same DCMT_HW_GEN and MCU macros, so the
# pin map, LED and speed-loop selection follow the same #if paths.
ENVS := gen1_nano gen2_nano gen1_nanoevery gen2_nanoevery
FLAGS_gen1_nano      := -DDCMT_HW_GEN=1 -D__AVR_ATmega328P__
FLAGS_gen2_nano      := -DDCMT_HW_GEN=2 -D__AVR_ATmega328P__
FLAGS_gen1_nanoevery := -DDCMT_HW_GEN=1 -D__AVR_ATmega4809__ -DARDUINO_ARCH_MEGAAVR
FLAGS_gen2_nanoevery := -DDCMT_HW_GEN=2 -D__AVR_ATmega4809__ -DARDUINO_ARCH_MEGAAVR

INCLUDES := -Istubs -I. -I$(FW)/include -I$(CRUMBS) -I$(CRUMBS)/crc -I$(CONTRACTS) -I$(CORE)
FW_SRC   := $(wildcard $(FW)/src/*.cpp) $(wildcard $(CORE)/*.cpp)
CRUMBS_SRC := $(CRUMBS)/core/crumbs_core.c $(CRUMBS)/crc/crumbs_crc.c $(CRUMBS)/crc/crc8_nibble.c
CRUMBS_OBJ := $(OUT)/crumbs_core.o $(OUT)/crumbs_crc.o $(OUT)/crc8_nibble.o
# test_status_led runs once per e-stop case (the test target); the others
# run once.
SINGLE   := test_dispatch test_mode_pwm test_applied_pwm test_serial_burst test_max_lead test_estop_hold test_watchdog_trip test_serial_numbers test_setpoint_loop test_watchdog_lease test_stall_fault test_hold_brakes test_speed_freeze
TESTS    := $(SINGLE) test_status_led

.PHONY: all test motor clean check-libdeps check-motor-libdeps FORCE
all: test motor

check-libdeps:
	@test -f $(CRUMBS)/crumbs.h || { echo "CRUMBS not found at $(CRUMBS): run 'pio pkg install -e gen2_nano' in firmware/, or set LIBDEPS" >&2; exit 1; }
	@test -d $(CONTRACTS)/bread || { echo "contracts not found at $(CONTRACTS)" >&2; exit 1; }
	@test -f $(CORE)/bread_slice_core.h || { echo "bread-slice-core not found at $(CORE)" >&2; exit 1; }

check-motor-libdeps: check-libdeps
	@test -f $(DCMS)/DCMotorTacho.cpp || { echo "DCMotorServo not found at $(DCMS)" >&2; exit 1; }
	@test -f $(PID)/PID_v1.cpp || { echo "PID not found at $(PID)" >&2; exit 1; }

# pio pkg install keeps a release archive's mtimes, so a library installed
# by a pin change can look older than what was built from the last one. The
# stamp holds the installed versions and is rewritten only when one changes,
# so everything built from libdeps depends on it.
STAMP := $(OUT)/libdeps.stamp
$(STAMP): FORCE | check-libdeps
	@mkdir -p $(OUT)
	@for f in "$(LIBDEPS)"/*/.piopm; do if [ -f "$$f" ]; then cat "$$f"; fi; done > $@.new
	@if cmp -s $@.new $@; then rm $@.new; else mv $@.new $@; fi

$(OUT)/%.o: $(CRUMBS)/core/%.c $(STAMP) | check-libdeps
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I$(CRUMBS) -I$(CRUMBS)/crc -c $< -o $@
$(OUT)/%.o: $(CRUMBS)/crc/%.c $(STAMP) | check-libdeps
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I$(CRUMBS) -I$(CRUMBS)/crc -c $< -o $@

# $(1) env, $(2) test
define TEST_template
$(OUT)/$(1)/$(2): $(2).cpp host.cpp host.h $(FW_SRC) $(wildcard $(FW)/include/*.h) $(wildcard stubs/*.h stubs/avr/*.h) $(CRUMBS_OBJ) $(STAMP)
	@mkdir -p $(OUT)/$(1)
	$(CXX) $(CXXFLAGS) $(FLAGS_$(1)) $(INCLUDES) $(2).cpp host.cpp $(FW_SRC) $(CRUMBS_OBJ) -o $$@
endef
$(foreach e,$(ENVS),$(foreach t,$(TESTS),$(eval $(call TEST_template,$(e),$(t)))))

BINS := $(foreach e,$(ENVS),$(foreach t,$(TESTS),$(OUT)/$(e)/$(t)))

# Runs every binary, prints each result line, and fails if any test failed.
test: $(BINS)
	@status=0; \
	for e in $(ENVS); do \
	  for t in $(SINGLE); do \
	    echo "== $$e: $$t"; $(OUT)/$$e/$$t || status=1; \
	  done; \
	  for line in released held; do \
	    echo "== $$e: test_status_led $$line"; $(OUT)/$$e/test_status_led $$line || status=1; \
	  done; \
	done; \
	exit $$status

clean:
	rm -rf $(OUT)

# ---- Motor tests ----
# The real DCMotorServo, DCMotorTacho and PID_v1 in place of the stubs: their
# directories come before stubs/ on the include path, so main.cpp's
# <DCMotorServo.h> and <DCMotorTacho.h> are the library's. The Arduino core,
# LMD18200 and Encoder stay stubbed; motor_sim.cpp models the motor between
# them. Built for the envs that build the speed loop (DCMT_ENABLE_SPEED_LOOP
# defaults to 1 on the Nano Every only).
MOTOR_ENVS  := gen1_nanoevery gen2_nanoevery
# test_speed_entry_62 and test_stall_fault_64 run once per case (the motor
# target); the others run once.
MOTOR_SINGLE := test_speed_baseline_90 test_stall_release_89
MOTOR_TESTS := $(MOTOR_SINGLE) test_speed_entry_62 test_stall_fault_64
MOTOR_INCLUDES := -I$(DCMS) -I$(PID) $(INCLUDES)
MOTOR_SRC   := motor_sim.cpp $(wildcard $(DCMS)/*.cpp) $(PID)/PID_v1.cpp

# $(1) env, $(2) test
define MOTOR_template
$(OUT)/$(1)/$(2): $(2).cpp host.cpp host.h motor_sim.cpp motor_sim.h $(FW_SRC) $(MOTOR_SRC) $(wildcard $(DCMS)/*.h) $(wildcard $(FW)/include/*.h) $(wildcard stubs/*.h stubs/avr/*.h) $(CRUMBS_OBJ) $(STAMP) | check-motor-libdeps
	@mkdir -p $(OUT)/$(1)
	$(CXX) $(CXXFLAGS) $(FLAGS_$(1)) $(MOTOR_INCLUDES) $(2).cpp host.cpp $(FW_SRC) $(MOTOR_SRC) $(CRUMBS_OBJ) -o $$@
endef
$(foreach e,$(MOTOR_ENVS),$(foreach t,$(MOTOR_TESTS),$(eval $(call MOTOR_template,$(e),$(t)))))

MOTOR_BINS := $(foreach e,$(MOTOR_ENVS),$(foreach t,$(MOTOR_TESTS),$(OUT)/$(e)/$(t)))

motor: $(MOTOR_BINS)
	@status=0; \
	for e in $(MOTOR_ENVS); do \
	  for t in $(MOTOR_SINGLE); do \
	    echo "== $$e: $$t"; $(OUT)/$$e/$$t || status=1; \
	  done; \
	  for c in reverse forward; do \
	    echo "== $$e: test_speed_entry_62 $$c"; $(OUT)/$$e/test_speed_entry_62 $$c || status=1; \
	  done; \
	  for c in speed_held position_rest position_held speed_10; do \
	    echo "== $$e: test_stall_fault_64 $$c"; $(OUT)/$$e/test_stall_fault_64 $$c || status=1; \
	  done; \
	done; \
	exit $$status
