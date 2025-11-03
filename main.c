#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include <stdio.h>
#include <string.h>

// This header is generated automatically by the build system from our .pio file.
#include "dac_trigger.pio.h"

// --- Configuration ---
#define ADC_CHANNEL 1 // GPIO 27
#define RING_BUFFER_SIZE 256
#define POLARIZATION_PIN 26
#define READ_WRITE_PIN   28

// --- Function Prototypes ---
void pio_irq_handler();
void build_dac_lut();
void excitation_pulse(int frequency, int interval_ms);
void data_read(int frequency, int interval_ms);
void handle_incoming_commands();
void handle_nmr_command(int pulse_ms, int read_ms);

// --- Global Variables ---
const uint8_t sin_lut_16[16] = {
    128, 177, 217, 244, 255, 244, 217, 177,
    128,  79,  39,  12,   1,  12,  39,  79
};
const uint B0 = 29, B1 = 6, B2 = 7, B3 = 0, B4 = 1, B5 = 2, B6 = 4, B7 = 3;
const uint32_t dac_mask = (1u << B0) | (1u << B1) | (1u << B2) | (1u << B3) |
                          (1u << B4) | (1u << B5) | (1u << B6) | (1u << B7);
uint32_t dac_lut[256];

// Ring Buffer for ADC data
uint16_t sample_buffer[RING_BUFFER_SIZE];
volatile uint32_t write_index = 0;
volatile uint32_t read_index = 0;

// State variables for the PIO task
volatile uint32_t length_pulse;
volatile uint32_t trigger_count;
volatile bool pulse_ended_flag = true;
volatile bool is_read_mode = false;

// PIO globals
PIO pio = pio0;
uint sm = 0;
uint32_t pio_delay_cycles = 0;


// --- Main Program ---
int main() {
    stdio_init_all();
    // A longer sleep allows time to connect a serial monitor after flashing
    sleep_ms(4000);
    printf("NMR Controller Ready. Waiting for commands...\n");

    // --- One-Time Setup ---
    build_dac_lut();
    gpio_init_mask(dac_mask);
    gpio_set_dir_out_masked(dac_mask);
    
    // Setup for relay pins
    gpio_init(POLARIZATION_PIN);
    gpio_set_dir(POLARIZATION_PIN, GPIO_OUT);
    gpio_init(READ_WRITE_PIN);
    gpio_set_dir(READ_WRITE_PIN, GPIO_OUT);
    // Ensure relays are in a safe default state
    gpio_put(POLARIZATION_PIN, 0);
    gpio_put(READ_WRITE_PIN, 0);

    // Setup for ADC
    adc_init();
    adc_gpio_init(26 + ADC_CHANNEL);
    adc_select_input(ADC_CHANNEL);

    // --- PIO & Interrupt Setup ---
    uint offset = pio_add_program(pio, &dac_trigger_program);
    pio_sm_claim(pio, sm);
    pio_sm_config c = dac_trigger_program_get_default_config(offset);
    pio_sm_init(pio, sm, offset, &c);
    pio_set_irq0_source_enabled(pio, pis_interrupt0, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio_irq_handler);
    irq_set_enabled(PIO0_IRQ_0, true);
    pio_sm_set_enabled(pio, sm, true);

    // The main loop now only listens for commands.
    while (1) {
        handle_incoming_commands();
    }
    return 0;
}


// --- Command Handling ---
void handle_incoming_commands() {
    char cmd_buffer[64];
    uint char_idx = 0;
    
    int c = getchar_timeout_us(0); // Non-blocking read from USB

    while (c != PICO_ERROR_TIMEOUT && char_idx < sizeof(cmd_buffer) - 1) {
        if (c == '\n' || c == '\r') {
            cmd_buffer[char_idx] = '\0'; // Null-terminate the string
            
            int pulse_ms, read_ms;
            // Use sscanf to parse "NMR(pulse,read)" format
            if (sscanf(cmd_buffer, "NMR(%d,%d)", &pulse_ms, &read_ms) == 2) {
                printf("COMMAND RECEIVED: Pulse=%dms, Read=%dms\n", pulse_ms, read_ms);
                handle_nmr_command(pulse_ms, read_ms);
                // After sequence, let the user know we're ready again
                printf("NMR Controller Ready. Waiting for commands...\n");
            } else {
                printf("ERROR: Invalid command format. Use: NMR(pulse_ms,read_ms)\n");
            }
            return; // Exit after processing one command
        } else {
            cmd_buffer[char_idx++] = c;
        }
        c = getchar_timeout_us(0);
    }
}

