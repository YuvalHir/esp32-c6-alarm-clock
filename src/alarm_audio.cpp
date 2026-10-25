#include "alarm_audio.h"
#include <driver/i2s_std.h>
#include <math.h>

#define AUDIO_I2S_MCLK   GPIO_NUM_19
#define AUDIO_I2S_BCLK   GPIO_NUM_20
#define AUDIO_I2S_WS     GPIO_NUM_22
#define AUDIO_I2S_DOUT   GPIO_NUM_23
#define ES8311_I2C_ADDR  0x18
#define SAMPLE_RATE      16000

static i2c_master_dev_handle_t es8311_dev = NULL;
static I2cMasterBus *audio_i2c = NULL;
static i2s_chan_handle_t tx_handle = NULL;
static TaskHandle_t audio_task_handle = NULL;
static volatile bool is_playing_alarm = false;
static volatile bool is_playing_test = false;
static uint8_t current_vol_reg = 0xDF; // ~ +6dB default output
static bool is_audio_powered = false;

static void es8311_write(uint8_t reg, uint8_t val) {
    if (!audio_i2c || !es8311_dev) return;
    audio_i2c->i2c_write_buff(es8311_dev, reg, &val, 1);
}

static uint8_t es8311_read(uint8_t reg) {
    if (!audio_i2c || !es8311_dev) return 0;
    uint8_t val = 0;
    audio_i2c->i2c_read_buff(es8311_dev, reg, &val, 1);
    return val;
}

static void es8311_power_down() {
    if (!audio_i2c || !es8311_dev) return;
    es8311_write(0x31, 0x60); // Mute DAC
    es8311_write(0x12, 0x3F); // Power down DAC
    es8311_write(0x0E, 0xFF); // Power down analog circuits
    if (tx_handle && is_audio_powered) {
        i2s_channel_disable(tx_handle);
    }
    is_audio_powered = false;
    Serial.println("[Audio] ES8311 Codec & I2S placed in Low-Power Standby (0mA)");
}

static void es8311_power_up() {
    if (!audio_i2c || !es8311_dev) return;
    if (is_audio_powered) return;
    if (tx_handle) {
        i2s_channel_enable(tx_handle);
    }
    es8311_write(0x0E, 0x00); // Power up analog circuits
    es8311_write(0x12, 0x00); // Power up DAC
    es8311_write(0x14, 0x1A);
    es8311_write(0x0D, 0x01); // Power up state machine
    es8311_write(0x15, 0x40);
    es8311_write(0x32, current_vol_reg);
    es8311_write(0x31, 0x00); // Unmute DAC
    is_audio_powered = true;
    Serial.println("[Audio] ES8311 Codec & I2S Woken Up for playback");
}

static void es8311_init_regs() {
    Serial.println("[Audio] Initializing ES8311 Codec registers...");
    
    // Check chip ID
    uint8_t id1 = es8311_read(0xFD);
    uint8_t id2 = es8311_read(0xFE);
    Serial.printf("[Audio] ES8311 Chip ID read: 0x%02X 0x%02X\n", id1, id2);

    // Enhanced noise immunity write
    es8311_write(0x44, 0x08);
    es8311_write(0x44, 0x08);

    // Clocks
    es8311_write(0x01, 0x30); // Enable clock manager
    es8311_write(0x02, 0x00); // Pre-div=1, mult=1
    es8311_write(0x03, 0x10); // ADC OSR
    es8311_write(0x04, 0x10); // DAC OSR
    es8311_write(0x05, 0x00); // Div=1
    es8311_write(0x06, 0x03); // BCLK division = 4
    es8311_write(0x07, 0x00); // LRCK high
    es8311_write(0x08, 0xFF); // LRCK low

    // System power & slave mode
    es8311_write(0x0B, 0x00);
    es8311_write(0x0C, 0x00);
    es8311_write(0x10, 0x1F);
    es8311_write(0x11, 0x7F);
    es8311_write(0x00, 0x80); // Reset CSM
    es8311_write(0x00, 0x80); // Slave mode (bit 6 = 0)
    es8311_write(0x01, 0x3F); // Use MCLK
    es8311_write(0x13, 0x10);
    es8311_write(0x16, 0x24);
    es8311_write(0x1B, 0x0A);
    es8311_write(0x1C, 0x6A);
    es8311_write(0x44, 0x08);

    // Serial format: 16-bit I2S standard
    es8311_write(0x09, 0x0C); // DAC SDP 16-bit standard I2S
    es8311_write(0x0A, 0x0C); // ADC SDP 16-bit

    // Analog & DAC power-up
    es8311_write(0x0E, 0x00); // Power up analog circuits
    es8311_write(0x12, 0x00); // Power up DAC
    es8311_write(0x14, 0x1A);
    es8311_write(0x0D, 0x01); // Power up state machine
    es8311_write(0x15, 0x40);
    es8311_write(0x37, 0x08); // Ramp rate
    es8311_write(0x45, 0x00);

    // Volume & Unmute
    es8311_write(0x32, current_vol_reg);
    es8311_write(0x31, 0x00); // Unmute DAC
    Serial.println("[Audio] ES8311 Codec configured and unmuted!");
}

