/*
 * Switch HD Rumble to DualSense PCM haptics output adapter.
 *
 * SPDX-License-Identifier: MIT
 */

#include "switch_rumble_adapter.h"

#include <algorithm>

#include "audio.h"
#include "effects_runtime.h"
#include "tusb.h"
#include "bt.h"
#include "config.h"
#include "pico/time.h"
#include "switch_haptics_synth.h"
#include "usb_mode.h"
#include "switch_bt.h"

namespace {

constexpr uint64_t HAPTICS_PACKET_INTERVAL_US =
    SWITCH_HAPTICS_PACKET_FRAMES * 1000000ull / SWITCH_HAPTICS_SAMPLE_RATE;
constexpr uint64_t RUMBLE_COMMAND_TIMEOUT_US = 500000;
constexpr uint8_t SILENCE_TAIL_PACKETS = 2;

SwitchHapticsSynth synth;
uint64_t next_packet_us = 0;
uint64_t last_command_us = 0;
uint8_t silence_tail_packets = 0;
bool transport_active = false;
SwitchRumbleState sequence[3]{};
uint8_t sequence_next = 3;
size_t sequence_frames_left = 0;
constexpr size_t SEQUENCE_FRAME_SAMPLES = SWITCH_HAPTICS_SAMPLE_RATE * 5 / 1000;

uint16_t haptics_gain_q8() {
    const float gain = std::clamp(get_config().haptics_gain, 1.0f, 2.0f);
    return static_cast<uint16_t>(gain * 256.0f + 0.5f);
}

} // namespace

void switch_rumble_adapter_apply(const SwitchRumbleState &rumble) {
    sequence_next = 3;
    if (!(usb_mode_is_switch() || switch_bt_enabled()) || !bt_is_connected() || (switch_bt_enabled() ? !switch_bt_connected() : (!tud_mounted() || tud_suspended()))) return;
    effects_rumble(std::max(rumble.left.low.amplitude, rumble.left.high.amplitude),
                   std::max(rumble.right.low.amplitude, rumble.right.high.amplitude));
    synth.set_state(rumble);
    last_command_us = time_us_64();
    if (!transport_active) next_packet_us = 0;
    transport_active = true;
    silence_tail_packets = rumble.silent() ? SILENCE_TAIL_PACKETS : 0;
}

void switch_rumble_adapter_apply_sequence(const SwitchRumbleState states[3]) {
    switch_rumble_adapter_apply(states[0]);
    if (!transport_active) return;
    std::copy_n(states,3,sequence);sequence_next=1;sequence_frames_left=SEQUENCE_FRAME_SAMPLES;
    // Remaining frames can be non-silent even when the first one is silent.
    silence_tail_packets=0;
}

void switch_rumble_adapter_stop() {
    sequence_next=3;sequence_frames_left=0;
    synth.reset();
    next_packet_us = 0;
    last_command_us = 0;
    silence_tail_packets = 0;
    transport_active = false;

    // Profile changes stop calling the Switch task immediately, so send one
    // explicit silent PCM packet before returning to the DualSense USB mode.
    if (bt_is_connected()) {
        int8_t silence[SWITCH_HAPTICS_PACKET_FRAMES * 2]{};
        audio_send_haptics_pcm(silence, SWITCH_HAPTICS_PACKET_FRAMES);
    }
}

void switch_rumble_adapter_task() {
    if (!(usb_mode_is_switch() || switch_bt_enabled()) || !bt_is_connected() || (switch_bt_enabled() ? !switch_bt_connected() : (!tud_mounted() || tud_suspended()))) {
        synth.reset();
        transport_active = false;
        silence_tail_packets = 0;
        next_packet_us = 0;
        last_command_us = 0;
        sequence_next=3;sequence_frames_left=0;
        return;
    }
    if (!transport_active) return;

    const uint64_t now = time_us_64();
    if (!synth.silent() && last_command_us != 0 &&
        now - last_command_us >= RUMBLE_COMMAND_TIMEOUT_US) {
        // A vanished USB host must not leave the actuators running forever.
        // Switch hosts normally refresh active rumble well inside 500 ms.
        synth.set_state({});
        silence_tail_packets = SILENCE_TAIL_PACKETS;
        last_command_us = 0;
        sequence_next=3;sequence_frames_left=0;
    }
    if (next_packet_us != 0 && now < next_packet_us) return;

    int8_t samples[SWITCH_HAPTICS_PACKET_FRAMES * 2];
    size_t rendered=0;
    while(rendered<SWITCH_HAPTICS_PACKET_FRAMES) {
        if(sequence_next<3 && sequence_frames_left==0) {
            synth.set_state(sequence[sequence_next++]);sequence_frames_left=SEQUENCE_FRAME_SAMPLES;
        }
        const size_t count=sequence_next<3?std::min(SWITCH_HAPTICS_PACKET_FRAMES-rendered,sequence_frames_left):SWITCH_HAPTICS_PACKET_FRAMES-rendered;
        synth.render(samples+rendered*2,count,haptics_gain_q8());rendered+=count;
        if(sequence_frames_left>=count)sequence_frames_left-=count;
    }
    audio_send_haptics_pcm(samples, SWITCH_HAPTICS_PACKET_FRAMES);

    if (synth.silent() && sequence_next>=3) {
        if (silence_tail_packets == 0) {
            transport_active = false;
            next_packet_us = 0;
            return;
        }
        --silence_tail_packets;
    }

    // Never burst old packets after a long stall; stale haptics feel worse
    // than dropping one block and resynchronizing to the current command.
    if (next_packet_us == 0 || now - next_packet_us > HAPTICS_PACKET_INTERVAL_US) {
        next_packet_us = now + HAPTICS_PACKET_INTERVAL_US;
    } else {
        next_packet_us += HAPTICS_PACKET_INTERVAL_US;
    }
}