// This function executes the entire NMR sequence. It is a blocking function.
void handle_nmr_command(int pulse_ms, int read_ms) {
    // 1. POLARIZATION STAGE
    printf("STATUS: Polarizing coil ON for 5 seconds...\n");
    gpio_put(POLARIZATION_PIN, 1);
    sleep_ms(5000);
    gpio_put(POLARIZATION_PIN, 0);
    printf("STATUS: Polarization complete.\n");
    
    // 2. EXCITATION PULSE STAGE
    printf("STATUS: Setting relay to WRITE mode...\n");
    gpio_put(READ_WRITE_PIN, 1); // 1 = Write/Pulse
    sleep_ms(10); // Small delay to allow the relay to physically switch
    
    printf("STATUS: Starting excitation pulse...\n");
    excitation_pulse(1701, pulse_ms);
    // Wait for the pulse to finish
    while(!pulse_ended_flag) {}
    printf("STATUS: Pulse finished.\n");

    // 3. DATA READ STAGE
    printf("STATUS: Setting relay to READ mode...\n");
    gpio_put(READ_WRITE_PIN, 0); // 0 = Read
    sleep_ms(10); // Small delay for relay
    
    printf("STATUS: Starting data read at 10kSPS...\n");
    data_read(10000, read_ms);
    uint16_t start_seq = 0xFFFF;
    fwrite(&start_seq, sizeof(uint16_t), 1, stdout);
    // Wait for the read to complete, streaming data from the ring buffer as we go.
    while(!pulse_ended_flag) {
        if (write_index != read_index) {
            fwrite(&sample_buffer[read_index], sizeof(uint16_t), 1, stdout);
            read_index = (read_index + 1) % RING_BUFFER_SIZE;
        }
    }
    // After the read finishes, drain any leftover data from the buffer
    while(write_index != read_index) {
        fwrite(&sample_buffer[read_index], sizeof(uint16_t), 1, stdout);
        read_index = (read_index + 1) % RING_BUFFER_SIZE;
    }
    fwrite(&start_seq, sizeof(uint16_t), 1, stdout);
    printf("STATUS: Data read complete. Sequence finished.\n");
}


// --- PIO Interrupt Handler ---
// This is the heart of the real-time operation.
// REWRITTEN: The robust ISR with the new synchronization bit logic.
void pio_irq_handler() {
    // --- Timing-Critical Section ---
    if (trigger_count >= length_pulse - 1) {
         pulse_ended_flag = true;
    } else {
         pio_sm_put(pio, sm, pio_delay_cycles);
    }

    // --- Action Section ---
    if (is_read_mode) {
        // Read the 12-bit sample from the ADC
        uint16_t sample = adc_read();

        // Place the (potentially marked) sample in the buffer
        sample_buffer[write_index] = sample;
        write_index = (write_index + 1) % RING_BUFFER_SIZE;
    } else {
        // DAC write logic is unchanged
        gpio_put_masked(dac_mask, dac_lut[sin_lut_16[trigger_count % 16]]);
    }
    
    trigger_count++;
    
    pio_interrupt_clear(pio, 0);
}

// --- Helper Functions ---
void data_read(int frequency, int interval_ms) {
    if (!pulse_ended_flag) return;

    pio_delay_cycles = ((float)clock_get_hz(clk_sys) / frequency) - 5;
    float sample_period_us = 1000000.0f / frequency;
    length_pulse = (uint32_t)(interval_ms * 1000.0f / sample_period_us);

    trigger_count = 0;
    pulse_ended_flag = false;
    is_read_mode = true;
    
    pio_sm_put(pio, sm, pio_delay_cycles);
}

void excitation_pulse(int frequency, int interval_ms) {
    if (!pulse_ended_flag) return;

    pio_delay_cycles = ((float)clock_get_hz(clk_sys) / (frequency * 16.0f)) - 5;
    float sample_period_us = 1000000.0f / (frequency * 16.0f);
    length_pulse = (uint32_t)(interval_ms * 1000.0f / sample_period_us);

    trigger_count = 0;
    pulse_ended_flag = false;
    is_read_mode = false;
    
    pio_sm_put(pio, sm, pio_delay_cycles);
}

void build_dac_lut() {
    for (int i = 0; i < 256; i++) {
        dac_lut[i] =
            (((i >> 0) & 1u) << B0) | (((i >> 1) & 1u) << B1) |
            (((i >> 2) & 1u) << B2) | (((i >> 3) & 1u) << B3) |
            (((i >> 4) & 1u) << B4) | (((i >> 5) & 1u) << B5) |
            (((i >> 6) & 1u) << B6) | (((i >> 7) & 1u) << B7);
    }
}
