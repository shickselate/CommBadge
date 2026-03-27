#include <driver/i2s.h>

// INMP441 pins — match your wiring
#define I2S_WS  8
#define I2S_SD  9
#define I2S_SCK 10

#define I2S_PORT I2S_NUM_0
#define bufferLen 64

int32_t sBuffer[bufferLen];

void i2s_install() {
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = i2s_bits_per_sample_t(32),
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = bufferLen,
    .use_apll = false
  };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
}

void i2s_setpin() {
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };
  i2s_set_pin(I2S_PORT, &pin_config);
}

void setup() {
  Serial.begin(115200);
  i2s_install();
  i2s_setpin();
  i2s_start(I2S_PORT);
  delay(500);
}

void loop() {
  int rangelimit = 15000;
  Serial.print(rangelimit * -1);
  Serial.print(" ");
  Serial.print(rangelimit);
  Serial.print(" ");

  size_t bytesIn = 0;
  esp_err_t result = i2s_read(I2S_PORT, &sBuffer, sizeof(sBuffer), &bytesIn, portMAX_DELAY);

  if (result == ESP_OK) {
    int samples_read = bytesIn / sizeof(int32_t);
    if (samples_read > 0) {
      float mean = 0;
      for (int i = 0; i < samples_read; i++) {
        mean += (sBuffer[i] >> 8);
      }
      mean /= samples_read;
      Serial.println(mean);
    }
  }
  delay(25);
}