// Synthesize a bell-like tone with attack and exponential decay
static void play_tone(float freq_hz, int duration_ms, float peak_amp) {
    if (!tx_handle) return;
    int total_samples = (SAMPLE_RATE * duration_ms) / 1000;
    const int CHUNK = 256;
    int16_t buffer[CHUNK * 2]; // Stereo: Left & Right

    float phase = 0.0f;
    float phase_inc = (2.0f * M_PI * freq_hz) / (float)SAMPLE_RATE;
    float phase_harm = 0.0f;
    float phase_inc_harm = (2.0f * M_PI * freq_hz * 2.0f) / (float)SAMPLE_RATE;

    int samples_left = total_samples;
    int current_sample = 0;

    while (samples_left > 0 && (is_playing_alarm || is_playing_test)) {
        int chunk_size = (samples_left > CHUNK) ? CHUNK : samples_left;
        for (int i = 0; i < chunk_size; i++) {
            float t = (float)current_sample / (float)total_samples;
            // Bell envelope: fast attack (5%), exponential decay
            float env = 0.0f;
            if (t < 0.05f) {
                env = t / 0.05f;
            } else {
                env = expf(-3.5f * (t - 0.05f));
            }

            // Fundamental + warm 2nd harmonic
            float sample_val = sinf(phase) + 0.35f * sinf(phase_harm);
            int16_t out = (int16_t)(sample_val * env * peak_amp * 32767.0f * 0.7f);

            buffer[i * 2]     = out; // Left channel
            buffer[i * 2 + 1] = out; // Right channel

            phase += phase_inc;
            if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
            phase_harm += phase_inc_harm;
            if (phase_harm >= 2.0f * M_PI) phase_harm -= 2.0f * M_PI;
            current_sample++;
        }

        size_t bytes_written = 0;
        i2s_channel_write(tx_handle, buffer, chunk_size * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
        samples_left -= chunk_size;
    }
}

static void audio_task(void *arg) {
    // Melody notes for morning chime: E5, G#5, B5, E6
    const float notes[] = { 659.25f, 830.61f, 987.77f, 1318.51f };
    const int durs[]   = { 220,     220,     250,     650 };

    while (1) {
        if (is_playing_alarm) {
            es8311_power_up();
            // Play melodic ascending wake-up chime
            for (int i = 0; i < 4; i++) {
                if (!is_playing_alarm) break;
                play_tone(notes[i], durs[i], 0.85f);
                vTaskDelay(pdMS_TO_TICKS(40));
            }
            // Pause 1.2s between chime phrases
            for (int p = 0; p < 12; p++) {
                if (!is_playing_alarm) break;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        } 
        else if (is_playing_test) {
            es8311_power_up();
            // Quick 2-phrase preview
            for (int i = 0; i < 4; i++) {
                play_tone(notes[i], durs[i], 0.85f);
                vTaskDelay(pdMS_TO_TICKS(30));
            }
            vTaskDelay(pdMS_TO_TICKS(300));
            for (int i = 0; i < 4; i++) {
                play_tone(notes[i], durs[i], 0.85f);
                vTaskDelay(pdMS_TO_TICKS(30));
            }
            is_playing_test = false;
            es8311_power_down();
        } 
        else {
            if (is_audio_powered) {
                es8311_power_down();
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

void alarm_audio_init(I2cMasterBus &i2c_bus) {
    audio_i2c = &i2c_bus;

    // Register ES8311 on I2C bus
    i2c_master_bus_handle_t bus_handle = audio_i2c->Get_I2cBusHandle();
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = ES8311_I2C_ADDR;
    dev_cfg.scl_speed_hz    = 100000;
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &es8311_dev);
    if (err == ESP_OK) {
        Serial.println("[Audio] ES8311 I2C device attached successfully");
        es8311_init_regs();
    } else {
        Serial.printf("[Audio Error] Failed to attach ES8311 on I2C: %d\n", err);
    }

    // Initialize I2S Master Channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear_after_cb = true;
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 256;
    err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (err != ESP_OK) {
        Serial.printf("[Audio Error] i2s_new_channel failed: %d\n", err);
        return;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_MCLK,
            .bclk = AUDIO_I2S_BCLK,
            .ws = AUDIO_I2S_WS,
            .dout = AUDIO_I2S_DOUT,
            .din = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (err != ESP_OK) {
        Serial.printf("[Audio Error] i2s_channel_init_std_mode failed: %d\n", err);
        return;
    }

    err = i2s_channel_enable(tx_handle);
    if (err == ESP_OK) {
        Serial.println("[Audio] I2S TX channel enabled successfully (16kHz stereo)");
        is_audio_powered = true;
        // Place immediately in low-power standby (0mA) until sound is needed
        es8311_power_down();
    } else {
        Serial.printf("[Audio Error] i2s_channel_enable failed: %d\n", err);
        return;
    }

    // Create FreeRTOS Background Task for Sound
    xTaskCreate(audio_task, "audio_chime_task", 3072, NULL, 1, &audio_task_handle);
    Serial.println("[Audio] Audio Chime task created successfully!");
}

void alarm_audio_start() {
    is_playing_alarm = true;
    is_playing_test = false;
    Serial.println("[Audio] Alarm Chime STARTED");
}

void alarm_audio_stop() {
    is_playing_alarm = false;
    is_playing_test = false;
    es8311_power_down();
    Serial.println("[Audio] Alarm Chime STOPPED");
}

void alarm_audio_play_test() {
    uint8_t id1 = es8311_read(0xFD);
    uint8_t id2 = es8311_read(0xFE);
    uint8_t v31 = es8311_read(0x31);
    uint8_t v32 = es8311_read(0x32);
    Serial.printf("[Audio] Test Chime: ES8311 Chip ID=0x%02X%02X, Reg31(Mute)=0x%02X, Reg32(Vol)=0x%02X\n", id1, id2, v31, v32);
    is_playing_alarm = false;
    is_playing_test = true;
}

bool alarm_audio_is_playing() {
    return is_playing_alarm || is_playing_test;
}

void alarm_audio_set_volume(uint8_t volume) {
    if (volume > 100) volume = 100;
    // Map 0..100 to ES8311 DAC volume 0x00..0xFF
    current_vol_reg = (uint8_t)((volume * 255) / 100);
    es8311_write(0x32, current_vol_reg);
}
